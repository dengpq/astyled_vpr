#include <stdio.h>
#include <math.h>
#include "../include/util.h"
#include "../include/globals.h"
#include "../include/path_delay.h"
#include "../include/path_delay2.h"
#include "../include/net_delay.h"
#include "../include/timing_place_lookup.h"
#include "../include/timing_place.h"


float** timing_place_crit; /*float timing_place_crit[0..num_nets-1][1..num_pins-1] */

static struct s_linked_vptr* timing_place_crit_chunk_list_head;
static struct s_linked_vptr* net_delay_chunk_list_head;


/******** prototypes ******************/
static float** alloc_crit(struct s_linked_vptr** chunk_list_head_ptr);

static void free_crit(struct s_linked_vptr** chunk_list_head_ptr);
/**************************************/

static float** alloc_crit(struct s_linked_vptr** chunk_list_head_ptr)
{
    /* Allocates space for the timing_place_crit data structure *
     * [0..num_nets-1][1..num_pins-1].  I chunk the data to save space on large    *
     * problems.                                                                   */
    *chunk_list_head_ptr = NULL;

    float* tmp_ptr;
    int chunk_bytes_avail = 0;
    char* chunk_next_avail_mem = NULL;

    /* float load_crit[0..num_nets-1][1..num_pins-1] */
    float** local_crit = (float**)my_malloc(num_nets * sizeof(float*));

    int inet = -1;
    for (inet = 0; inet < num_nets; inet++) {
        tmp_ptr = (float*) my_chunk_malloc((net[inet].num_pins - 1) *
                                           sizeof(float), chunk_list_head_ptr, &chunk_bytes_avail,
                                           &chunk_next_avail_mem);
        local_crit[inet] = tmp_ptr - 1; /* only need [1..num_pins-1] */
    }

    return (local_crit);
}

/**************************************/
static void free_crit(struct s_linked_vptr** chunk_list_head_ptr)
{
    free_chunk_memory(*chunk_list_head_ptr);
    *chunk_list_head_ptr = NULL;
}
/**************************************/
void print_sink_delays(char* fname)
{
    int num_at_level, num_edges, inode, ilevel, i;
    FILE* fp;
    fp = my_fopen(fname, "w", 0);

    for (ilevel = num_tnode_levels - 1; ilevel >= 0; ilevel--) {
        num_at_level = tnodes_at_level[ilevel].nelem;

        for (i = 0; i < num_at_level; i++) {
            inode = tnodes_at_level[ilevel].list[i];
            num_edges = tnode[inode].num_edges;

            if (num_edges == 0) {   /* sink */
                fprintf(fp, "%g\n", tnode[inode].T_arr);
            }
        }
    }

    fclose(fp);
}

/*********************************************************************
 * FIXME: Calculate all tedges's criticality in Timing_Analyze_Graph *
**********************************************************************/
void load_criticalities(struct s_placer_opts placer_opts,
                        float** net_slack, float crit_delay,
                        float crit_exponent)
{
    /*set criticality values, returns the maximum criticality found*/
    /*assumes that net_slack contains correct values, ie. assumes  *
     *that load_net_slack has been called*/
    int inet = -1;
    for (inet = 0; inet < num_nets; ++inet) {
        if (inet == OPEN || is_global[inet]) {
            continue;
        }

        int ipin = 0;
        for (ipin = 1; ipin < net[inet].num_pins; ++ipin) {
        /* clip the criticality to never go negative(could happen for a constant *
         * generator since it's slack is huge).                                  *
         * criticality<SOURCE, ipin> = 1 - net_slack<SOURCE, ipn>/max_delay      */
            float pin_crit = max(1 - net_slack[inet][ipin] / crit_delay, 0.0);
            timing_place_crit[inet][ipin] = pow(pin_crit, crit_exponent);
        }
    }
} /* end of void load_criticalities(), calculate all tedges' Timing_Criticality */

/* FIXME: alloated all needed delay lookup matrix for timing-driven placement */
void alloc_lookups_and_criticalities(t_chan_width_dist chan_width_dist,
                                     struct s_router_opts  router_opts,
                                     struct s_det_routing_arch det_routing_arch,
                                     t_segment_inf* segment_inf,
                                     t_timing_inf  timing_inf,
                                     t_subblock_data subblock_data,
                                     float*** net_delay, float*** net_slack)
{
    /* alocate slack for PATH_TIMING_DRIVEN_PLACE and Timing-Analyze */
    (*net_slack) = alloc_and_load_timing_graph(timing_inf, subblock_data);

    /* allocate delay for TIMING_DRIVEN_PLACE, the net_slack was depend on *
     * the net_delay value.                                                */
    (*net_delay) = alloc_net_delay(&net_delay_chunk_list_head);

    /* compute 4 delay lookup matrixes for TIMING_DRIVEN_PLACEMENT */
    compute_delay_lookup_tables(router_opts, det_routing_arch, segment_inf,
                                timing_inf, chan_width_dist, subblock_data);

    /* allocate float** timing_crit[][] for */
    timing_place_crit = alloc_crit(&timing_place_crit_chunk_list_head);
} /* end of allocate 4 delay-lookup-matrix and timing_crit. */

/**************************************/
void free_lookups_and_criticalities(float*** net_delay, float*** net_slack)
{
    free(timing_place_crit);
    free_crit(&timing_place_crit_chunk_list_head);
    free_timing_graph(*net_slack);
    free_net_delay(*net_delay, &net_delay_chunk_list_head);
}
/**************************************/

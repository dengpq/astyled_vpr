#include <stdio.h>
#include "../include/util.h"
#include "../include/globals.h"
#include "../include/place_and_route.h"
#include "../include/place.h"
#include "../include/read_place.h"
#include "../include/route_export.h"
#include "../include/draw.h"
#include "../include/stats.h"
#include "../include/check_route.h"
#include "../include/rr_graph.h"
#include "../include/path_delay.h"
#include "../include/net_delay.h"
#include "../include/timing_place.h"


/******************* Subroutines local to this module ************************/

static int binary_search_place_and_route(struct s_placer_opts
                                         placer_opts, char* place_file, char* net_file, char* arch_file,
                                         char* route_file, boolean full_stats, boolean verify_binary_search,
                                         struct s_annealing_sched annealing_sched, struct s_router_opts
                                         router_opts, struct s_det_routing_arch det_routing_arch,
                                         t_segment_inf* segment_inf, t_timing_inf timing_inf, t_subblock_data
                                         *subblock_data_ptr, t_chan_width_dist chan_width_dist);

static float comp_width(t_chan* chan, float x, float separation);


/************************* Subroutine Definitions ****************************/
/*FIXME: the main function about VPR4.3 placement and routing  *
 *TODO: What about the t_segment_inf and t_timing_inf do?      */
void place_and_route(enum e_operation operation,
                     struct s_placer_opts placer_opts,
                     char* place_file, char* net_file,
                     char* arch_file, char* route_file,
                     boolean full_stats, boolean verify_binary_search,
                     struct s_annealing_sched annealing_sched,
                     struct s_router_opts router_opts,
                     struct s_det_routing_arch det_routing_arch,
                     t_segment_inf* segment_inf, t_timing_inf timing_inf,
                     t_subblock_data *subblock_data_ptr,
                     t_chan_width_dist chan_width_dist)
{
    /* This routine controls the overall placement and routing of a circuit. */
    char msg[BUFSIZE];
    int width_fac;
    boolean success;
    float**  net_delay;
    float**  net_slack;
    struct s_linked_vptr* net_delay_chunk_list_head;
    t_ivec** clb_opins_used_locally; /* [0..num_blocks-1][0..num_class-1] */

    if (placer_opts.place_freq == PLACE_NEVER) {
        read_place(place_file, net_file, arch_file, placer_opts, router_opts,
                   chan_width_dist, det_routing_arch, segment_inf, timing_inf,
                   subblock_data_ptr);
    } else if (placer_opts.place_freq == PLACE_ONCE) {
        try_place(placer_opts, annealing_sched, chan_width_dist,
                  router_opts, det_routing_arch, segment_inf,
                  timing_inf, subblock_data_ptr);
        print_place(place_file, net_file, arch_file);
    } else if (placer_opts.place_freq == PLACE_ALWAYS &&
               router_opts.fixed_channel_width != NO_FIXED_CHANNEL_WIDTH) {
        placer_opts.place_chan_width = router_opts.fixed_channel_width;
        try_place(placer_opts, annealing_sched, chan_width_dist,
                  router_opts, det_routing_arch, segment_inf,
                  timing_inf, subblock_data_ptr);
        print_place(place_file, net_file, arch_file);
    }

    fflush(stdout);

    if (operation == PLACE_ONLY) {
        return;
    }

    /* Binary search over channel width required? */

    if (router_opts.fixed_channel_width == NO_FIXED_CHANNEL_WIDTH) {
        width_fac = binary_search_place_and_route(placer_opts, place_file,
                                                  net_file, arch_file, route_file, full_stats, verify_binary_search,
                                                  annealing_sched, router_opts, det_routing_arch, segment_inf,
                                                  timing_inf, subblock_data_ptr, chan_width_dist);
        return;
    } else {  /* Only need to route (or try to route) once. */
        width_fac = router_opts.fixed_channel_width;
        /* Allocate the major routing structures. */
        clb_opins_used_locally = alloc_route_structs(*subblock_data_ptr);

        if (timing_inf.timing_analysis_enabled) {
            net_slack = alloc_and_load_timing_graph(timing_inf, *subblock_data_ptr);
            net_delay = alloc_net_delay(&net_delay_chunk_list_head);
        } else {
            net_delay = NULL;    /* Defensive coding. */
            net_slack = NULL;
        }

        /* Only needed to build timing graph and clb_opins_used_locally */
        free_subblock_data(subblock_data_ptr);
        success = try_route(width_fac, router_opts, det_routing_arch, segment_inf,
                            timing_inf, net_slack, net_delay, chan_width_dist,
                            clb_opins_used_locally);
    }

    if (success == FALSE) {
        printf("Circuit is unrouteable with a channel width factor of %d.\n\n",
               width_fac);
        sprintf(msg, "Routing failed with a channel width factor of %d.  ILLEGAL "
                "routing shown.", width_fac);
    } else {
        check_route(router_opts.route_type, det_routing_arch.num_switch,
                    clb_opins_used_locally);
        get_serial_num();
        printf("Circuit successfully routed with a channel width factor of %d."
               "\n\n", width_fac);
        routing_stats(full_stats, router_opts.route_type,
                      det_routing_arch.num_switch, segment_inf,
                      det_routing_arch.num_segment, det_routing_arch.R_minW_nmos,
                      det_routing_arch.R_minW_pmos, timing_inf.timing_analysis_enabled,
                      net_slack, net_delay);
        print_route(route_file);
#ifdef PRINT_SINK_DELAYS
        print_sink_delays("Routing_Sink_Delays.echo");
#endif
        sprintf(msg, "Routing succeeded with a channel width factor of %d.",
                width_fac);
    }

    init_draw_coords(pins_per_clb);
    update_screen(MAJOR, msg, ROUTING, timing_inf.timing_analysis_enabled);

    /* Do not forget free memory */
    if (timing_inf.timing_analysis_enabled) {
        free_timing_graph(net_slack);
        free_net_delay(net_delay, &net_delay_chunk_list_head);
    }
    free_route_structs(clb_opins_used_locally);
    fflush(stdout);
}

/* try to iterate route the netlist, to find the minimum tracks per channel that can route successfully */
static int binary_search_place_and_route(struct s_placer_opts placer_opts,
                                         char* place_file, char* net_file,
                                         char* arch_file, char* route_file,
                                         boolean full_stats, boolean verify_binary_search,
                                         struct s_annealing_sched annealing_sched,
                                         struct s_router_opts router_opts,
                                         struct s_det_routing_arch det_routing_arch,
                                         t_segment_inf* segment_inf,
                                         t_timing_inf timing_inf,
                                         t_subblock_data *subblock_data_ptr,
                                         t_chan_width_dist chan_width_dist)
{
    /* This routine performs a binary search to find the minimum number of      *
     * tracks per channel required to successfully route a circuit, and returns *
     * that minimum width_fac.                                                  */
    char msg[BUFSIZE];
    struct s_linked_vptr* net_delay_chunk_list_head;

    /* Allocate the major routing structures. */
    /* t_ivec clb_opins_used_locally[0..num_blocks-1][0..num_class-1] */
    t_ivec**  clb_opins_used_locally = alloc_route_structs(*subblock_data_ptr);
    t_ivec**  saved_clb_opins_used_locally;
    /* Saves the best routing found so far. */
    struct s_trace** best_routing = alloc_saved_routing(clb_opins_used_locally,
                                       &saved_clb_opins_used_locally);

    float**  net_delay = NULL;
    float**  net_slack = NULL;
    if (timing_inf.timing_analysis_enabled) {
        net_slack = alloc_and_load_timing_graph(timing_inf, *subblock_data_ptr);
        net_delay = alloc_net_delay(&net_delay_chunk_list_head);
    } else {
        net_delay = NULL; /* Defensive coding. */
        net_slack = NULL;
    }

    /* Only needed to build timing graph */
    free_subblock_data(subblock_data_ptr);
    int current = 2 * pins_per_clb;     /* Binary search part */
    int low = -1;
    int high = -1;
    int final = -1;

    boolean success, prev_success, prev2_success;
    while (final == -1) {
        fflush(stdout);
#ifdef VERBOSE
        printf("low = %d, high = %d, current = %d\n", low, high, current);
#endif

        /* Check if the channel width is huge to avoid overflow. Assume the *
         * circuit is unroutable with the current router options if we're   *
         * going to overflow.                                               */
        if (current > MAX_CHANNEL_WIDTH) {
            printf("This circuit appears to be unroutable with the current "
                   "router options.\n");
            printf("Aborting routing procedure.\n");
            exit(1);
        }

        if (placer_opts.place_freq == PLACE_ALWAYS) {
            /* using current channel width to placement */
            placer_opts.place_chan_width = current;
            try_place(placer_opts, annealing_sched, chan_width_dist,
                      router_opts, det_routing_arch, segment_inf,
                      timing_inf, subblock_data_ptr);
        }
        /* try to route according to current tracks per channel */
        success = try_route(current, router_opts, det_routing_arch, segment_inf,
                            timing_inf, net_slack, net_delay, chan_width_dist,
                            clb_opins_used_locally); /* FIXME */
        if (success) {
            high = current;
            /* If we're re-placing constantly, save placement in case it is best. */
            if (placer_opts.place_freq == PLACE_ALWAYS) {
                print_place(place_file, net_file, arch_file);
            }

            /* Save routing in case it is best. */
            save_routing(best_routing, clb_opins_used_locally,
                         saved_clb_opins_used_locally);

            if ((high - low) <= 1) {
                final = high;
            }

            if (low != -1) {
                current = (high + low) / 2;
            } else {
                current = high / 2; /* haven't found lower bound yet */
            }
        } else { /* last route not successful */
            low = current;

            if (high != -1) {
                if ((high - low) <= 1) {
                    final = high;
                }

                current = (high + low) / 2;
            } else {
                current = low * 2; /* Haven't found upper bound yet */
            }
        }
    }

    /* The binary search above occassionally does not find the minimum    *
     * routeable channel width.  Sometimes a circuit that will not route  *
     * in 19 channels will route in 18, due to router flukiness.  If      *
     * verify_binary_search is set, the code below will ensure that FPGAs *
     * with channel widths of final-2 and final-3 wil not route           *
     * successfully.  If one does route successfully, the router keeps    *
     * trying smaller channel widths until two in a row (e.g. 8 and 9)    *
     * fail.                                                              */

    if (verify_binary_search) {
        printf("\nVerifying that binary search found min. channel width ...\n");
        prev_success = TRUE; /* Actually final - 1 failed, but this makes router */
        /* try final-2 and final-3 even if both fail: safer */
        prev2_success = TRUE;
        current = final - 2;

        while (prev2_success || prev_success) {
            fflush(stdout);

            if (current < 1) {
                break;
            }

            if (placer_opts.place_freq == PLACE_ALWAYS) {
                placer_opts.place_chan_width = current;
                try_place(placer_opts, annealing_sched, chan_width_dist,
                          router_opts, det_routing_arch, segment_inf,
                          timing_inf, subblock_data_ptr);
            }

            success = try_route(current, router_opts, det_routing_arch,
                                segment_inf, timing_inf, net_slack, net_delay, chan_width_dist,
                                clb_opins_used_locally);

            if (success) {
                final = current;
                save_routing(best_routing, clb_opins_used_locally,
                             saved_clb_opins_used_locally);

                if (placer_opts.place_freq == PLACE_ALWAYS) {
                    print_place(place_file, net_file, arch_file);
                }
            }

            prev2_success = prev_success;
            prev_success = success;
            current--;
        }
    }   /* End binary search verification. */

    /* Restore the best placement (if necessary), the best routing, and  *
     * the best channel widths for final drawing and statistics output.  */
    init_chan(final, chan_width_dist);

    if (placer_opts.place_freq == PLACE_ALWAYS) {
        printf("Reading best placement back in.\n");
        placer_opts.place_chan_width = final;
        /* read placement file */
        read_place(place_file, net_file, arch_file, placer_opts, router_opts,
                   chan_width_dist, det_routing_arch, segment_inf, timing_inf,
                   subblock_data_ptr);
    }

    free_rr_graph(); /* free existed routing_resource graph */

    build_rr_graph(router_opts.route_type, det_routing_arch, segment_inf,
                   timing_inf, router_opts.base_cost_type);

    free_rr_graph_internals(router_opts.route_type, det_routing_arch, segment_inf,
                            timing_inf, router_opts.base_cost_type);

    restore_routing(best_routing, clb_opins_used_locally,
                    saved_clb_opins_used_locally);

    check_route(router_opts.route_type, det_routing_arch.num_switch,
                clb_opins_used_locally);
    get_serial_num();
    printf("Best routing used a channel width factor of %d.\n\n", final);
    routing_stats(full_stats, router_opts.route_type,
                  det_routing_arch.num_switch, segment_inf,
                  det_routing_arch.num_segment, det_routing_arch.R_minW_nmos,
                  det_routing_arch.R_minW_pmos, timing_inf.timing_analysis_enabled,
                  net_slack, net_delay);
    print_route(route_file);
#ifdef PRINT_SINK_DELAYS
    print_sink_delays("Routing_Sink_Delays.echo");
#endif
    init_draw_coords(pins_per_clb);
    sprintf(msg, "Routing succeeded with a channel width factor of %d.",
            final);
    update_screen(MAJOR, msg, ROUTING, timing_inf.timing_analysis_enabled);

    if (timing_inf.timing_analysis_enabled) {
        free_timing_graph(net_slack);
        free_net_delay(net_delay, &net_delay_chunk_list_head);
    }

    free_route_structs(clb_opins_used_locally);
    free_saved_routing(best_routing, saved_clb_opins_used_locally);
    fflush(stdout);
    return (final);
}


/* Assigns widths to channels(in tracks). The minimum value was one track per    *
 * channel. IO channels are io_rat * maximum in interior tracks wide. The channel*
 * tracks wide. The channel distributions read from the architecture file are    *
 * are scaled by cfactor.                                                        */
void init_chan(int cfactor, t_chan_width_dist chan_width_dist)
{
    float  chan_width_io = chan_width_dist.chan_width_io;
    t_chan chan_x_dist = chan_width_dist.chan_x_dist;
    t_chan chan_y_dist = chan_width_dist.chan_y_dist;

    /* io channel widths are io_ratio * maximum in interior tracks wide */
    int nio = (int)floor(cfactor * chan_width_io + 0.5);
    if (nio == 0) {
        nio = 1;    /* No zero width channels */
    }
    /* the 0 and nx or ny was io */
    chan_width_x[0] = chan_width_x[ny] = nio;
    chan_width_y[0] = chan_width_y[nx] = nio;

    float x, separation;
    int i;
    /* then initial chan_width_x[1...ny-1] */
    if (ny > 1) {
        separation = 1.0 / (ny - 2.0); /* Norm. distance between two channels in y-dix. */
        x = 0.0;    /* This avoids div by zero if ny = 2. */
        chan_width_x[1] = (int)floor(cfactor * comp_width(&chan_x_dist, x,
                                                          separation) + 0.5);
        /* No zero width channels */
        chan_width_x[1] = max(chan_width_x[1], 1);
        /* TODO: Why did the chan_width_x[1...ny-1] should initial like the
         * following statement? */
        for (i = 1; i < ny-1; ++i) {
            x = (float)i / ((float)(ny - 2.0));
            chan_width_x[i+1] = (int)floor(cfactor * comp_width(&chan_x_dist, x,
                                                                separation) + 0.5);
            chan_width_x[i+1] = max(chan_width_x[i+1], 1);
        }
    }
    /* then initial chan_width_y[1...nx-1] */
    if (nx > 1) {
        separation = 1.0 / (nx - 2.0); /* Norm. distance between two channels in x-dix. */
        x = 0.0;    /* Avoids div by zero if nx = 2. */
        chan_width_y[1] = (int)floor(cfactor * comp_width(&chan_y_dist, x,
                                                          separation) + 0.5);
        chan_width_y[1] = max(chan_width_y[1], 1);
        /* TODO: Q: like previous question. */
        for (i = 1; i < nx - 1; i++) {
            x = (float)i / ((float)(nx - 2.0));
            chan_width_y[i + 1] = (int)floor(cfactor * comp_width(&chan_y_dist, x,
                                                                   separation) + 0.5);
            chan_width_y[i + 1] = max(chan_width_y[i + 1], 1);
        }
    }

#ifdef VERBOSE
    printf("\nchan_width_x:\n");

    for (i = 0; i <= ny; i++) {
        printf("%d  ", chan_width_x[i]);
    }

    printf("\n\nchan_width_y:\n");

    for (i = 0; i <= nx; i++) {
        printf("%d  ", chan_width_y[i]);
    }

    printf("\n\n");
#endif
} /* end of void init_chan(int cfactor, t_chan_width_dist chan_width_dist) */


static float comp_width(t_chan* chan, float x, float separation)
{
    /* Return the relative channel density.  *chan points to a channel   *
     * functional description data structure, and x is the distance      *
     * (between 0 and 1) we are across the chip.  separation is the      *
     * distance between two channels, in the 0 to 1 coordinate system.   */
    float val;

    switch (chan->type) {
        case UNIFORM:
            val = chan->peak;
            break;

        case GAUSSIAN:
            val = (x - chan->xpeak) * (x - chan->xpeak) / (2 * chan->width *
                                                           chan->width);
            val = chan->peak * exp(-val);
            val += chan->dc;
            break;

        case PULSE:
            val = (float) fabs((double)(x - chan->xpeak));

            if (val > chan->width / 2.) {
                val = 0;
            } else {
                val = chan->peak;
            }

            val += chan->dc;
            break;

        case DELTA:
            val = x - chan->xpeak;

            if (val > -separation / 2. && val <= separation / 2.) {
                val = chan->peak;
            } else {
                val = 0.;
            }

            val += chan->dc;
            break;

        default:
            printf("Error in comp_width:  Unknown channel type %d.\n", chan->type);
            exit(1);
            break;
    }

    return(val);
}

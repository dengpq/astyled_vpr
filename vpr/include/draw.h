#ifndef DRAW_H
#define DRAW_H

#include "vpr_types.h"

void update_screen(int priority, char* msg, enum pic_type pic_on_screen_val,
                   boolean crit_path_button_enabled);

void alloc_draw_structs(void);

void init_draw_coords(float clb_width);

void set_graphics_state(boolean show_graphics_val, int gr_automode_val,
                        enum e_route_type route_type);

#endif


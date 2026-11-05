#pragma once
#include "lvgl.h"
#include "bambu_state.h"
void ui_filament_create(lv_obj_t *screen);
void ui_filament_show(const bambu_state_t *state, int unit_index);
void ui_filament_hide(void);
void ui_filament_forget(void);

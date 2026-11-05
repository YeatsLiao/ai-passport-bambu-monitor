#pragma once
#include "bambu_state.h"
// Overview + one page per discovered unit + a reported external spool.
int bambu_pages_count(const bambu_state_t *state);
int bambu_pages_unit(const bambu_state_t *state, int page); // -1 overview, -2 external
int bambu_pages_find(const bambu_state_t *state, int unit_id);

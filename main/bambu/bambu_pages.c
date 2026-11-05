#include "bambu_pages.h"
// Four-slot theme pages already include the external spool row.
static bool external_page(const bambu_state_t *st) {
    if (!st->external_present) return false;
    for (int i = 0; i < st->unit_count; ++i)
        if (st->units[i].slot_count > 1) return false;
    return true;
}
int bambu_pages_count(const bambu_state_t *st) {
    return 1 + st->unit_count + (external_page(st) ? 1 : 0);
}
int bambu_pages_unit(const bambu_state_t *st, int page) {
    if (page > 0 && page <= st->unit_count) return page - 1;
    if (external_page(st) && page == st->unit_count + 1) return -2;
    return -1;
}
int bambu_pages_find(const bambu_state_t *st, int unit_id) {
    if (unit_id == 254 && external_page(st)) return st->unit_count + 1;
    for (int i = 0; i < st->unit_count; ++i) if (st->units[i].id == unit_id) return i + 1;
    return 0;
}

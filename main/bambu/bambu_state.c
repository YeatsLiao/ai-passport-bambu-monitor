// main/bambu/bambu_state.c
#include "bambu_state.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
#define STATE_LOCK() portENTER_CRITICAL(&s_state_mux)
#define STATE_UNLOCK() portEXIT_CRITICAL(&s_state_mux)
#else
#define STATE_LOCK() ((void)0)
#define STATE_UNLOCK() ((void)0)
#endif

bambu_state_t g_bambu_state;

void bambu_state_init(void) {
    bambu_state_reset(&g_bambu_state);
}

void bambu_state_reset(bambu_state_t *st) {
    memset(st, 0, sizeof(*st));
    st->state = BAMBU_STATE_UNKNOWN;
    st->active_tray = 255;
    st->active_unit_id = st->active_slot_id = -1;
    st->vt_tray.remain = -1;
    for (int i = 0; i < BAMBU_AMS_TRAY_COUNT; ++i) st->trays[i].remain = -1;
}

void bambu_state_copy(bambu_state_t *out) {
    STATE_LOCK();
    memcpy(out, &g_bambu_state, sizeof(*out));
    STATE_UNLOCK();
}

void bambu_state_begin_connection(void) {
    STATE_LOCK();
    bambu_state_reset(&g_bambu_state);
    STATE_UNLOCK();
}

void bambu_state_commit(const bambu_state_t *state) {
    STATE_LOCK();
    memcpy(&g_bambu_state, state, sizeof(*state));
    STATE_UNLOCK();
}

void bambu_state_project(bambu_state_t *st, int unit_index) {
    memset(st->trays, 0, sizeof(st->trays));
    st->ams_count = 0;
    if (unit_index >= 0 && unit_index < st->unit_count) {
        const bambu_ams_unit_t *u = &st->units[unit_index];
        memcpy(st->trays, u->slots, sizeof(st->trays));
        st->ams_count = u->slot_count;
    }
    // Legacy theme renderers must not interpret another AMS/HT as external.
    if (st->active_tray != 254) st->active_tray = -1;
}

bool bambu_state_fresh(const bambu_state_t *st, uint32_t now_ms) {
    return st->data_valid && (uint32_t)(now_ms - st->last_update_ms) <= BAMBU_DATA_STALE_MS;
}

const char *bambu_ams_kind_name(bambu_ams_kind_t kind) {
    switch (kind) {
        case BAMBU_AMS_LITE: return "AMS Lite";
        case BAMBU_AMS_PRO: return "AMS 2 Pro";
        case BAMBU_AMS_HT: return "AMS HT";
        default: return "AMS";
    }
}

const char *bambu_state_str(bambu_print_state_t s) {
    switch (s) {
        case BAMBU_STATE_IDLE:    return "IDLE";
        case BAMBU_STATE_RUNNING: return "RUNNING";
        case BAMBU_STATE_PAUSE:   return "PAUSE";
        case BAMBU_STATE_FINISH:  return "FINISH";
        case BAMBU_STATE_FAILED:  return "FAILED";
        case BAMBU_STATE_PREPARE: return "PREPARE";
        default:                  return "UNKNOWN";
    }
}

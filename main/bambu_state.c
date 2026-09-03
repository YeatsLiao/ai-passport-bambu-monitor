// main/bambu_state.c
#include "bambu_state.h"
#include <string.h>

bambu_state_t g_bambu_state;

void bambu_state_init(void) {
    memset(&g_bambu_state, 0, sizeof(g_bambu_state));
    g_bambu_state.state = BAMBU_STATE_IDLE;
    strcpy(g_bambu_state.gcode_state, "IDLE");
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

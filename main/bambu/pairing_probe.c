#include "pairing_probe.h"
#include "bambu_report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pairing_probe_init(pairing_probe_t *p, const char *serial) {
    memset(p, 0, sizeof(*p));
    snprintf(p->topic, sizeof(p->topic), "device/%s/report", serial);
}
void pairing_probe_free(pairing_probe_t *p) {
    free(p->buffer);
    p->buffer = NULL;
    p->total = p->used = p->capacity = 0;
    p->requested = false;
}
bool pairing_probe_data(pairing_probe_t *p, const char *topic, size_t topic_len,
                        const char *data, size_t len, size_t total, size_t offset, bool retained) {
    if (!offset) {
        p->total = p->used = 0;
        if (!p->requested || retained || !topic || topic_len != strlen(p->topic) ||
            memcmp(topic, p->topic, topic_len) || !total || total >= 96 * 1024) return false;
        if (total + 1 > p->capacity) {
            char *next = realloc(p->buffer, total + 1);
            if (!next) return false;
            p->buffer = next;
            p->capacity = total + 1;
        }
        p->total = total;
    }
    if (!data || !len || !p->total || retained || total != p->total || offset != p->used ||
        offset > total || len > total - offset) { p->total = p->used = 0; return false; }
    memcpy(p->buffer + offset, data, len);
    p->used += len;
    if (p->used != p->total) return false;
    p->buffer[p->used] = 0;
    bool ok = bambu_report_validate(p->buffer, p->used);
    p->total = p->used = 0;
    return ok;
}

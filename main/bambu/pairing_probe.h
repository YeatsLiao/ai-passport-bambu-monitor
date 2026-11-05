#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    char topic[96];
    char *buffer;
    size_t total, used, capacity;
    bool requested;
} pairing_probe_t;
void pairing_probe_init(pairing_probe_t *p, const char *serial);
void pairing_probe_free(pairing_probe_t *p);
bool pairing_probe_data(pairing_probe_t *p, const char *topic, size_t topic_len,
                        const char *data, size_t len, size_t total, size_t offset, bool retained);

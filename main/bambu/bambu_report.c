// Small-memory report reader: validate the wire document, then decode only the
// requested scalars and individual tray objects. No full cJSON report tree.
#include "bambu_report.h"
#include "bambu_state.h"
#include "cJSON.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bambu_state_t s_next; // MQTT is the single writer; keep this off its stack.

static const char *ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

static const char *string_end(const char *p) {
    if (*p++ != '"') return NULL;
    while (*p && *p != '"') {
        if ((unsigned char)*p < 32) return NULL;
        if (*p++ == '\\') {
            if (!*p) return NULL;
            if (*p == 'u') {
                ++p;
                for (int i = 0; i < 4; ++i) {
                    if (!isxdigit((unsigned char)*p)) return NULL;
                    ++p;
                }
            } else {
                if (!strchr("\"\\/bfnrt", *p)) return NULL;
                ++p;
            }
        }
    }
    return *p == '"' ? p + 1 : NULL;
}

static const char *value_end(const char *p, unsigned depth) {
    p = ws(p);
    if (depth > 32) return NULL;
    if (*p == '"') return string_end(p);
    if (*p == '{' || *p == '[') {
        bool object = *p == '{';
        char close = object ? '}' : ']';
        p = ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (object) {
                p = string_end(p);
                if (!p || *(p = ws(p)) != ':') return NULL;
                p = ws(p + 1);
            }
            p = value_end(p, depth + 1);
            if (!p) return NULL;
            p = ws(p);
            if (*p == close) return p + 1;
            if (*p != ',') return NULL;
            p = ws(p + 1);
        }
    }
    if (!strncmp(p, "true", 4) || !strncmp(p, "null", 4)) return p + 4;
    if (!strncmp(p, "false", 5)) return p + 5;
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else {
        if (*p < '1' || *p > '9') return NULL;
        do { ++p; } while (isdigit((unsigned char)*p));
    }
    if (*p == '.') {
        ++p;
        if (!isdigit((unsigned char)*p)) return NULL;
        do { ++p; } while (isdigit((unsigned char)*p));
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (!isdigit((unsigned char)*p)) return NULL;
        do { ++p; } while (isdigit((unsigned char)*p));
    }
    return p;
}

// Input has already passed value_end(), so all cursors below are bounded by NUL.
static const char *field(const char *obj, const char *key) {
    if (!obj || *(obj = ws(obj)) != '{') return NULL;
    const char *p = ws(obj + 1);
    size_t n = strlen(key);
    while (*p != '}') {
        const char *end = string_end(p);
        if (!end) return NULL;
        bool match = (size_t)(end - p - 2) == n && !memcmp(p + 1, key, n);
        p = ws(ws(end) + 1);
        if (match) return p;
        p = ws(value_end(p, 0));
        if (*p != ',') break;
        p = ws(p + 1);
    }
    return NULL;
}

static const char *element(const char **cursor) {
    const char *p = ws(*cursor);
    if (*p == ']') return NULL;
    const char *end = ws(value_end(p, 0));
    *cursor = *end == ',' ? end + 1 : end;
    return p;
}

static bool number(const char *p, double lo, double hi, double *out) {
    if (!p) return false;
    bool quoted = *p == '"';
    if (quoted) ++p;
    if (*p != '-' && !isdigit((unsigned char)*p)) return false;
    char *end;
    double v = strtod(p, &end);
    if (p == end || !isfinite(v) || v < lo || v > hi) return false;
    if (quoted ? *end != '"' : (*end && *end != ',' && *end != '}' && *end != ']' && !isspace((unsigned char)*end))) return false;
    *out = v;
    return true;
}

static bool integer(const char *p, int lo, int hi, int *out) {
    double n;
    if (!number(p, lo, hi, &n) || floor(n) != n) return false;
    *out = (int)n;
    return true;
}

static cJSON *fragment(const char *p) {
    if (!p) return NULL;
    const char *end = value_end(p, 0);
    return end ? cJSON_ParseWithLength(p, (size_t)(end - p)) : NULL;
}

static bool text(const char *p, char *out, size_t size) {
    if (!p || *p != '"') return false;
    cJSON *v = fragment(p);
    if (!cJSON_IsString(v)) { cJSON_Delete(v); return false; }
    size_t n = strlen(v->valuestring);
    if (n >= size) n = size - 1;
    // Never cut inside a UTF-8 sequence.
    while (n && ((unsigned char)v->valuestring[n] & 0xc0) == 0x80) --n;
    memcpy(out, v->valuestring, n);
    out[n] = 0;
    cJSON_Delete(v);
    return true;
}

static bool read_float(const char *obj, const char *key, float *out, uint32_t bit,
                       double lo, double hi) {
    double n;
    if (!number(field(obj, key), lo, hi, &n)) return false;
    *out = (float)n;
    s_next.valid_mask |= bit;
    return true;
}

static bool read_int(const char *obj, const char *key, int *out, uint32_t bit,
                     int lo, int hi) {
    if (!integer(field(obj, key), lo, hi, out)) return false;
    s_next.valid_mask |= bit;
    return true;
}

static void reset_tray(bambu_ams_tray_t *t, int id) {
    memset(t, 0, sizeof(*t));
    t->tag = id;
    t->remain = -1;
}

static bool merge_tray(const char *obj, bambu_ams_tray_t *t) {
    cJSON *tray = fragment(obj);
    if (!cJSON_IsObject(tray)) { cJSON_Delete(tray); return false; }
    cJSON *v = cJSON_GetObjectItemCaseSensitive(tray, "tray_type");
    if (cJSON_IsString(v) && !v->valuestring[0]) reset_tray(t, t->tag);
    // Explicitly empty type clears a slot; missing attributes preserve a delta.
    if (cJSON_IsString(v)) {
        strncpy(t->type, v->valuestring, sizeof(t->type) - 1);
        t->type[sizeof(t->type) - 1] = 0;
    }
    v = cJSON_GetObjectItemCaseSensitive(tray, "tray_color");
    if (!cJSON_IsString(v)) v = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(tray, "cols"), 0);
    if (cJSON_IsString(v)) {
        size_t n = strlen(v->valuestring);
        bool valid = n == 6 || n == 8;
        for (size_t i = 0; valid && i < n; ++i) valid = isxdigit((unsigned char)v->valuestring[i]);
        if (valid) memcpy(t->color, v->valuestring, n + 1);
    }
    v = cJSON_GetObjectItemCaseSensitive(tray, "tray_sub_brands");
    if (cJSON_IsString(v)) {
        strncpy(t->sub_brand, v->valuestring, sizeof(t->sub_brand) - 1);
        t->sub_brand[sizeof(t->sub_brand) - 1] = 0;
    }
    double remain;
    if (number(field(obj, "remain"), -1, 100, &remain)) t->remain = (float)remain;
    t->translucent = (strlen(t->color) == 8 && !strcmp(t->color + 6, "00")) ||
                     strstr(t->sub_brand, "Translucent") || strstr(t->sub_brand, "translucent");
    t->reported = true;
    cJSON_Delete(tray);
    return true;
}

static bambu_ams_unit_t *get_unit(int id) {
    for (int i = 0; i < s_next.unit_count; ++i)
        if (s_next.units[i].id == id) return &s_next.units[i];
    if (s_next.unit_count == BAMBU_MAX_AMS_UNITS) { s_next.ams_overflow = true; return NULL; }
    bambu_ams_unit_t *u = &s_next.units[s_next.unit_count++];
    memset(u, 0, sizeof(*u));
    u->id = id;
    u->kind = id >= 128 && id <= 135 ? BAMBU_AMS_HT : BAMBU_AMS_STANDARD;
    u->slot_count = u->kind == BAMBU_AMS_HT ? 1 : 4;
    u->temperature = -1;
    u->humidity = u->humidity_level = u->dry_minutes = u->dry_state = -1;
    for (int j = 0; j < 4; ++j) reset_tray(&u->slots[j], j);
    return u;
}

static bool hex_info(const char *p, unsigned long *out) {
    char buf[20];
    if (!text(p, buf, sizeof(buf)) || !buf[0] || strlen(buf) > 8) return false;
    for (const char *c = buf; *c; ++c) if (!isxdigit((unsigned char)*c)) return false;
    *out = strtoul(buf, NULL, 16);
    return true;
}

static bool bitmask(const char *p, uint32_t *out) {
    // Studio reads *_bits strings as hexadecimal, not decimal. Native JSON
    // numbers still mean their numeric value. Info strings are hexadecimal too.
    unsigned long hex;
    if (p && *p == '"') {
        if (!hex_info(p, &hex)) return false;
        *out = (uint32_t)hex;
        return true;
    }
    double n;
    if (!number(p, 0, UINT32_MAX, &n) || floor(n) != n) return false;
    *out = (uint32_t)n;
    return true;
}

static bool merge_unit(const char *obj) {
    int id;
    if (!integer(field(obj, "id"), 0, 253, &id)) return false;
    bambu_ams_unit_t *u = get_unit(id);
    if (!u) return true; // recognized topology, overflow is explicitly exposed
    unsigned long info;
    if (hex_info(field(obj, "info"), &info)) {
        unsigned type = info & 15;
        if (type >= 1 && type <= 4) u->kind = (bambu_ams_kind_t)type;
        if (u->kind == BAMBU_AMS_PRO || u->kind == BAMBU_AMS_HT) u->dry_state = (info >> 4) & 15;
        u->slot_count = u->kind == BAMBU_AMS_HT ? 1 : 4;
    }
    double n;
    if (number(field(obj, "temp"), -40, 150, &n)) u->temperature = n;
    integer(field(obj, "humidity_raw"), 0, 100, &u->humidity);
    integer(field(obj, "humidity"), 0, 5, &u->humidity_level);
    integer(field(obj, "dry_time"), 0, 100000, &u->dry_minutes);
    const char *a = field(obj, "tray");
    if (a && *a == '[') {
        const char *it = a + 1, *v;
        while ((v = element(&it))) {
            int slot;
            if (integer(field(v, "id"), 0, u->slot_count - 1, &slot)) merge_tray(v, &u->slots[slot]);
        }
    }
    return true;
}

static int unit_cmp(const void *a, const void *b) {
    return ((const bambu_ams_unit_t *)a)->id - ((const bambu_ams_unit_t *)b)->id;
}

static void active_slots(void) {
    s_next.active_unit_id = s_next.active_slot_id = -1;
    for (int i = 0; i < s_next.unit_count; ++i) {
        bambu_ams_unit_t *u = &s_next.units[i];
        for (int j = 0; j < u->slot_count; ++j) {
            // BambuStudio CollectTrayIndexMap: HT uses its AMS ID, regular
            // four-slot devices use AMS ID * 4 + slot. External is exactly 254.
            int wire = u->kind == BAMBU_AMS_HT ? u->id : u->id * 4 + j;
            bool active = s_next.active_tray != 254 && s_next.active_tray != 255 && wire == s_next.active_tray;
            u->slots[j].active = active;
            if (active) { s_next.active_unit_id = u->id; s_next.active_slot_id = j; }
        }
    }
    s_next.vt_tray.active = s_next.active_tray == 254;
    memset(s_next.trays, 0, sizeof(s_next.trays));
    s_next.ams_count = 0;
    if (s_next.unit_count) {
        memcpy(s_next.trays, s_next.units[0].slots, sizeof(s_next.trays));
        s_next.ams_count = s_next.units[0].slot_count;
    }
}

static bool merge_ams(const char *ams) {
    if (!ams || *ams != '{') return false;
    bool changed = false;
    int active;
    if (integer(field(ams, "tray_now"), 0, 1023, &active)) { s_next.active_tray = active; changed = true; }
    uint32_t bits;
    const char *raw_bits = field(ams, "ams_exist_bits_raw");
    bool has_bits = bitmask(raw_bits ? raw_bits : field(ams, "ams_exist_bits"), &bits);
    if (has_bits) {
        uint32_t mask = (uint32_t)bits;
        for (int i = s_next.unit_count - 1; i >= 0; --i) {
            int id = s_next.units[i].id;
            int bit = id >= 0 && id < 4 ? id : id >= 128 && id < 136 ? 16 + id - 128 : -1;
            if (bit >= 0 && !(mask & (1U << bit))) {
                memmove(&s_next.units[i], &s_next.units[i + 1], (s_next.unit_count - i - 1) * sizeof(s_next.units[0]));
                --s_next.unit_count;
            }
        }
        changed = true;
    }
    const char *a = field(ams, "ams");
    if (a && *a == '[') {
        const char *it = a + 1, *v;
        while ((v = element(&it))) changed |= merge_unit(v);
        if (*ws(a + 1) == ']' && has_bits && (uint32_t)bits == 0) s_next.unit_count = 0;
    }
    uint32_t tray_bits;
    if (bitmask(field(ams, "tray_exist_bits"), &tray_bits)) {
        uint32_t mask = (uint32_t)tray_bits;
        for (int i = 0; i < s_next.unit_count; ++i) {
            bambu_ams_unit_t *u = &s_next.units[i];
            for (int j = 0; j < u->slot_count; ++j) {
                int bit = u->id < 4 ? u->id * 4 + j :
                          u->id >= 128 && u->id < 136 ? 16 + u->id - 128 + j : -1;
                if (bit >= 0 && bit < 32 && !(mask & (1U << bit))) reset_tray(&u->slots[j], j);
            }
        }
        changed = true;
    }
    if (has_bits) {
        uint32_t mask = (uint32_t)bits & 0x00ff000fU;
        unsigned count = 0;
        while (mask) { count += mask & 1; mask >>= 1; }
        s_next.ams_overflow = count > BAMBU_MAX_AMS_UNITS;
    }
    qsort(s_next.units, s_next.unit_count, sizeof(s_next.units[0]), unit_cmp);
    return changed;
}

static bool fans(const char *print) {
    int gear;
    if (integer(field(print, "fan_gear"), 0, 0xffffff, &gear)) {
        s_next.cooling_fan = (int)lround((gear & 255) * 100.0 / 255);
        s_next.big_fan1 = (int)lround(((gear >> 8) & 255) * 100.0 / 255);
        s_next.big_fan2 = (int)lround(((gear >> 16) & 255) * 100.0 / 255);
        s_next.valid_mask |= BAMBU_VALID_COOLING | BAMBU_VALID_AUX_FAN | BAMBU_VALID_CHAMBER_FAN;
        return true;
    }
    const char *keys[] = {"cooling_fan_speed", "big_fan1_speed", "big_fan2_speed"};
    int *out[] = {&s_next.cooling_fan, &s_next.big_fan1, &s_next.big_fan2};
    uint32_t flags[] = {BAMBU_VALID_COOLING, BAMBU_VALID_AUX_FAN, BAMBU_VALID_CHAMBER_FAN};
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        double n;
        if (number(field(print, keys[i]), 0, 15, &n)) {
            // Match Studio's legacy wire -> 0..255 conversion, then percent.
            *out[i] = (int)lround(round(floor(n / 1.5) * 25.5) * 100.0 / 255);
            s_next.valid_mask |= flags[i];
            changed = true;
        }
    }
    return changed;
}

// Model identifiers come from Studio machine profiles. Never infer a printer
// from an AMS module, firmware number, or an unverified serial prefix.
static bool model_name(const char *value) {
    char raw[64];
    if (!text(value, raw, sizeof(raw))) return false;
    const char *name = raw;
    if (!strncmp(name, "Bambu Lab ", 10)) name += 10;
    if (!strcmp(name, "BL-P001") || !strcmp(name, "X1 Carbon")) name = "X1C";
    else if (!strcmp(name, "O1D")) name = "H2D";
    else if (!strcmp(name, "BL-P002")) name = "X1";
    else if (!strcmp(name, "C11")) name = "P1P";
    else if (!strcmp(name, "C12")) name = "P1S";
    else if (!strcmp(name, "C13")) name = "X1E";
    else if (!strcmp(name, "N2S")) name = "A1";
    else if (!strcmp(name, "N1")) name = "A1 mini";
    const char *known[] = {"X1C", "X1", "X1E", "P1P", "P1S", "A1", "A1 mini", "H2D", "H2S", "H2C", "H2D Pro", "X2D"};
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (!strcmp(name, known[i])) {
            strcpy(s_next.printer_model, name);
            return true;
        }
    }
    return false;
}

static bool identity(const char *json, const char *print) {
    if (model_name(field(print, "printer_type"))) return true;
    const char *info = field(json, "info");
    char command[24], result[16];
    if (!text(field(info, "command"), command, sizeof(command)) || strcmp(command, "get_version")) return false;
    if (text(field(info, "result"), result, sizeof(result)) && !strcmp(result, "fail")) return false;
    const char *modules = field(info, "module");
    if (!modules || *modules != '[') return false;
    const char *it = modules + 1, *v;
    while ((v = element(&it))) {
        char name[24];
        if (text(field(v, "name"), name, sizeof(name)) && !strcmp(name, "ota")) {
            if (model_name(field(v, "product_name")) || model_name(field(v, "hw_ver"))) return true;
        }
    }
    return false;
}

static bool parse_report(const char *json, size_t length, uint32_t now_ms, bool publish) {
    // Caller owns a NUL-terminated receive buffer of length+1 bytes.
    if (!json || !length || strlen(json) != length) return false;
    const char *end = value_end(json, 0);
    if (!end || *ws(end)) return false;
    const char *print = field(json, "print");
    if (publish) bambu_state_copy(&s_next);
    else memset(&s_next, 0, sizeof(s_next));
    bool identified = publish && identity(json, print);
    if (!print || *print != '{') {
        if (identified) bambu_state_commit(&s_next);
        return false; // Identity never proves live telemetry or pairing success.
    }
    bool changed = false;
    changed |= read_float(print, "nozzle_temper", &s_next.nozzle_temp, BAMBU_VALID_NOZZLE, -40, 500);
    changed |= read_float(print, "nozzle_target_temper", &s_next.nozzle_target, BAMBU_VALID_NOZZLE_TARGET, 0, 500);
    changed |= read_float(print, "bed_temper", &s_next.bed_temp, BAMBU_VALID_BED, -40, 200);
    changed |= read_float(print, "bed_target_temper", &s_next.bed_target, BAMBU_VALID_BED_TARGET, 0, 200);
    const char *ctc = field(field(field(print, "device"), "ctc"), "info");
    if (!read_float(ctc, "temp", &s_next.chamber_temp, BAMBU_VALID_CHAMBER, -40, 200))
        changed |= read_float(print, "chamber_temper", &s_next.chamber_temp, BAMBU_VALID_CHAMBER, -40, 200);
    else changed = true;
    changed |= read_int(print, "layer_num", &s_next.layer_num, BAMBU_VALID_LAYER, 0, 1000000);
    changed |= read_int(print, "total_layer_num", &s_next.total_layer, BAMBU_VALID_TOTAL_LAYER, 0, 1000000);
    changed |= read_int(print, "mc_percent", &s_next.mc_percent, BAMBU_VALID_PROGRESS, 0, 100);
    changed |= read_int(print, "mc_remaining_time", &s_next.mc_remaining, BAMBU_VALID_REMAIN, 0, 1000000);
    changed |= read_int(print, "spd_lvl", &s_next.spd_lvl, BAMBU_VALID_SPEED_LEVEL, 1, 5);
    changed |= read_int(print, "spd_mag", &s_next.spd_mag, BAMBU_VALID_SPEED, 0, 1000);
    char state[sizeof(s_next.gcode_state)];
    if (text(field(print, "gcode_state"), state, sizeof(state)) && state[0]) {
        const char *names[] = {"IDLE", "RUNNING", "PAUSE", "FINISH", "FAILED", "PREPARE"};
        s_next.state = BAMBU_STATE_UNKNOWN;
        for (int i = 0; i < 6; ++i) if (!strcmp(names[i], state)) s_next.state = (bambu_print_state_t)i;
        memcpy(s_next.gcode_state, state, sizeof(state));
        changed = true;
    }
    changed |= text(field(print, "gcode_file"), s_next.gcode_file, sizeof(s_next.gcode_file));
    changed |= fans(print);
    changed |= merge_ams(field(print, "ams"));
    const char *vt = field(print, "vt_tray");
    if (vt && *vt == '{' && merge_tray(vt, &s_next.vt_tray)) {
        s_next.external_present = true;
        changed = true;
    }
    if (!changed) {
        if (identified) bambu_state_commit(&s_next);
        return false;
    }
    active_slots();
    s_next.last_update_ms = now_ms;
    s_next.data_valid = true;
    if (publish) bambu_state_commit(&s_next);
    return true;
}

bool bambu_report_apply(const char *json, size_t length, uint32_t now_ms) {
    return parse_report(json, length, now_ms, true);
}

bool bambu_report_validate(const char *json, size_t length) {
    return parse_report(json, length, 0, false);
}

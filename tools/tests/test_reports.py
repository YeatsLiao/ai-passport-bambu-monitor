"""Run the actual embedded report reader on the host (no printer needed).

python tools/tests/test_reports.py --idf D:/.../esp-idf-v5.5.5
Requires a native C compiler (GCC/Clang), and ESP-IDF's bundled cJSON source.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include "bambu_report.h"
#include "pairing_probe.h"
#include "bambu_state.h"
#include "bambu_pages.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define APPLY(s) assert(bambu_report_apply(s, strlen(s), ++now))
#define IGNORE(s) assert(!bambu_report_apply(s, strlen(s), ++now))
static uint32_t now = 1000;
static bambu_state_t view;
int main(int argc, char **argv) {

    pairing_probe_t probe;
    pairing_probe_init(&probe, "TEST123");
    const char *topic = "device/TEST123/report";
    const char *good = "{\"print\":{\"nozzle_temper\":23}}";
    size_t size = strlen(good);
    assert(!pairing_probe_data(&probe, topic, strlen(topic), good, size, size, 0, false));
    probe.requested = true;
    assert(!pairing_probe_data(&probe, "device/WRONG/report", 19, good, size, size, 0, false));
    assert(!pairing_probe_data(&probe, topic, strlen(topic), good, size, size, 0, true));
    assert(!pairing_probe_data(&probe, topic, strlen(topic), "{\"print\":{}}", 12, 12, 0, false));
    assert(!pairing_probe_data(&probe, topic, strlen(topic), good, 10, size, 0, false));
    assert(!pairing_probe_data(&probe, NULL, 0, good + 11, size - 11, size, 11, false));
    assert(!pairing_probe_data(&probe, topic, strlen(topic), good, 10, size, 0, false));
    assert(pairing_probe_data(&probe, NULL, 0, good + 10, size - 10, size, 10, false));
    assert(!g_bambu_state.data_valid); // Verification must not publish candidate data.
    assert(!pairing_probe_data(&probe, topic, strlen(topic), good, 10, 96 * 1024, 0, false));
    char *large = malloc(24000); assert(large);
    const char *prefix = "{\"print\":{\"nozzle_temper\":23,\"padding\":\"";
    size_t head = strlen(prefix); memcpy(large, prefix, head);
    memset(large + head, 'x', 23000); strcpy(large + head + 23000, "\"}}");
    size_t large_size = strlen(large);
    bool proved = false;
    for (size_t offset = 0; offset < large_size; offset += 1024) {
        size_t len = large_size - offset; if (len > 1024) len = 1024;
        proved = pairing_probe_data(&probe, offset ? NULL : topic,
                                    offset ? 0 : strlen(topic), large + offset,
                                    len, large_size, offset, false);
        assert(proved == (offset + len == large_size));
    }
    assert(proved && !g_bambu_state.data_valid); free(large);
    pairing_probe_free(&probe);
    bambu_state_init();
    assert(!g_bambu_state.data_valid && g_bambu_state.active_tray == 255);
    const char *identity = "{\"info\":{\"command\":\"get_version\",\"module\":[{\"name\":\"ams\",\"product_name\":\"H2D\"},{\"name\":\"ota\",\"product_name\":\"Bambu Lab X1 Carbon\"}]}}";
    assert(!bambu_report_apply(identity, strlen(identity), ++now));
    assert(!strcmp(g_bambu_state.printer_model, "X1C"));
    assert(!g_bambu_state.data_valid && !g_bambu_state.last_update_ms);
    assert(!bambu_report_validate(identity, strlen(identity)));
    IGNORE("{\"print\":{\"printer_type\":\"O1D\"}}");
    assert(!strcmp(g_bambu_state.printer_model, "H2D"));
    IGNORE("{\"print\":{\"printer_type\":\"unknown-model\"}}");
    assert(!strcmp(g_bambu_state.printer_model, "H2D"));
    IGNORE("{\"info\":{\"command\":\"get_version\",\"module\":[{\"name\":\"ams\",\"product_name\":\"X1C\"}]}}");
    assert(!strcmp(g_bambu_state.printer_model, "H2D"));
    IGNORE("{\"info\":{\"command\":\"get_version\",\"result\":\"fail\",\"module\":[{\"name\":\"ota\",\"product_name\":\"X1C\"}]}}");
    assert(!strcmp(g_bambu_state.printer_model, "H2D"));
    bambu_state_begin_connection();
    assert(!g_bambu_state.printer_model[0]);

    assert(bambu_pages_count(&g_bambu_state) == 1);
    bambu_state_t pages;
    bambu_state_reset(&pages);
    pages.external_present = true;
    assert(bambu_pages_count(&pages) == 2 && bambu_pages_unit(&pages, 1) == -2);
    pages.unit_count = 1; pages.units[0].id = 128; pages.units[0].slot_count = 1;
    assert(bambu_pages_count(&pages) == 3 && bambu_pages_find(&pages, 254) == 2);
    pages.unit_count = 2; pages.units[1].id = 0; pages.units[1].slot_count = 4;
    assert(bambu_pages_count(&pages) == 3 && bambu_pages_find(&pages, 254) == 0);
    assert(bambu_pages_unit(&pages, 3) == -1);
    IGNORE("{\"print\":42}");
    IGNORE("{\"print\":{}}");
    IGNORE("{\"print\":{\"command\":\"pushall\"}}");
    IGNORE("{\"print\":{\"mc_percent\":40}");
    IGNORE("{\"print\":{\"mc_percent\":40,}}");
    IGNORE("{\"print\":{\"mc_percent\":040}}");
    IGNORE("{\"print\":{\"mc_percent\":true}}");
    IGNORE("{\"print\":{\"mc_percent\":101}}");
    IGNORE("{\"print\":{\"nozzle_temper\":\"2x\"}}");
    IGNORE("{\"print\":{\"nozzle_temper\":null}}");
    assert(!g_bambu_state.data_valid);
    APPLY("{\"print\":{\"nozzle_temper\":\"245\",\"bed_temper\":70,\"mc_percent\":0}}");
    assert(g_bambu_state.nozzle_temp == 245 && g_bambu_state.bed_temp == 70);
    assert(g_bambu_state.valid_mask & BAMBU_VALID_PROGRESS);
    assert(!(g_bambu_state.valid_mask & BAMBU_VALID_CHAMBER));
    APPLY("{\"print\":{\"chamber_temper\":0,\"device\":{\"ctc\":{\"info\":{\"temp\":45}}}}}");
    assert(g_bambu_state.chamber_temp == 45);
    APPLY("{\"print\":{\"cooling_fan_speed\":\"2\",\"big_fan1_speed\":\"0\",\"big_fan2_speed\":\"10\"}}");
    assert(g_bambu_state.cooling_fan == 10 && g_bambu_state.big_fan1 == 0 && g_bambu_state.big_fan2 == 60);
    APPLY("{\"print\":{\"fan_gear\":16711935}}");
    assert(g_bambu_state.cooling_fan == 100 && g_bambu_state.big_fan1 == 0 && g_bambu_state.big_fan2 == 100);
    APPLY("{\"print\":{\"ams\":{\"tray_now\":\"4\",\"ams\":[{\"id\":\"128\",\"info\":\"14\",\"temp\":\"65\",\"humidity_raw\":\"12\",\"dry_time\":90,\"tray\":[{\"id\":\"0\",\"tray_type\":\"PA-CF\"}]},{\"id\":\"1\",\"tray\":[{\"id\":\"3\",\"tray_type\":\"PETG\"},{\"id\":\"0\",\"tray_type\":\"PLA\",\"tray_color\":\"000000FF\",\"remain\":50}]}]}}}");
    assert(g_bambu_state.unit_count == 2 && g_bambu_state.units[0].id == 1);
    assert(bambu_pages_count(&g_bambu_state) == 3 && bambu_pages_find(&g_bambu_state, 128) == 2);
    assert(g_bambu_state.units[1].kind == BAMBU_AMS_HT && g_bambu_state.units[1].slot_count == 1);
    assert(g_bambu_state.units[1].temperature == 65 && g_bambu_state.units[1].humidity == 12);
    assert(!g_bambu_state.units[0].slots[0].translucent);
    assert(g_bambu_state.units[0].slots[0].active);
    APPLY("{\"print\":{\"ams\":{\"ams_exist_bits\":\"2\",\"ams_exist_bits_raw\":\"10002\"}}}");
    assert(g_bambu_state.unit_count == 2);
    APPLY("{\"print\":{\"ams\":{\"tray_now\":\"128\"}}}");
    assert(!g_bambu_state.units[0].slots[0].active && g_bambu_state.units[1].slots[0].active);
    APPLY("{\"print\":{\"ams\":{\"ams\":[{\"id\":\"1\",\"tray\":[{\"id\":\"0\",\"remain\":42}]}]}}}");
    assert(!strcmp(g_bambu_state.units[0].slots[0].type, "PLA"));
    assert(!strcmp(g_bambu_state.units[0].slots[3].type, "PETG"));
    assert(g_bambu_state.units[0].slots[0].remain == 42);
    APPLY("{\"print\":{\"vt_tray\":{\"id\":\"254\",\"tray_type\":\"TPU\",\"remain\":-1}}}");
    assert(g_bambu_state.external_present && !strcmp(g_bambu_state.vt_tray.type,"TPU"));
    assert(g_bambu_state.vt_tray.remain == -1);
    APPLY("{\"print\":{\"ams\":{\"tray_now\":\"255\"}}}");
    assert(!g_bambu_state.vt_tray.active && !g_bambu_state.units[1].slots[0].active);
    APPLY("{\"print\":{\"ams\":{\"tray_now\":\"254\"}}}");
    assert(g_bambu_state.vt_tray.active);
    bambu_state_copy(&view);
    bambu_state_project(&view, 1);
    assert(view.ams_count == 1 && !strcmp(view.trays[0].type,"PA-CF"));
    assert(bambu_state_fresh(&view, now + BAMBU_DATA_STALE_MS));
    assert(!bambu_state_fresh(&view, now + BAMBU_DATA_STALE_MS + 1));
    APPLY("{\"print\":{\"ams\":{\"ams_exist_bits\":\"10000\"}}}");
    assert(g_bambu_state.unit_count == 1 && g_bambu_state.units[0].id == 128);
    APPLY("{\"print\":{\"ams\":{\"ams_exist_bits\":\"0\",\"ams\":[]}}}");
    assert(g_bambu_state.unit_count == 0 && g_bambu_state.ams_count == 0);
    APPLY("{\"print\":{\"ams\":{\"ams\":[{\"id\":0,\"tray\":[{\"id\":0,\"tray_type\":\"PLA\",\"tray_color\":\"00000000\"}]}]}}}");
    assert(g_bambu_state.units[0].slots[0].translucent);
    APPLY("{\"print\":{\"ams\":{\"ams\":[{\"id\":0,\"tray\":[{\"id\":0,\"tray_type\":\"\"}]}]}}}");
    assert(!g_bambu_state.units[0].slots[0].type[0] && g_bambu_state.units[0].slots[0].remain == -1);
    APPLY("{\"print\":{\"ams\":{\"ams\":[{\"id\":0,\"tray\":[{\"id\":2,\"tray_type\":\"PLA\"}]}]}}}");
    APPLY("{\"print\":{\"ams\":{\"tray_exist_bits\":\"b\"}}}");
    assert(!g_bambu_state.units[0].slots[2].type[0]);
    APPLY("{\"print\":{\"ams\":{\"ams\":[{\"id\":0,\"tray\":[{\"id\":0,\"tray_type\":\"PLA\"}]}]}}}");
    APPLY("{\"print\":{\"ams\":{\"tray_exist_bits\":\"0\"}}}");
    assert(!g_bambu_state.units[0].slots[0].type[0]);
    uint32_t last = g_bambu_state.last_update_ms;
    IGNORE("{\"print\":{\"unrelated\":\"escaped \\\" { [ \\u4e2d\"}}");
    assert(g_bambu_state.last_update_ms == last);
    bambu_state_begin_connection();
    assert(!g_bambu_state.data_valid && !g_bambu_state.valid_mask && !g_bambu_state.unit_count);
    for (int i = 0; i < BAMBU_MAX_AMS_UNITS + 1; ++i) {
        char report[160];
        snprintf(report, sizeof(report), "{\"print\":{\"ams\":{\"ams\":[{\"id\":%d,\"tray\":[{\"id\":0}]}]}}}", i);
        APPLY(report);
    }
    assert(g_bambu_state.unit_count == BAMBU_MAX_AMS_UNITS && g_bambu_state.ams_overflow);
    assert(bambu_pages_count(&g_bambu_state) == BAMBU_MAX_AMS_UNITS + 1);
    bambu_state_init();
    FILE *f = fopen(argv[1], "rb"); assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *data = calloc(n+1,1); assert(data); assert(fread(data,1,n,f) == (size_t)n); fclose(f);
    assert(bambu_report_apply(data, n, ++now)); free(data);
    assert(g_bambu_state.nozzle_temp == 245 && g_bambu_state.bed_temp == 70);
    assert(g_bambu_state.unit_count == 1 && g_bambu_state.external_present);
    printf("PASS: report validation, scalar types, fan scales, multi-AMS/HT identity, deltas, removal, external spool, color, freshness, repository sample\n");
    printf("state bytes: %zu; sample fans: %d/%d/%d %%\n", sizeof(bambu_state_t), g_bambu_state.cooling_fan,g_bambu_state.big_fan1,g_bambu_state.big_fan2);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf', default=os.environ.get('IDF_PATH'))
    parser.add_argument('--cc', default=shutil.which('gcc') or shutil.which('clang'))
    args = parser.parse_args()
    if not args.idf or not args.cc:
        parser.error('Set --idf/IDF_PATH and install GCC or Clang.')
    cjson = Path(args.idf) / 'components/json/cJSON'
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='bambu-report-test-') as folder:
        source = Path(folder) / 'test.c'
        source.write_text(PROBE, encoding='utf-8')
        binary = Path(folder) / ('test.exe' if os.name == 'nt' else 'test')
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                   str(source), str(ROOT / 'main/bambu/bambu_report.c'), str(ROOT / 'main/bambu/bambu_state.c'), str(ROOT / 'main/bambu/bambu_pages.c'), str(ROOT / 'main/bambu/pairing_probe.c'),
                   str(cjson / 'cJSON.c'), '-I' + str(ROOT / 'main'), *['-I' + str(ROOT / 'main' / d) for d in ('config', 'bambu', 'network', 'device', 'ui')], '-I' + str(cjson), '-lm', '-o', str(binary)]
        subprocess.run(command, check=True, env=env)
        subprocess.run([str(binary), str(ROOT / 'docs/Topic-device.json')], check=True, env=env)


if __name__ == '__main__':
    main()

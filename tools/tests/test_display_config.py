"""Exercise the real display-config setter with injected NVS failures."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ERRORS = '''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_NVS_NO_FREE_PAGES -3
#define ESP_ERR_NVS_NEW_VERSION_FOUND -4
#define ESP_ERR_NVS_NOT_FOUND -5
'''
NVS = '''#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
'''
PROBE = r'''
#include "app_config.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int failure;
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) { return ESP_FAIL; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *out, size_t len) { return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    (void)name; (void)mode; *out = 1; return failure == 1 ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out) {
    (void)h;
    if (!strcmp(key, "bl")) *out = 70;
    else if (!strcmp(key, "soff")) *out = 7;
    else if (!strcmp(key, "style")) *out = 12;
    else if (!strcmp(key, "lang")) *out = 2;
    else return ESP_FAIL;
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len) {
    (void)h;
    const char *value = !strcmp(key, "corder") ? "" : "test";
    if (*len < strlen(value) + 1) return ESP_FAIL;
    strcpy(out, value); *len = strlen(value) + 1; return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    (void)h; (void)value;
    return failure == 2 && !strcmp(key, "bl") ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) {
    (void)h; (void)key; (void)value; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return failure == 3 ? ESP_FAIL : ESP_OK; }
int main(void) {
    assert(app_config_init() == ESP_OK);
    app_config_t original = *app_config_get();
    assert(original.backlight == 70 && original.screen_off_min == 7);
    for (failure = 1; failure <= 3; ++failure) {
        assert(app_config_set_display(90, 30) == ESP_FAIL);
        assert(!memcmp(&original, app_config_get(), sizeof(original)));
    }
    failure = 0;
    assert(app_config_set_display(90, 30) == ESP_OK);
    assert(app_config_get()->backlight == 90 && app_config_get()->screen_off_min == 30);
    assert(app_config_get()->ui_style == original.ui_style && app_config_get()->lang == original.lang);
    assert(app_config_set_display(0, 200) == ESP_OK);
    assert(app_config_get()->backlight == 10 && app_config_get()->screen_off_min == 60);
    assert(app_config_set_display(200, 0) == ESP_OK);
    assert(app_config_get()->backlight == 100 && app_config_get()->screen_off_min == 0);
    puts("PASS: real display setter keeps runtime config on NVS open/write/commit failure, publishes success, clamps bounds");
    return 0;
}
'''


def main():
    cc = shutil.which('gcc') or shutil.which('clang')
    if not cc:
        raise SystemExit('GCC or Clang required')
    env = dict(os.environ)
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env['PATH']
    with tempfile.TemporaryDirectory(prefix='bambu-config-test-') as directory:
        folder = Path(directory)
        (folder / 'esp_err.h').write_text(ERRORS)
        (folder / 'nvs.h').write_text(NVS)
        (folder / 'nvs_flash.h').write_text('#pragma once\n#include "esp_err.h"\nesp_err_t nvs_flash_init(void);\nesp_err_t nvs_flash_erase(void);\n')
        (folder / 'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n')
        source = folder / 'probe.c'
        source.write_text(PROBE)
        binary = folder / ('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([cc, '-std=c11', str(source), str(ROOT / 'main/config/app_config.c'),
                        '-I' + str(folder), '-I' + str(ROOT / 'main'), *['-I' + str(ROOT / 'main' / d) for d in ('config', 'bambu', 'network', 'device', 'ui')], '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)


if __name__ == '__main__':
    main()

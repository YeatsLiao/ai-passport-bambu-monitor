"""Run real form/config code against transactional, failure-injected NVS."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from test_display_config import ERRORS, NVS

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include "app_config.h"
#include "prov_form.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
typedef struct { char key[16]; unsigned char value[512]; size_t size; } entry;
static entry disk[24], work[24], baseline[24];
static int operation, fail_at;
static int fail(void) { return ++operation == fail_at; }
static entry *find(entry *store, const char *key, int create) {
    for (int i=0;i<24;i++) if (!strcmp(store[i].key,key)) return &store[i];
    if (create) for (int i=0;i<24;i++) if (!store[i].key[0]) { strcpy(store[i].key,key);return &store[i]; }
    return NULL;
}
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { memset(disk,0,sizeof(disk));return ESP_OK; }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) {
    if (mode && fail()) return ESP_FAIL;
    *h = mode; memcpy(work,disk,sizeof(disk));return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    entry *e=find(disk,key,0);if (!e || *size < e->size) return ESP_FAIL;
    memcpy(out,e->value,e->size);*size=e->size;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) {
    if (fail()) return ESP_FAIL;
    entry *e=find(work,key,1);CHECK(e && size<=sizeof(e->value));
    memcpy(e->value,in,size);e->size=size;return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size) {return nvs_get_blob(h,key,out,size);}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *in) {return nvs_set_blob(h,key,in,strlen(in)+1);}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out) {size_t n=1;return nvs_get_blob(h,key,out,&n);}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value) {return nvs_set_blob(h,key,&value,1);}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    if (fail()) return ESP_FAIL;
    entry *e=find(work,key,0);if(!e)return ESP_FAIL;memset(e,0,sizeof(*e));return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {if(fail())return ESP_FAIL;memcpy(disk,work,sizeof(disk));return ESP_OK;}
static void failures_off(void) {operation=0;fail_at=0;}
int main(void) {
    CHECK(app_config_init()==ESP_OK);
    app_config_t old=*app_config_get();
    strcpy(old.wifi_ssid,"OLD");strcpy(old.wifi_pass,"password");
    strcpy(old.printer_ip,"192.168.1.10");strcpy(old.printer_serial,"OLD123");strcpy(old.access_code,"12345678");
    old.backlight=70;old.screen_off_min=7;old.ui_style=12;old.lang=2;
    app_config_set_comp_order_csv(&old,"5,3,1");
    CHECK(app_config_save(&old)==ESP_OK);CHECK(app_config_init()==ESP_OK);
    old=*app_config_get();
    app_config_t next;bool force;
    CHECK(!prov_form_parse("ip=192.168.1.11",&old,&next,&force));
    CHECK(next.backlight==70 && next.screen_off_min==7 && next.comp_count==3);
    CHECK(!strcmp(next.wifi_pass,old.wifi_pass) && !strcmp(next.access_code,old.access_code));
    CHECK(prov_form_parse("ssid=NEW&pass=",&old,&next,&force));
    CHECK(prov_form_parse("serial=NEW123&code=",&old,&next,&force));
    CHECK(prov_form_parse("ip=1.2.3.256",&old,&next,&force));
    CHECK(prov_form_parse("ssid=%00hidden",&old,&next,&force));
    CHECK(prov_form_parse("ssid=%ZZ",&old,&next,&force));
    CHECK(prov_form_parse("ssid=one&ssid=two",&old,&next,&force));
    CHECK(prov_form_parse("code=123456789",&old,&next,&force));
    CHECK(!prov_form_parse("ssid=12345678901234567890123456789012&pass=newpassword&serial=NEW123&code=ABCDEFGH&force=1",&old,&next,&force));
    CHECK(force && strlen(next.wifi_ssid)==32 && next.backlight==70);
    CHECK(prov_form_parse("ssid=123456789012345678901234567890123&pass=newpassword",&old,&next,&force));
    CHECK(!prov_form_parse("ssid=NEW&pass=newpassword&serial=NEW123&code=ABCDEFGH&order=",&old,&next,&force));
    CHECK(!next.comp_count);
    memcpy(baseline,disk,sizeof(disk));
    // Every stage write/commit boundary must leave old runtime credentials intact.
    for(int i=1;i<=5;i++) {
        memcpy(disk,baseline,sizeof(disk));failures_off();CHECK(app_config_init()==ESP_OK);
        operation=0;fail_at=i;CHECK(app_config_stage(&next)!=ESP_OK);failures_off();
        CHECK(!memcmp(&old,app_config_get(),sizeof(old)));
        CHECK(app_config_init()==ESP_OK && !app_config_has_trial());
        CHECK(!memcmp(&old,app_config_get(),sizeof(old)));
    }
    memcpy(disk,baseline,sizeof(disk));failures_off();CHECK(app_config_init()==ESP_OK);
    CHECK(app_config_stage(&next)==ESP_OK);
    CHECK(!strcmp(app_config_get()->wifi_ssid,"OLD")); // Stage has no runtime side effects.
    CHECK(app_config_init()==ESP_OK && app_config_has_trial());
    CHECK(!strcmp(app_config_get()->wifi_ssid,"NEW"));
    CHECK(app_config_rollback_trial()==ESP_OK);
    CHECK(app_config_init()==ESP_OK && !app_config_has_trial());
    CHECK(!memcmp(&old,app_config_get(),sizeof(old)));
    entry pending[24];
    CHECK(app_config_stage(&next)==ESP_OK);memcpy(pending,disk,sizeof(disk));
    for(int i=1;i<=10;i++) {
        memcpy(disk,pending,sizeof(disk));failures_off();CHECK(app_config_init()==ESP_OK);
        operation=0;fail_at=i;CHECK(app_config_confirm_trial()!=ESP_OK);
        failures_off();CHECK(app_config_init()==ESP_OK && app_config_has_trial());
        CHECK(!strcmp(app_config_get()->wifi_ssid,"NEW"));
        CHECK(app_config_confirm_trial()==ESP_OK);
        CHECK(app_config_init()==ESP_OK && !app_config_has_trial());
        CHECK(!strcmp(app_config_get()->wifi_ssid,"NEW"));
    }
    memcpy(disk,baseline,sizeof(disk));failures_off();CHECK(app_config_init()==ESP_OK);
    // Confirm and reboot: display settings stay independent of the stable blob.
    CHECK(app_config_stage(&next)==ESP_OK);CHECK(app_config_init()==ESP_OK);
    CHECK(app_config_confirm_trial()==ESP_OK);
    CHECK(app_config_set_display(90,30)==ESP_OK);
    CHECK(app_config_init()==ESP_OK && !app_config_has_trial());
    CHECK(!strcmp(app_config_get()->wifi_ssid,"NEW"));
    CHECK(app_config_get()->backlight==90 && app_config_get()->screen_off_min==30);
    // A failed rollback must remain pending, ready to retry without losing the backup.
    CHECK(app_config_stage(&old)==ESP_OK);CHECK(app_config_init()==ESP_OK);
    operation=0;fail_at=2;CHECK(app_config_rollback_trial()!=ESP_OK);CHECK(app_config_has_trial());
    failures_off();CHECK(app_config_rollback_trial()==ESP_OK);
    CHECK(!strcmp(app_config_get()->wifi_ssid,"NEW"));
    puts("PASS: strict form merge, secret reuse boundaries, byte limits, trial reboot/confirm/rollback, display persistence and injected storage failures");
    return 0;
}
'''


def main():
    cc = shutil.which('gcc') or shutil.which('clang')
    if not cc:
        raise SystemExit('GCC or Clang required')
    env = dict(os.environ)
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env['PATH']
    with tempfile.TemporaryDirectory(prefix='bambu-provision-test-') as directory:
        folder = Path(directory)
        (folder / 'esp_err.h').write_text(ERRORS)
        (folder / 'nvs.h').write_text(NVS)
        (folder / 'nvs_flash.h').write_text('#include "esp_err.h"\nesp_err_t nvs_flash_init(void);\nesp_err_t nvs_flash_erase(void);\n')
        (folder / 'esp_log.h').write_text('#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n')
        (folder / 'probe.c').write_text(PROBE)
        binary = folder / ('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([cc, '-std=c11', str(folder / 'probe.c'), str(ROOT / 'main/config/app_config.c'),
                        str(ROOT / 'main/network/prov_form.c'), '-I' + str(folder), '-I' + str(ROOT / 'main'), *['-I' + str(ROOT / 'main' / d) for d in ('config', 'bambu', 'network', 'device', 'ui')],
                        '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)


if __name__ == '__main__':
    main()

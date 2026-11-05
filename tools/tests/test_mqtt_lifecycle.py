"""Exercise the actual MQTT manager with a deterministic ESP event/clock shim."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORM = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_NVS_NO_FREE_PAGES -3
#define ESP_ERR_NVS_NEW_VERSION_FOUND -4
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_AUTH_OPEN 0
#define MQTT_TRANSPORT_OVER_SSL 1
typedef int esp_event_base_t;
typedef int esp_netif_t;
typedef int wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() 0
typedef struct {struct {unsigned char ssid[32],password[64];struct {int authmode;} threshold;} sta;} wifi_config_t;
typedef struct {struct {int ip;} ip_info;} ip_event_got_ip_t;
typedef void *esp_mqtt_client_handle_t;
typedef struct {
 struct {struct {const char *hostname;int port,transport;} address;struct {bool skip_cert_common_name_check;} verification;} broker;
 struct {const char *username,*client_id;struct {const char *password;} authentication;} credentials;
 struct {bool disable_auto_reconnect;} network;
 struct {int keepalive;} session;
 struct {int size,out_size;} buffer;
} esp_mqtt_client_config_t;
typedef struct {char *data,*topic;int data_len,total_data_len,current_data_offset,topic_len,retain;} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;
enum {MQTT_EVENT_CONNECTED=10,MQTT_EVENT_SUBSCRIBED,MQTT_EVENT_DISCONNECTED,MQTT_EVENT_DATA,MQTT_EVENT_ERROR};
int64_t esp_timer_get_time(void);
esp_err_t nvs_flash_init(void);esp_err_t nvs_flash_erase(void);
esp_err_t esp_netif_init(void);esp_err_t esp_event_loop_create_default(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);void esp_netif_destroy_default_wifi(esp_netif_t *);
esp_err_t esp_wifi_init(wifi_init_config_t *);esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_mode(int);esp_err_t esp_wifi_set_config(int,wifi_config_t *);
esp_err_t esp_wifi_start(void);esp_err_t esp_wifi_stop(void);esp_err_t esp_wifi_connect(void);
typedef void (*event_fn)(void *,esp_event_base_t,int32_t,void *);
esp_err_t esp_event_handler_instance_register(int,int,event_fn,void *,void *);
esp_err_t esp_event_handler_unregister(int,int,event_fn);
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t,int,event_fn,void *);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t,const char *,int);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t,const char *,const char *,int,int,int);
'''
PROBE = r'''
#include "bambu_mqtt.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int64_t now;
static int connect_calls,init_calls,destroy_calls,publish_calls,fail_init,fail_start;
static wifi_config_t captured;
static app_config_t config={.wifi_ssid="12345678901234567890123456789012",.wifi_pass="password",.printer_ip="192.168.1.1",.printer_serial="SERIAL123",.access_code="12345678"};
const app_config_t *app_config_get(void) {return &config;}
int64_t esp_timer_get_time(void) {return now;}
esp_err_t nvs_flash_init(void){return ESP_OK;}esp_err_t nvs_flash_erase(void){return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_err_t esp_event_loop_create_default(void){return ESP_ERR_INVALID_STATE;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){static int netif;return &netif;}
void esp_netif_destroy_default_wifi(esp_netif_t *p){}
esp_err_t esp_wifi_init(wifi_init_config_t *p){return ESP_OK;}
esp_err_t esp_wifi_deinit(void){return ESP_OK;}
esp_err_t esp_wifi_set_mode(int mode){return ESP_OK;}
esp_err_t esp_wifi_set_config(int iface,wifi_config_t *cfg){captured=*cfg;return ESP_OK;}
esp_err_t esp_wifi_start(void){return ESP_OK;}
esp_err_t esp_wifi_stop(void){wifi_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);return ESP_OK;}
esp_err_t esp_wifi_connect(void){++connect_calls;return ESP_OK;}
esp_err_t esp_event_handler_instance_register(int a,int b,event_fn f,void *u,void *i){return ESP_OK;}
esp_err_t esp_event_handler_unregister(int a,int b,event_fn f){return ESP_OK;}
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *cfg){++init_calls;return fail_init?NULL:(void*)1;}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c,int id,event_fn f,void *u){return ESP_OK;}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c){return fail_start?ESP_FAIL:ESP_OK;}
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t c){return ESP_OK;}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t c){++destroy_calls;return ESP_OK;}
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t c,const char *topic,int qos){CHECK(!strcmp(topic,"device/SERIAL123/report"));return 7;}
int esp_mqtt_client_publish(esp_mqtt_client_handle_t c,const char *topic,const char *data,int len,int qos,int retain){++publish_calls;return 1;}
int main(void) {
    CHECK(bambu_mqtt_start()==ESP_OK && connect_calls==1);
    CHECK(!memcmp(captured.sta.ssid,config.wifi_ssid,32));
    CHECK(bambu_mqtt_start()==ESP_OK && connect_calls==1);
    now=25000000;bambu_mqtt_poll();CHECK(!init_calls); // No initial timeout disables the manager.
    ip_event_got_ip_t ip={0};wifi_event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);
    now+=2000000;fail_init=1;bambu_mqtt_poll();CHECK(init_calls==1);
    now+=2000000;fail_init=0;fail_start=1;bambu_mqtt_poll();CHECK(init_calls==2 && destroy_calls==1);
    now+=2000000;fail_start=0;bambu_mqtt_poll();CHECK(init_calls==3);
    esp_mqtt_event_t event={0};mqtt_event_handler(NULL,0,MQTT_EVENT_CONNECTED,&event);
    CHECK(bambu_mqtt_connected() && !bambu_mqtt_has_report());
    wifi_event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);CHECK(bambu_mqtt_connected());
    mqtt_event_handler(NULL,0,MQTT_EVENT_SUBSCRIBED,&event);CHECK(publish_calls==2);
    char data[]="{\"print\":{\"nozzle_temper\":23}}";
    event.data=data;event.data_len=event.total_data_len=strlen(data);
    event.topic="device/WRONG/report";event.topic_len=strlen(event.topic);
    mqtt_event_handler(NULL,0,MQTT_EVENT_DATA,&event);CHECK(!bambu_mqtt_has_report());
    event.topic="device/SERIAL123/report";event.topic_len=strlen(event.topic);event.retain=1;
    mqtt_event_handler(NULL,0,MQTT_EVENT_DATA,&event);CHECK(!bambu_mqtt_has_report());
    event.retain=0;mqtt_event_handler(NULL,0,MQTT_EVENT_DATA,&event);CHECK(bambu_mqtt_has_report());
    wifi_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);CHECK(!bambu_mqtt_connected());
    now+=2000000;bambu_mqtt_poll();CHECK(init_calls==3); // Existing MQTT client owns reconnect.
    int before=connect_calls;bambu_mqtt_stop();CHECK(connect_calls==before && !bambu_mqtt_connected());
    now+=2000000;bambu_mqtt_poll();CHECK(connect_calls==before);
    CHECK(bambu_mqtt_start()==ESP_OK); // Start again after provisioning reused the default event loop.
    bambu_mqtt_stop();
    puts("PASS: late DHCP, allocation/start retry, connection idempotence, topic/retained proof, disconnect, stop and restart");
}
'''


def main():
    cc = shutil.which('gcc') or shutil.which('clang')
    if not cc:
        raise SystemExit('GCC or Clang required')
    idf = Path(os.environ.get('IDF_PATH', 'D:/1.Soft/Espressif/frameworks/esp-idf-v5.5.5'))
    cjson = idf / 'components/json/cJSON'
    env = dict(os.environ)
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env['PATH']
    with tempfile.TemporaryDirectory(prefix='bambu-mqtt-test-') as directory:
        folder = Path(directory)
        (folder / 'platform.h').write_text(PLATFORM)
        for name in ['esp_err.h', 'esp_log.h', 'esp_netif.h', 'esp_wifi.h', 'esp_event.h',
                     'esp_timer.h', 'nvs_flash.h', 'mqtt_client.h']:
            (folder / name).write_text('#include "platform.h"\n')
        (folder / 'probe.c').write_text(PROBE)
        binary = folder / ('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([cc, '-std=c11', str(folder / 'probe.c'), str(ROOT / 'main/bambu/bambu_report.c'),
                        str(ROOT / 'main/bambu/bambu_state.c'), str(cjson / 'cJSON.c'),
                        '-I' + str(folder), '-I' + str(ROOT / 'main'), *['-I' + str(ROOT / 'main' / d) for d in ('config', 'bambu', 'network', 'device', 'ui')], '-I' + str(cjson),
                        '-lm', '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)


if __name__ == '__main__':
    main()

// main/bambu_mqtt.c —— 拓竹打印机 MQTT-TLS 客户端实现
//
// 连接流程:
//   1. 初始化 NVS + network stack
//   2. WiFi STA 连接 (CFG_WIFI_SSID / CFG_WIFI_PASSWORD)
//   3. MQTT-TLS 连接 (mqtts://CFG_PRINTER_IP:8883, user=bblp, pass=CFG_ACCESS_CODE)
//   4. 订阅 device/CFG_PRINTER_SERIAL/report
//   5. 收到 JSON → 解析 → 更新 g_bambu_state
//
// 内存优化:
//   - MQTT buffer 由 config.h 的 CFG_MQTT_BUFFER_SIZE 控制（默认 16KB）
//   - JSON 用 cJSON 流式解析，避免大块分配
//   - TLS 用 setInsecure 跳过证书验证（拓竹自签名证书）

#include "bambu_mqtt.h"
#include "config.h"
#include "bambu_state.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "bambu_mqtt";

// 连接状态
typedef enum {
    MQTT_ST_DISCONNECTED = 0,
    MQTT_ST_WIFI_CONNECTING,
    MQTT_ST_WIFI_CONNECTED,
    MQTT_ST_MQTT_CONNECTING,
    MQTT_ST_CONNECTED,
    MQTT_ST_FAILED,
} mqtt_conn_state_t;

static mqtt_conn_state_t s_conn_state = MQTT_ST_DISCONNECTED;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_wifi_started = false;
static char s_topic_report[96];
static char s_topic_request[96];

// ---------------------------------------------------------------------------
// WiFi 连接
// ---------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi 断开，重连...");
        s_conn_state = MQTT_ST_WIFI_CONNECTING;
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi 已连接 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_conn_state = MQTT_ST_WIFI_CONNECTED;
    }
}

static esp_err_t wifi_connect(void) {
    s_conn_state = MQTT_ST_WIFI_CONNECTING;

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    // Network stack
    ret = esp_netif_init();
    if (ret != ESP_OK) return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) return ret;

    // WiFi STA
    s_sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) return ret;

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL, NULL);
    ret |= esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) return ret;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, CFG_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CFG_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    ret |= esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ret |= esp_wifi_start();
    if (ret != ESP_OK) return ret;
    s_wifi_started = true;

    ret = esp_wifi_connect();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "WiFi 连接中...");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// MQTT 消息回调 —— JSON 解析
// ---------------------------------------------------------------------------
// 解析单个托盘对象 (AMS 料槽和外挂料槽 vt_tray 共用)
static void parse_tray_obj(cJSON *tray, bambu_ams_tray_t *t, int active_tray) {
    if (!tray || !t) return;
    // 颜色: 优先 tray_color (字符串); cols 是数组, 取首个元素作后备
    cJSON *item = cJSON_GetObjectItem(tray, "tray_color");
    if (!item || !item->valuestring) {
        cJSON *cols = cJSON_GetObjectItem(tray, "cols");
        if (cols && cJSON_IsArray(cols))
            item = cJSON_GetArrayItem(cols, 0);
    }
    if (item && item->valuestring)
        strncpy(t->color, item->valuestring, sizeof(t->color) - 1);
    item = cJSON_GetObjectItem(tray, "tray_type");
    if (item && item->valuestring)
        strncpy(t->type, item->valuestring, sizeof(t->type) - 1);
    item = cJSON_GetObjectItem(tray, "remain");
    if (item) t->remain = (float)item->valuedouble;
    item = cJSON_GetObjectItem(tray, "id");
    if (item && item->valuestring)
        t->tag = atoi(item->valuestring);   // id 是字符串 ("0"~"3")
    else if (item)
        t->tag = item->valueint;
    t->active = (t->tag == active_tray);
}

static void parse_print_json(cJSON *print_obj) {
    bambu_state_t *st = &g_bambu_state;
    cJSON *item;

    // 温度
    if ((item = cJSON_GetObjectItem(print_obj, "nozzle_temper")))
        st->nozzle_temp = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "nozzle_target_temper")))
        st->nozzle_target = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "bed_temper")))
        st->bed_temp = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "bed_target_temper")))
        st->bed_target = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "chamber_temper")))
        st->chamber_temp = (float)item->valuedouble;

    // 进度
    if ((item = cJSON_GetObjectItem(print_obj, "layer_num")))
        st->layer_num = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "total_layer_num")))
        st->total_layer = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "mc_percent")))
        st->mc_percent = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "mc_remaining_time")))
        st->mc_remaining = item->valueint;

    // 状态
    if ((item = cJSON_GetObjectItem(print_obj, "gcode_state")) && item->valuestring) {
        strncpy(st->gcode_state, item->valuestring, sizeof(st->gcode_state) - 1);
        st->gcode_state[sizeof(st->gcode_state) - 1] = '\0';

        // 映射到枚举
        if (strcmp(st->gcode_state, "IDLE") == 0) st->state = BAMBU_STATE_IDLE;
        else if (strcmp(st->gcode_state, "RUNNING") == 0) st->state = BAMBU_STATE_RUNNING;
        else if (strcmp(st->gcode_state, "PAUSE") == 0) st->state = BAMBU_STATE_PAUSE;
        else if (strcmp(st->gcode_state, "FINISH") == 0) st->state = BAMBU_STATE_FINISH;
        else if (strcmp(st->gcode_state, "FAILED") == 0) st->state = BAMBU_STATE_FAILED;
        else if (strcmp(st->gcode_state, "PREPARE") == 0) st->state = BAMBU_STATE_PREPARE;
        else st->state = BAMBU_STATE_UNKNOWN;
    }

    // 速度
    if ((item = cJSON_GetObjectItem(print_obj, "spd_lvl")))
        st->spd_lvl = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "spd_mag")))
        st->spd_mag = item->valueint;

    // 风扇
    if ((item = cJSON_GetObjectItem(print_obj, "cooling_fan_speed")))
        st->cooling_fan = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "big_fan1_speed")))
        st->big_fan1 = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "big_fan2_speed")))
        st->big_fan2 = item->valueint;

    // 文件名
    if ((item = cJSON_GetObjectItem(print_obj, "gcode_file")) && item->valuestring) {
        strncpy(st->gcode_file, item->valuestring, sizeof(st->gcode_file) - 1);
        st->gcode_file[sizeof(st->gcode_file) - 1] = '\0';
    }

    // AMS 托盘信息
    // 真实推送结构: print.ams.ams[unit].tray[] (AMS 对象内嵌套 AMS 单元数组)
    cJSON *ams = cJSON_GetObjectItem(print_obj, "ams");
    if (ams) {
        // tray_now 是字符串 ("0"~"3"), 不能直接用 valueint
        cJSON *ams_tray = cJSON_GetObjectItem(ams, "tray_now");
        if (ams_tray && ams_tray->valuestring)
            st->active_tray = atoi(ams_tray->valuestring);

        cJSON *unit_arr = cJSON_GetObjectItem(ams, "ams");
        if (unit_arr && cJSON_IsArray(unit_arr)) {
            int idx = 0;
            int max_trays = BAMBU_AMS_COUNT * BAMBU_AMS_TRAY_COUNT;
            int unit_count = cJSON_GetArraySize(unit_arr);
            for (int u = 0; u < unit_count && idx < max_trays; u++) {
                cJSON *unit = cJSON_GetArrayItem(unit_arr, u);
                if (!unit) continue;
                cJSON *tray_arr = cJSON_GetObjectItem(unit, "tray");
                if (!tray_arr || !cJSON_IsArray(tray_arr)) continue;
                int tray_count = cJSON_GetArraySize(tray_arr);
                for (int i = 0; i < tray_count && idx < max_trays; i++) {
                    cJSON *tray = cJSON_GetArrayItem(tray_arr, i);
                    if (!tray) continue;
                    bambu_ams_tray_t *t = &st->trays[idx++];
                    memset(t, 0, sizeof(*t));
                    parse_tray_obj(tray, t, st->active_tray);
                }
            }
            st->ams_count = idx;
        }

        // 外挂料槽 (Ext / 虚拟托盘, 位于 print.vt_tray)
        cJSON *vt = cJSON_GetObjectItem(print_obj, "vt_tray");
        if (vt) {
            memset(&st->vt_tray, 0, sizeof(st->vt_tray));
            parse_tray_obj(vt, &st->vt_tray, st->active_tray);
        }
    }

    st->last_update_ms = esp_timer_get_time() / 1000;
    st->data_valid = true;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    (void)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT 已连接");
            s_conn_state = MQTT_ST_CONNECTED;
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_report, 0);
            // X1 系列每次推送完整状态，不需要 pushall
            // 但首次连接可以请求一次确保拿到数据
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT 断开");
            s_conn_state = MQTT_ST_DISCONNECTED;
            break;

        case MQTT_EVENT_DATA: {
            if (event->topic_len > 0 && event->data_len > 0) {
                // 解析 JSON
                char *json_str = malloc(event->data_len + 1);
                if (!json_str) break;
                memcpy(json_str, event->data, event->data_len);
                json_str[event->data_len] = '\0';

                cJSON *root = cJSON_Parse(json_str);
                if (root) {
                    cJSON *print_obj = cJSON_GetObjectItem(root, "print");
                    if (print_obj) {
                        parse_print_json(print_obj);
                    }
                    cJSON_Delete(root);
                }
                free(json_str);
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT 错误");
            s_conn_state = MQTT_ST_FAILED;
            break;

        default:
            break;
    }
}

static esp_err_t mqtt_connect(void) {
    s_conn_state = MQTT_ST_MQTT_CONNECTING;

    // 构建 topic
    snprintf(s_topic_report, sizeof(s_topic_report),
             "device/%s/report", CFG_PRINTER_SERIAL);
    snprintf(s_topic_request, sizeof(s_topic_request),
             "device/%s/request", CFG_PRINTER_SERIAL);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname = CFG_PRINTER_IP,
                .port = CFG_MQTT_PORT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .skip_cert_common_name_check = true,
                // 拓竹用自签名证书，跳过验证（由 CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY 生效）
            },
        },
        .credentials = {
            .username = "bblp",
            .client_id = "ai-passport-monitor",
            .authentication = {
                .password = CFG_ACCESS_CODE,
            },
        },
        .network = {
            .disable_auto_reconnect = false,
        },
        .session = {
            .keepalive = 30,
        },
        .buffer = {
            .size = CFG_MQTT_BUFFER_SIZE,
            .out_size = 512,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "MQTT 客户端创建失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "MQTT 连接中 %s:%d ...", CFG_PRINTER_IP, CFG_MQTT_PORT);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
esp_err_t bambu_mqtt_start(void) {
    esp_err_t ret = wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 连接失败: %s", esp_err_to_name(ret));
        s_conn_state = MQTT_ST_FAILED;
        return ret;
    }

    // 等 WiFi 连上后再连 MQTT（最多等 20 秒）
    int wait_ms = 0;
    while (s_conn_state != MQTT_ST_WIFI_CONNECTED && wait_ms < 20000) {
        vTaskDelay(pdMS_TO_TICKS(200));
        wait_ms += 200;
    }

    if (s_conn_state != MQTT_ST_WIFI_CONNECTED) {
        ESP_LOGE(TAG, "WiFi 连接超时");
        s_conn_state = MQTT_ST_FAILED;
        return ESP_ERR_TIMEOUT;
    }

    return mqtt_connect();
}

void bambu_mqtt_stop(void) {
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }
    if (s_wifi_started) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_started = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_conn_state = MQTT_ST_DISCONNECTED;
}

bool bambu_mqtt_connected(void) {
    return s_conn_state == MQTT_ST_CONNECTED;
}

void bambu_mqtt_pushall(void) {
    if (!s_mqtt_client || s_conn_state != MQTT_ST_CONNECTED) return;

    // 构造 pushall 请求
    const char *req = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
    esp_mqtt_client_publish(s_mqtt_client, s_topic_request, req, strlen(req), 0, 0);
    ESP_LOGI(TAG, "已发送 pushall 请求");
}

const char *bambu_mqtt_status_str(void) {
    switch (s_conn_state) {
        case MQTT_ST_DISCONNECTED:  return "Disconnected";
        case MQTT_ST_WIFI_CONNECTING: return "WiFi...";
        case MQTT_ST_WIFI_CONNECTED:  return "MQTT...";
        case MQTT_ST_MQTT_CONNECTING: return "MQTT...";
        case MQTT_ST_CONNECTED:       return "Connected";
        case MQTT_ST_FAILED:          return "Failed";
        default:                      return "Unknown";
    }
}

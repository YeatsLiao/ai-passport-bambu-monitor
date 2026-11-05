// main/network/bambu_mqtt.c —— 拓竹打印机 MQTT-TLS 客户端实现
//
// 连接流程:
//   1. 初始化 NVS + network stack
//   2. WiFi STA 连接 (运行时配置 app_config: NVS 保存值或 config.h 编译期默认值)
//   3. MQTT-TLS 连接 (mqtts://打印机IP:8883, user=bblp, pass=访问码)
//   4. 订阅 device/<序列号>/report
//   5. 收到 JSON → 解析 → 更新 g_bambu_state
//      (X2D/H2D report 超过接收缓冲时分片到达, 先按 offset 拼齐再解析)
//
// 内存优化:
//   - MQTT buffer 由 config.h 的 CFG_MQTT_BUFFER_SIZE 控制（默认 16KB）,
//     超过的消息按分片重组后再解析 (X2D report 约 21KB)
//   - JSON 不建整树: 在原始文本上按字段扫描, 只对小片段用 cJSON 解码
//     (整棵 cJSON 树 >100KB, 会撑爆 ESP32-C3 的 ~30KB 可用堆)
//   - TLS 用 setInsecure 跳过证书验证（拓竹自签名证书）

#include "bambu_mqtt.h"
#include "config.h"
#include "app_config.h"
#include "bambu_state.h"
#include "bambu_report.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
static bool s_wifi_initialized, s_enabled;
static volatile bool s_have_ip, s_linked;
static volatile bool s_report_received;
static int64_t s_next_attempt;
static char s_topic_report[96];
static char s_topic_request[96];

// 分片重组缓冲: X2D/H2D 新固件的 report 较大 (含 device.ctc 等新结构块),
// 超过 MQTT 接收缓冲后 esp-mqtt 会把一条消息拆成多次 MQTT_EVENT_DATA 投递,
// 必须按 current_data_offset 拼齐后才能解析, 否则 JSON 永远解析失败 (界面全 0)
#define MQTT_RX_BUF_MAX  (96 * 1024)   // 单条 report 上限, 防异常报文撑爆内存
static char  *s_rx_buf = NULL;
static size_t s_rx_cap = 0;
static size_t s_rx_total = 0;          // 当前重组中消息的总长, 0 = 空闲
static size_t s_rx_len = 0;            // 已收字节数
static bool   s_rx_overflow = false;   // 超限消息丢弃中

// ---------------------------------------------------------------------------
// WiFi 连接
// ---------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_enabled) return;
        s_have_ip = s_linked = false;
        ESP_LOGW(TAG, "WiFi 断开，重连...");
        s_conn_state = MQTT_ST_WIFI_CONNECTING;
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi 已连接 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (!s_enabled) return;
        s_have_ip = true;
        if (!s_linked) s_conn_state = MQTT_ST_WIFI_CONNECTED;
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
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    // WiFi STA
    s_sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) return ret;
    s_wifi_initialized = true;

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL, NULL);
    ret |= esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) return ret;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    // 凭据来自运行时配置 (NVS 保存值或编译期默认值)
    const app_config_t *app_cfg = app_config_get();
    memcpy(wifi_config.sta.ssid, app_cfg->wifi_ssid, strlen(app_cfg->wifi_ssid));
    memcpy(wifi_config.sta.password, app_cfg->wifi_pass, strlen(app_cfg->wifi_pass));

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
static bool warn_ratelimit(void) {
    static int64_t s_last_ms = 0;
    static bool    s_fired = false;
    int64_t now = esp_timer_get_time() / 1000;
    if (s_fired && now - s_last_ms < 30000) return false;
    s_last_ms = now;
    s_fired = true;
    return true;
}

// 确保重组缓冲 >= need 字节 (只增不减, 复用避免打印中每秒 malloc/free)
static bool rx_buf_reserve(size_t need) {
    if (s_rx_cap >= need) return true;
    size_t cap = s_rx_cap ? s_rx_cap : 4096;
    if (need > MQTT_RX_BUF_MAX) return false;
    while (cap < need) {
        cap *= 2;
        if (cap > MQTT_RX_BUF_MAX) cap = MQTT_RX_BUF_MAX;
    }
    char *buf = realloc(s_rx_buf, cap);
    if (!buf) return false;
    s_rx_buf = buf;
    s_rx_cap = cap;
    return true;
}

// 收到一条完整 report (json 已 NUL 结尾) → 解析并更新状态
static void mqtt_process_report(char *json, int len) {
    if (bambu_report_apply(json, (size_t)len, (uint32_t)(esp_timer_get_time() / 1000)))
        s_report_received = true;
    else if (warn_ratelimit())
        ESP_LOGW(TAG, "Report ignored: malformed JSON or no supported valid fields (%d bytes)", len);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    (void)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            if (!s_enabled) break;
            s_linked = true;
            s_report_received = false;
            bambu_state_begin_connection();
            ESP_LOGI(TAG, "MQTT 已连接");
            s_conn_state = MQTT_ST_CONNECTED;
            ESP_LOGI(TAG, "订阅 %s", s_topic_report);
            esp_mqtt_client_subscribe(s_mqtt_client, s_topic_report, 0);
            // LAN 模式空闲时打印机不主动周期推送, 连上先请求一次全量状态
            break;

        case MQTT_EVENT_SUBSCRIBED:
            bambu_mqtt_pushall();
            {
                const char *req = "{\"info\":{\"sequence_id\":\"1\",\"command\":\"get_version\"}}";
                esp_mqtt_client_publish(s_mqtt_client, s_topic_request, req, strlen(req), 0, 0);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_linked = false;
            s_rx_total = s_rx_len = 0;
            s_rx_overflow = false;
            ESP_LOGW(TAG, "MQTT 断开");
            s_conn_state = MQTT_ST_DISCONNECTED;
            break;

        case MQTT_EVENT_DATA: {
            if (!event->data || event->data_len <= 0) break;
            if (event->retain) break;
            if (event->current_data_offset == 0 &&
                (!event->topic || event->topic_len != strlen(s_topic_report) ||
                 memcmp(event->topic, s_topic_report, event->topic_len))) {
                s_rx_total = s_rx_len = 0;
                break;
            }
            size_t total  = (size_t)event->total_data_len;
            size_t offset = (size_t)event->current_data_offset;
            if (total == 0) total = (size_t)event->data_len;   // 保险

            ESP_LOGD(TAG, "MQTT 数据: %d 字节 (offset=%d/total=%d)",
                     event->data_len, (int)offset, (int)total);

            if (offset == 0 && total == (size_t)event->data_len) {
                s_rx_total = s_rx_len = 0;
                s_rx_overflow = false;
                // 完整消息一次到达 → 拷入重组缓冲统一加 '\0' 解析
                if (rx_buf_reserve((size_t)event->data_len + 1)) {
                    memcpy(s_rx_buf, event->data, event->data_len);
                    s_rx_buf[event->data_len] = '\0';
                    mqtt_process_report(s_rx_buf, event->data_len);
                } else if (warn_ratelimit()) {
                    ESP_LOGW(TAG, "Report buffer unavailable (%d bytes; limit=%d)", event->data_len, MQTT_RX_BUF_MAX);
                }
                break;
            }

            // ---- 分片消息: 拼齐后再解析 ----
            if (offset == 0) {
                if (rx_buf_reserve(total + 1)) {
                    s_rx_total = total;
                    s_rx_len = 0;
                    s_rx_overflow = false;
                } else {
                    s_rx_overflow = true;
                    if (warn_ratelimit())
                        ESP_LOGW(TAG, "Report buffer unavailable (%d bytes; limit=%d)", (int)total, MQTT_RX_BUF_MAX);
                }
            }
            if (s_rx_overflow) break;
            if (s_rx_total == 0) break;                        // 起始分片已被丢弃, 忽略后续
            if (total != s_rx_total || offset != s_rx_len ||
                offset > s_rx_total || (size_t)event->data_len > s_rx_total - offset) {
                s_rx_total = s_rx_len = 0;
                break;
            } // 分片序列异常, 重置
            memcpy(s_rx_buf + s_rx_len, event->data, event->data_len);
            s_rx_len += (size_t)event->data_len;
            if (s_rx_len >= s_rx_total) {
                s_rx_buf[s_rx_len] = '\0';
                mqtt_process_report(s_rx_buf, (int)s_rx_len);
                s_rx_total = 0;
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            s_linked = false;
            ESP_LOGE(TAG, "MQTT 错误");
            s_conn_state = MQTT_ST_FAILED;
            break;

        default:
            break;
    }
}

static esp_err_t mqtt_connect(void) {
    s_conn_state = MQTT_ST_MQTT_CONNECTING;

    // 凭据来自运行时配置 (app_config 单例, 指针生命周期覆盖整个连接期)
    const app_config_t *cfg = app_config_get();

    // 构建 topic
    snprintf(s_topic_report, sizeof(s_topic_report),
             "device/%s/report", cfg->printer_serial);
    snprintf(s_topic_request, sizeof(s_topic_request),
             "device/%s/request", cfg->printer_serial);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname = cfg->printer_ip,
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
                .password = cfg->access_code,
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
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "MQTT 连接中 %s:%d ...", cfg->printer_ip, CFG_MQTT_PORT);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
esp_err_t bambu_mqtt_start(void) {
    if (s_enabled) return ESP_OK;
    s_enabled = true;
    s_have_ip = s_linked = false;
    s_next_attempt = esp_timer_get_time() + 2000000;
    esp_err_t ret = wifi_connect();
    if (ret != ESP_OK) {
        bambu_mqtt_stop();
        s_conn_state = MQTT_ST_FAILED;
    }
    return ret;
}

void bambu_mqtt_poll(void) {
    if (!s_enabled || esp_timer_get_time() < s_next_attempt) return;
    s_next_attempt = esp_timer_get_time() + 2000000;
    if (!s_have_ip) {
        if (s_wifi_started) esp_wifi_connect();
        return;
    }
    // Once created, esp-mqtt owns its normal reconnect policy.
    if (!s_mqtt_client) mqtt_connect();
}

void bambu_mqtt_stop(void) {
    s_enabled = false;
    s_have_ip = s_linked = false;
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }
    // 释放分片重组缓冲
    free(s_rx_buf);
    s_rx_buf = NULL;
    s_rx_cap = 0;
    s_rx_total = 0;
    s_rx_len = 0;
    s_rx_overflow = false;
    if (s_wifi_initialized) {
        if (s_wifi_started) esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_started = s_wifi_initialized = false;
    }
    // 注: 注销事件 handler 必须在 esp_wifi_deinit() 之后，确保不会再有任何 WiFi/IP 事件到达
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler);
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_conn_state = MQTT_ST_DISCONNECTED;
}

bool bambu_mqtt_connected(void) {
    return s_linked && s_have_ip;
}

bool bambu_mqtt_has_report(void) { return bambu_mqtt_connected() && s_report_received; }

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

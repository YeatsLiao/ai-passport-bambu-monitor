// main/network/wifi_prov.c —— 配网模式实现
//
// 组成:
//   1. SoftAP 热点 "Passport-XXXX" (开放网络, XXXX 取 MAC 后两字节避免撞名)
//   2. captive DNS: 劫持所有域名解析到 192.168.4.1 (手机连上后自动弹页)
//   3. HTTP 表单: 填写 WiFi / 打印机 IP / 序列号 / 访问码, 保存到 NVS 后重启
//
// 网页表单留空的 WiFi 密码/访问码沿用旧值 (方便只改一台打印机或换网)。

#include "wifi_prov.h"
#include "app_config.h"
#include "prov_form.h"
#include "bambu_report.h"
#include "pairing_probe.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "lwip/sockets.h"
#include "dns_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define AP_SSID_PREFIX  "Passport-"
#define AP_MAX_CONN     4

// 打印机 LAN MQTT 固定端口 (与 config.h 的 CFG_MQTT_PORT 保持一致;
// 此处独立定义, 配网层不依赖编译期 config.h)
#define PROV_MQTT_PORT  8883

static const char *TAG = "wifi_prov";

// WiFi 扫描事件 (api_scan 用): 非阻塞扫描后等 SCAN_DONE, 超时兜底防挂死
#define SCAN_DONE_BIT   BIT0
static EventGroupHandle_t s_scan_events;
static httpd_handle_t s_server;
static dns_server_handle_t s_dns;
static esp_netif_t *s_ap_netif, *s_sta_netif;
static bool s_wifi_initialized;
static volatile bool s_cancel, s_restart_pending;
static portMUX_TYPE s_prov_mux = portMUX_INITIALIZER_UNLOCKED;

static bool begin_save(void) {
    portENTER_CRITICAL(&s_prov_mux);
    bool allowed = !s_cancel && !s_restart_pending;
    if (allowed) s_restart_pending = true;
    portEXIT_CRITICAL(&s_prov_mux);
    return allowed;
}

static void scan_done_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data) {
    if (s_scan_events) xEventGroupSetBits(s_scan_events, SCAN_DONE_BIT);
}

// ---------------------------------------------------------------------------
// 配置验证: 保存前实连一遍 WiFi 和打印机, 结果反馈到配网页
// ---------------------------------------------------------------------------
// WiFi 验证结果 (事件回调填充)
static volatile struct {
    bool got_ip;
    bool disconnected;
    int  reason;      // WIFI_REASON_*
} s_wifi_verify;

static void prov_wifi_event(void *arg, esp_event_base_t base,
                            int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_verify.disconnected = true;
        s_wifi_verify.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_verify.got_ip = true;
    }
}

// 用待保存配置连 WiFi; NULL=成功, 否则返回给用户看的错误文案
static const char *verify_wifi(const app_config_t *cfg, int timeout_ms) {
    wifi_config_t sta = { 0 };
    memcpy(sta.sta.ssid, cfg->wifi_ssid, strlen(cfg->wifi_ssid));
    memcpy(sta.sta.password, cfg->wifi_pass, strlen(cfg->wifi_pass));
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) return "WiFi 配置失败";

    s_wifi_verify.got_ip = false;
    s_wifi_verify.disconnected = false;
    s_wifi_verify.reason = 0;
    if (esp_wifi_connect() != ESP_OK) return "WiFi 连接请求失败，请重试";

    for (int t = 0; t < timeout_ms / 100; t++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_cancel) return "已取消配网，原配置未更改";
        if (s_wifi_verify.got_ip) return NULL;
        if (s_wifi_verify.disconnected) {
            // esp_wifi_disconnect() above can queue its leave event after connect.
            if (s_wifi_verify.reason == WIFI_REASON_ASSOC_LEAVE) {
                s_wifi_verify.disconnected = false;
                continue;
            }
            switch (s_wifi_verify.reason) {
                case WIFI_REASON_NO_AP_FOUND:
                    return "找不到该 WiFi：名称有误、距离太远，或它是 5GHz 网络（本设备仅支持 2.4GHz）";
                case WIFI_REASON_AUTH_EXPIRE:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_ASSOC_FAIL:
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    return "WiFi 密码可能错误";
                default:
                    ESP_LOGW(TAG, "WiFi 连接失败 reason=%d", s_wifi_verify.reason);
                    return "WiFi 连接失败，请重试";
            }
        }
    }
    return "连接超时：WiFi 信号可能太弱";
}

// MQTT 验证结果 (事件回调填充)
static volatile bool s_mq_ok;
static volatile bool s_mq_fail;
static volatile int  s_mq_refuse;   // CONNACK 拒绝码 (0=无), 用于区分访问码错误
static pairing_probe_t s_probe;
static int s_sub_id;
static bool s_mq_connected;
static char s_request_topic[96];

static void prov_mqtt_event(void *h, esp_event_base_t base,
                            int32_t id, void *event_data) {
    (void)h; (void)base; (void)id;
    esp_mqtt_event_handle_t e = event_data;
    switch (e->event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mq_connected = true;
            s_sub_id = esp_mqtt_client_subscribe(e->client, s_probe.topic, 0);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            if (s_sub_id >= 0 && e->msg_id == s_sub_id) {
                const char *request = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
                s_probe.requested = true;
                if (esp_mqtt_client_publish(e->client, s_request_topic, request, 0, 0, 0) < 0)
                    s_probe.requested = false;
            }
            break;
        case MQTT_EVENT_DATA:
            if (e->data_len > 0 && e->total_data_len > 0 && e->current_data_offset >= 0 &&
                pairing_probe_data(&s_probe, e->topic, e->topic_len, e->data,
                                   e->data_len, e->total_data_len, e->current_data_offset, e->retain))
                s_mq_ok = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mq_fail = true;
            break;
        case MQTT_EVENT_ERROR:
            s_mq_fail = true;
            if (e->error_handle &&
                e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
                s_mq_refuse = e->error_handle->connect_return_code;
            break;
        default: break;
    }
}

// 用待保存配置连打印机 MQTT (TLS + 访问码); NULL=成功, 否则错误文案
static const char *verify_mqtt(const app_config_t *cfg, int timeout_ms) {
    esp_mqtt_client_config_t m = {
        .broker = {
            .address = {
                .hostname = cfg->printer_ip,
                .port = PROV_MQTT_PORT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = { .skip_cert_common_name_check = true },
        },
        .credentials = {
            .username = "bblp",
            .client_id = "prov-verify",
            .authentication = { .password = cfg->access_code },
        },
        // Reassemble larger reports separately; do not allocate a full JSON tree.
        .buffer = { .size = 1024, .out_size = 256 },
        .network = { .disable_auto_reconnect = true, .timeout_ms = 8000 },
    };

    s_mq_ok = false; s_mq_fail = false; s_mq_refuse = 0;
    s_mq_connected = false; s_sub_id = -1;
    pairing_probe_init(&s_probe, cfg->printer_serial);
    snprintf(s_request_topic, sizeof(s_request_topic), "device/%s/request", cfg->printer_serial);
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&m);
    if (!c) return "打印机连接失败：内存不足";
    esp_mqtt_client_register_event(c, ESP_EVENT_ANY_ID, prov_mqtt_event, NULL);
    if (esp_mqtt_client_start(c) != ESP_OK) {
        esp_mqtt_client_destroy(c);
        return "打印机验证启动失败";
    }

    for (int t = 0; t < timeout_ms / 100; t++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_cancel || s_mq_ok || s_mq_fail) break;
    }
    esp_mqtt_client_stop(c);    // 未连上时 stop 返回错误, 忽略
    esp_mqtt_client_destroy(c);
    pairing_probe_free(&s_probe);
    if (s_cancel) return "已取消配网，原配置未更改";
    if (s_mq_ok) return NULL;

    if (s_mq_refuse == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
        s_mq_refuse == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED)
        return "打印机拒绝了访问码：请核对 8 位访问码（打印机 设置→网络）";
    if (s_mq_connected) return "已登录，但未收到该序列号的有效数据：请核对序列号、局域网模式和固件权限";
    return "连不上打印机：请检查 IP 与访问码，确认打印机已开机且在 设置→网络 开启了「仅局域网模式」";
}

// 运行时显示偏好取值范围 (与 config.example.h 的 STYLE_*/LANG_* 一致,
// 此处独立定义避免配网层依赖编译期 config.h)
#define PROV_STYLE_MIN  1     // STYLE_BAMBU
#define PROV_STYLE_MAX  12    // STYLE_APPLE
#define PROV_LANG_EN    1     // LANG_EN
#define PROV_LANG_CN    2     // LANG_CN

static char s_ap_ssid[32] = AP_SSID_PREFIX;
static bool s_prov_running = false;

// DNS 使用官方 dns_server 组件 (components/dns_server/),
// 所有 A 查询重定向到 SoftAP IP (192.168.4.1)

// ---------------------------------------------------------------------------
// 表单解析辅助
// ---------------------------------------------------------------------------
// HTML 属性值转义 (& " < >)
static void html_escape(const char *in, char *out, size_t outlen) {
    size_t o = 0;
    for (const char *p = in; *p && o + 7 < outlen; p++) {
        if (*p == '&')      o += snprintf(out + o, outlen - o, "&amp;");
        else if (*p == '"') o += snprintf(out + o, outlen - o, "&quot;");
        else if (*p == '<') o += snprintf(out + o, outlen - o, "&lt;");
        else if (*p == '>') o += snprintf(out + o, outlen - o, "&gt;");
        else out[o++] = *p;
    }
    out[o] = '\0';
}

// 基本 IPv4 格式校验: X.X.X.X, 每个 X 1-3 位, 0-255


// 配置保存后延迟重启: 使用 esp_timer 比 xTaskCreate 更可靠 (后者在内存耗尽时会失败)
static void restart_cb(void *arg) { (void)arg; esp_restart(); }
static const esp_timer_create_args_t s_restart_cfg = {
    .callback = restart_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "prov_restart"
};

static void schedule_restart(void) {
    esp_timer_handle_t t = NULL;
    if (esp_timer_create(&s_restart_cfg, &t) == ESP_OK && t)
        esp_timer_start_once(t, 2 * 1000000); // 2 秒后重启
    else
        esp_restart(); // 内存不足时的兜底: 立即重启
}

static const char PAGE_OK[] =
"<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>配置已保存</title>"
"<style>:root{--bg:#f4f5f7;--card:#fff;--ink:#111826;--sub:#5b6572;--line:#e6e8ec;--ok:#15803d;--okbg:#f0fdf4}"
"*{box-sizing:border-box;margin:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px;-webkit-font-smoothing:antialiased}"
".card{max-width:420px;width:100%;background:var(--card);border:1px solid var(--line);border-radius:18px;padding:32px 26px;text-align:center;box-shadow:0 1px 2px rgba(16,24,40,.04),0 12px 28px rgba(16,24,40,.06)}"
".mark{width:56px;height:56px;border-radius:16px;background:var(--okbg);display:flex;align-items:center;justify-content:center;margin:0 auto 18px}"
"h1{font-size:19px;font-weight:650;letter-spacing:-.01em;margin-bottom:12px}"
".ok{background:var(--okbg);border:1px solid #bbf7d0;border-radius:12px;padding:16px;color:var(--ok);font-size:15px;line-height:1.7;text-align:left}"
".ok b{font-size:16px}.hint{margin-top:16px;font-size:13px;color:var(--sub);line-height:1.6}</style></head><body>"
"<div class=\"card\"><div class=\"mark\"><svg viewBox=\"0 0 24 24\" width=\"28\" height=\"28\" fill=\"none\" stroke=\"#15803d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M8 12.5l2.5 2.5 5-5.5\"/></svg></div>"
"<h1>拓竹打印机监控器</h1>"
"<div class=\"ok\"><b>配置已进入试运行</b><br>设备正在重启。90 秒内未收到有效打印数据，将恢复原配置。</div>"
"<div class=\"hint\">可从设备设置中的「重新配网」再次进入本页面。</div>"
"</div></body></html>";

static const char PAGE_ERR[] =
"<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>保存失败</title>"
"<style>:root{--bg:#f4f5f7;--card:#fff;--ink:#111826;--line:#e6e8ec;--accent:#2f6fed;--err:#b42318;--errbg:#fef3f2}"
"*{box-sizing:border-box;margin:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px;-webkit-font-smoothing:antialiased}"
".card{max-width:420px;width:100%%;background:var(--card);border:1px solid var(--line);border-radius:18px;padding:30px 26px;box-shadow:0 1px 2px rgba(16,24,40,.04),0 12px 28px rgba(16,24,40,.06)}"
".mark{width:52px;height:52px;border-radius:15px;background:var(--errbg);display:flex;align-items:center;justify-content:center;margin:0 auto 16px}"
".err{background:var(--errbg);border:1px solid #fecdca;border-radius:12px;padding:16px;color:var(--err);font-size:15px;line-height:1.6}"
".err b{display:block;font-size:16px;margin-bottom:4px}"
"a{display:flex;align-items:center;justify-content:center;min-height:48px;margin-top:18px;border-radius:12px;background:var(--accent);color:#fff;text-decoration:none;font-size:15px;font-weight:600}</style></head><body>"
"<div class=\"card\"><div class=\"mark\"><svg viewBox=\"0 0 24 24\" width=\"26\" height=\"26\" fill=\"none\" stroke=\"#b42318\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M12 7.5v6\"/><path d=\"M12 16.5v.01\"/></svg></div>"
"<div class=\"err\"><b>保存失败</b>%s</div>"
"<a href=\"/\">返回重新填写</a></div></body></html>";

// 配页静态存储: httpd 单线程顺序处理, 用 static 避免大数组占用栈内存
// (含风格/语言下拉框 + 组件排序列表后页面 ~9KB, 各参数取最坏长度时 ~12.6KB, 预留 16KB 消除截断警告)
static char s_page[16384];

// http_404: 重定向到绝对 IP, 确保手机 captive portal WebView 能准确找到配置页
// 注意: iOS 要求响应体必须有内容, 否则不识别为 captive portal
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// 保存结果响应: JS fetch (?api=1) 返回 JSON; 无 JS 的传统表单提交返回 HTML 页
static esp_err_t save_fail_response(httpd_req_t *req, bool api_mode,
                                    const char *stage, const char *err) {
    if (api_mode) {
        char jr[320];
        snprintf(jr, sizeof(jr),
                 "{\"ok\":false,\"stage\":\"%s\",\"err\":\"%s\"}", stage, err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, jr, HTTPD_RESP_USE_STRLEN);
    } else {
        snprintf(s_page, sizeof(s_page), PAGE_ERR, err);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, s_page, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req) {
    char body[1536] = "";
    int len = req->content_len, used = 0;
    if (len <= 0 || len >= (int)sizeof(body)) goto bad_request;
    while (used < len) {
        int r = httpd_req_recv(req, body + used, len - used);
        if (r <= 0 || s_cancel) goto bad_request;
        used += r;
    }
    body[used] = 0;
    char q[16] = "";
    bool api_mode = httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
                    strstr(q, "api=1") != NULL;
    if (s_cancel || s_restart_pending)
        return save_fail_response(req, api_mode, "cancel", "Provisioning closed");
    app_config_t cfg;
    bool force;
    const char *err = prov_form_parse(body, app_config_get(), &cfg, &force);
    if (err) return save_fail_response(req, api_mode, "validate", err);

    // ---- 保存前实连验证: WiFi → 打印机 MQTT, 结果反馈到页面 ----
    const char *wifi_err = NULL, *mqtt_err = NULL;
    if (!force) {
        ESP_LOGI(TAG, "验证配置: WiFi \"%s\" → 打印机 %s ...", cfg.wifi_ssid, cfg.printer_ip);
        wifi_err = verify_wifi(&cfg, 15000);
        if (!wifi_err) mqtt_err = verify_mqtt(&cfg, 20000);
        esp_wifi_disconnect();   // 释放 STA, 用户修改后可重试
    }
    if (wifi_err || mqtt_err) {
        return save_fail_response(req, api_mode,
                                  wifi_err ? "wifi" : "mqtt",
                                  wifi_err ? wifi_err : mqtt_err);
    }

    if (!begin_save()) {
        return save_fail_response(req, api_mode, "cancel", "Cancelled; previous configuration kept");
    }
    esp_err_t ret = app_config_stage(&cfg);
    if (ret != ESP_OK) {
        s_restart_pending = false;
        return save_fail_response(req, api_mode, "validate", "写入存储失败");
    }

    ESP_LOGI(TAG, "配置已保存%s, 2 秒后重启", force ? "(跳过验证)" : " (验证通过)");
    if (api_mode) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, force ? "{\"ok\":true,\"verified\":false}" : "{\"ok\":true,\"verified\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, PAGE_OK, HTTPD_RESP_USE_STRLEN);
    }
    schedule_restart();
    return ESP_OK;

bad_request:
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// WiFi 扫描接口 (GET /api/scan)
// SoftAP 共存 (APSTA) 下扫描周围网络, 返回 JSON. 用户可从列表选择, 防止手输 SSID 错误.
// 扫描本身要 1-3 秒; 非阻塞 + 事件等待 + 超时, 重复点击或扫描未结束时自动重试.
// ---------------------------------------------------------------------------
static esp_err_t api_scan(httpd_req_t *req) {
    static wifi_ap_record_t recs[12];
    static char json[1024];

    // SoftAP 共存时每信道多留驻留时间, 提高弱信号/慢应答 AP 的命中率
    wifi_scan_config_t sc = {
        .show_hidden = false,
        .scan_time = { .active = { .min = 100, .max = 300 } },
    };

    bool ok = false;
    if (!s_scan_events) {   // 事件组创建失败 (内存耗尽): 返回空列表, 避免崩溃
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    for (int tries = 0; tries < 3 && !ok && !s_cancel; tries++) {
        xEventGroupClearBits(s_scan_events, SCAN_DONE_BIT);
        if (esp_wifi_scan_start(&sc, false) == ESP_OK) {
            // 10s 超时兜底: 阻塞式扫描丢 SCAN_DONE 事件会永久挂死 httpd 任务
            EventBits_t bits = 0;
            for (int wait = 0; wait < 100 && !s_cancel && !bits; ++wait)
                bits = xEventGroupWaitBits(s_scan_events, SCAN_DONE_BIT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
            ok = (bits & SCAN_DONE_BIT) != 0;
            if (!ok) ESP_LOGW(TAG, "扫描超时 (第 %d 次)", tries + 1);
        } else {
            // 上次扫描未结束 / radio 忙: 稍候重试
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    httpd_resp_set_type(req, "application/json");
    if (!ok) {
        httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    uint16_t n = 12;
    esp_wifi_scan_get_ap_records(&n, recs);

    size_t pos = 0;
    json[pos++] = '[';
    for (int i = 0; i < (int)n && pos < sizeof(json) - 80; i++) {
        // 跳过空 SSID 和自己的热点
        if (recs[i].ssid[0] == '\0') continue;
        if (strncmp((const char *)recs[i].ssid, AP_SSID_PREFIX, sizeof(AP_SSID_PREFIX) - 1) == 0) continue;
        if (pos > 1) json[pos++] = ',';
        // JSON 字符串转义: 过滤控制字符，并转义双引号和反斜杠
        char esc[40] = {};
        size_t ei = 0;
        for (const uint8_t *p = recs[i].ssid; *p && ei < 38; p++) {
            if (*p < 0x20) continue;
            if (*p == '"' || *p == '\\') esc[ei++] = '\\';
            esc[ei++] = (char)*p;
        }
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"s\":\"%s\",\"r\":%d,\"e\":%s}",
            esc, (int)recs[i].rssi,
            recs[i].authmode != WIFI_AUTH_OPEN ? "true" : "false");
    }
    json[pos++] = ']';
    json[pos] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t root_get(httpd_req_t *req) {
    const app_config_t *cfg = app_config_get();
    // esc_pass / esc_code 故意不用: 密码和访问码不回显到 HTML (安全设计)
    // esc_ssid 需要 200 字节: 32 字节 SSID 如果全是引号, html_escape 后最长 32*6=192 字节
    char esc_ssid[200], esc_ip[64], esc_ser[120];
    html_escape(cfg->wifi_ssid, esc_ssid, sizeof(esc_ssid));
    html_escape(cfg->printer_ip, esc_ip, sizeof(esc_ip));
    html_escape(cfg->printer_serial, esc_ser, sizeof(esc_ser));

    // WiFi 密码/访问码只在已配置时作为占位提示, 不回显明文
    bool have_pass = cfg->wifi_pass[0] != '\0';
    bool have_code = cfg->access_code[0] != '\0';

    // 风格/语言下拉框选项: 预置当前保存值为 selected。
    // 名称与 ui_theme_style_name() 一致; static 避免压 httpd 栈。
    static const char *const style_names[PROV_STYLE_MAX] = {
        "Bambu", "Cyber", "Sheikah", "White", "Industrial", "Neon",
        "Pixel", "SSD", "F1", "Gauge", "Geist", "Apple"
    };
    static char style_opts[768];
    int soff = 0;
    for (int i = 0; i < PROV_STYLE_MAX; i++) {
        if (soff >= (int)sizeof(style_opts) - 1) break;
        int w = snprintf(style_opts + soff, sizeof(style_opts) - soff,
                         "<option value=\"%d\"%s>%d \u00b7 %s</option>",
                         i + 1, cfg->ui_style == (uint8_t)(i + 1) ? " selected" : "",
                         i + 1, style_names[i]);
        if (w < 0) break;
        soff += w;
        if (soff >= (int)sizeof(style_opts)) soff = (int)sizeof(style_opts) - 1;
    }
    static char lang_opts[128];
    snprintf(lang_opts, sizeof(lang_opts),
             "<option value=\"%d\"%s>English</option>"
             "<option value=\"%d\"%s>\u4e2d\u6587</option>",
             PROV_LANG_EN, cfg->lang == PROV_LANG_EN ? " selected" : "",
             PROV_LANG_CN, cfg->lang == PROV_LANG_CN ? " selected" : "");

    // 组件排序列表: 依当前配置预排 DOM 顺序与勾选状态。
    // comp_count>0 展示用户已选(有序勾选)+未选(附尾未勾); =0 走默认(常见 6 项勾选)。
    static const char *const comp_names[APP_COMP_MAX + 1] = {
        "", "喷嘴温度", "热床温度", "腔体温度", "层数", "进度 %",
        "剩余时间", "打印状态", "打印速度", "AMS"
    };
    bool shown[APP_COMP_MAX + 1] = { false };
    int seq[APP_COMP_MAX]; int seqn = 0;
    bool use_default = (cfg->comp_count == 0);
    if (!use_default) {
        for (int i = 0; i < cfg->comp_count && i < APP_COMP_MAX; i++) {
            int id = cfg->comp_order[i];
            if (id >= 1 && id <= APP_COMP_MAX && !shown[id]) { seq[seqn++] = id; shown[id] = true; }
        }
        for (int id = 1; id <= APP_COMP_MAX; id++) if (!shown[id]) seq[seqn++] = id;
    } else {
        static const int dflt[6] = {5, 4, 1, 2, 6, 8};
        for (int i = 0; i < 6; i++) { seq[seqn++] = dflt[i]; shown[dflt[i]] = true; }
        for (int id = 1; id <= APP_COMP_MAX; id++) if (!shown[id]) seq[seqn++] = id;
    }
    // 每行 HTML ~310 字节, 9 行需 ~2800 字节, 预留 3200 防截断
    static char order_rows[3200];
    int ooff = 0;
    for (int i = 0; i < seqn; i++) {
        int id = seq[i];
        if (ooff >= (int)sizeof(order_rows) - 1) break;
        int w = snprintf(order_rows + ooff, sizeof(order_rows) - ooff,
            "<div class=\"orow\" data-cmp=\"%d\"><label class=\"olab\">"
            "<input type=\"checkbox\" class=\"ochk\"%s><span class=\"oidx\">%d</span>"
            "<span class=\"oname\">%s</span></label>"
            "<span class=\"omv\"><button type=\"button\" onclick=\"mvUp(this)\">\u2191</button>"
            "<button type=\"button\" onclick=\"mvDn(this)\">\u2193</button></span></div>",
            id, shown[id] ? " checked" : "", id, comp_names[id]);
        if (w < 0) break;
        ooff += w;
        if (ooff >= (int)sizeof(order_rows)) { ooff = (int)sizeof(order_rows) - 1; break; }
    }

    snprintf(s_page, sizeof(s_page),
"<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
"<title>拓竹打印机监控器 · 配置</title>"
"<style>:root{--bg:#f4f5f7;--card:#fff;--ink:#111826;--label:#3c4654;--sub:#5b6572;--line:#e6e8ec;--field:#f7f8fa;--field-line:#dfe3e8;--accent:#2f6fed;--accent-ink:#eef3ff;--danger:#b42318;--radius:12px}"
"*{box-sizing:border-box;margin:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh;padding:28px 16px;-webkit-font-smoothing:antialiased}"
".card{max-width:430px;margin:0 auto;background:var(--card);border:1px solid var(--line);border-radius:20px;padding:26px 22px;box-shadow:0 1px 2px rgba(16,24,40,.04),0 12px 28px rgba(16,24,40,.06)}"
".head{display:flex;align-items:center;gap:14px}"
".logo{width:46px;height:46px;border-radius:13px;background:var(--accent-ink);display:flex;align-items:center;justify-content:center;flex-shrink:0}"
"h1{font-size:19px;font-weight:650;letter-spacing:-.01em}"
"p.tip{font-size:13px;color:var(--sub);line-height:1.6;margin-top:4px}"
".sec{margin-top:26px;font-size:12px;font-weight:700;color:var(--accent);letter-spacing:.05em;display:flex;align-items:center;gap:10px}"
".sec:after{content:\"\";flex:1;height:1px;background:var(--line)}"
"label{display:block;font-size:13px;color:var(--label);margin:14px 0 6px;font-weight:600}"
"label em{font-style:normal;color:var(--danger)}"
"input:not([type=checkbox]):not([type=hidden]),select{width:100%%;padding:12px 14px;min-height:46px;border:1.5px solid var(--field-line);border-radius:var(--radius);font-size:16px;background:var(--field);color:var(--ink);-webkit-appearance:none;appearance:none;transition:border-color .15s,box-shadow .15s}"
"input:focus,select:focus{outline:none;border-color:var(--accent);background:#fff;box-shadow:0 0 0 3px rgba(47,111,237,.15)}"
".selwrap{position:relative}"
".selwrap select{padding-right:38px}"
".selwrap:after{content:\"\";position:absolute;right:16px;top:50%%;width:9px;height:9px;border-right:2px solid var(--sub);border-bottom:2px solid var(--sub);transform:translateY(-70%%) rotate(45deg);pointer-events:none}"
".netsel{margin-top:8px}"
".btnline{display:flex;gap:8px}"
".scanbtn{flex-shrink:0;width:auto;min-height:46px;margin:0;padding:0 16px;background:var(--field);border:1.5px solid var(--field-line);border-radius:var(--radius);color:var(--label);font-size:15px;font-weight:600;cursor:pointer;-webkit-appearance:none;appearance:none}"
".scanbtn:active{background:#e9edf2}"
".olist{border:1.5px solid var(--field-line);border-radius:var(--radius);overflow:hidden;margin-top:4px;transition:opacity .2s}"
".olist.off{opacity:.4;pointer-events:none}"
".orow{display:flex;align-items:center;justify-content:space-between;min-height:52px;padding:10px 12px;border-bottom:1px solid var(--line);background:#fff}"
".orow:last-child{border-bottom:0}"
".olab{display:flex;align-items:center;gap:11px;margin:0;font-size:15px;color:var(--ink);font-weight:500;cursor:pointer}"
".oidx{display:inline-flex;width:24px;height:24px;border-radius:7px;background:#eef1f4;color:#48525f;font-size:12px;align-items:center;justify-content:center;font-weight:700}"
".ochk{width:20px;height:20px;min-height:0;flex-shrink:0;margin:0;padding:0;border:0;accent-color:var(--accent)}"
".omv{display:flex;gap:6px}"
".omv button{width:40px;height:40px;min-width:40px;margin:0;padding:0;border:1px solid var(--field-line);border-radius:9px;background:var(--field);color:var(--label);font-size:16px;line-height:1;cursor:pointer;-webkit-appearance:none;appearance:none}"
".omv button:active{background:#e9edf2}"
".odflt{display:flex;align-items:center;gap:9px;font-size:13px;color:var(--label);font-weight:600;cursor:pointer}"
".odflt input{width:18px;height:18px;min-height:0;margin:0;padding:0;border:0;accent-color:var(--accent)}"
".result{display:none;margin-top:18px;border-radius:12px;padding:14px 16px;font-size:14px;line-height:1.7}"
".result.wait{display:block;background:#eef3ff;color:#2c4a9e}"
".result.okr{display:block;background:#f0fdf4;color:#15803d;border:1px solid #bbf7d0}"
".result.errr{display:block;background:#fef3f2;color:#b42318;border:1px solid #fecdca}"
".rbtns{display:flex;gap:10px;margin-top:12px}"
".rbtns button{flex:1;min-height:42px;margin:0;border-radius:10px;font-size:14px;font-weight:600;border:1.5px solid transparent;cursor:pointer;-webkit-appearance:none;appearance:none}"
".fbtn{background:var(--accent);color:#fff}"
".rbtn{background:#fff;color:var(--label);border-color:var(--field-line)}"
"button.submit{width:100%%;min-height:50px;margin-top:28px;padding:14px;border:0;border-radius:14px;background:var(--accent);color:#fff;font-size:16px;font-weight:650;cursor:pointer;transition:filter .15s}"
"button.submit:active{filter:brightness(.94)}"
".foot{max-width:430px;margin:16px auto 0;text-align:center;font-size:12.5px;color:var(--sub);line-height:1.6}</style>"
"</head><body>"
"<div class=\"card\">"
"<div class=\"head\"><div class=\"logo\"><svg viewBox=\"0 0 24 24\" width=\"24\" height=\"24\" fill=\"none\" stroke=\"#2f6fed\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M6 9V4h12v5\"/><rect x=\"4\" y=\"9\" width=\"16\" height=\"7\" rx=\"1.6\"/><path d=\"M7 16h10v4H7z\"/></svg></div><div><h1>拓竹打印机监控器</h1>"
"<p class=\"tip\">保存前会自动验证 WiFi 和打印机连接，失败会提示原因</p></div></div>"
"<form method=\"POST\" action=\"/save\" onsubmit=\"return submitForm(event)\">"
"<div class=\"sec\">WIFI · 仅支持 2.4GHz</div>"
"<label>WiFi 名称 <em>*</em></label>"
"<div class=\"btnline\">"
"<input id=\"ssid\" name=\"ssid\" required maxlength=\"32\" value=\"%s\" placeholder=\"输入或点击扫描\" autocomplete=\"off\" style=\"flex:1\">"
"<button type=\"button\" class=\"scanbtn\" id=\"scanbtn\" onclick=\"scanWifi()\">扫描</button>"
"</div>"
"<select id=\"netsel\" class=\"netsel\" onchange=\"pickNet()\" style=\"display:none\"></select>"
"<label>WiFi 密码%s</label>"
"<input name=\"pass\" type=\"password\" maxlength=\"64\" placeholder=\"%s\">"
"<div class=\"sec\">打印机 · 需开启仅局域网模式</div>"
"<label>打印机 IP <em>*</em></label>"
"<input name=\"ip\" required maxlength=\"15\" value=\"%s\" placeholder=\"如 192.168.1.105\" inputmode=\"decimal\" pattern=\"([0-9]{1,3}\\.){3}[0-9]{1,3}\" title=\"输入如 192.168.1.105\" autocomplete=\"off\">"
"<label>序列号（15 位，打印机 设置→设备） <em>*</em></label>"
"<input name=\"serial\" required maxlength=\"30\" value=\"%s\" placeholder=\"打印机序列号\" autocomplete=\"off\">"
"<label>访问码（8 位，打印机 设置→网络）%s</label>"
"<input name=\"code\" maxlength=\"8\" %splaceholder=\"%s\" autocomplete=\"off\">"
"<div class=\"sec\">显示设置 · 重启后生效</div>"
"<label>界面风格</label>"
"<div class=\"selwrap\"><select name=\"style\">%s</select></div>"
"<label>界面语言 / Language</label>"
"<div class=\"selwrap\"><select name=\"lang\">%s</select></div>"
"<label style=\"margin-top:16px\">组件排序（勾选显示 · 上移/下移调序）</label>"
"<label class=\"odflt\" style=\"margin:6px 0\"><input type=\"checkbox\" id=\"usedefault\" onchange=\"toggleDefault()\"%s> 使用各风格推荐默认顺序</label>"
"<div id=\"orderlist\" class=\"olist%s\">%s</div>"
"<input type=\"hidden\" name=\"order\" id=\"orderfield\" value=\"\">"
"<button type=\"submit\" class=\"submit\">保存并重启设备</button>"
"<div id=\"result\" class=\"result\"></div>"
"<input type=\"hidden\" name=\"force\" id=\"forcefield\" value=\"\">"
"</form></div>"
"<div class=\"foot\">配置仅保存在设备本地，不会上传任何服务器<br>此热点无互联网属正常现象，直接填写表单即可</div>"
"<script>"
"function setScan(t){var b=document.getElementById('scanbtn');b.textContent=t;b.disabled=(t!=='扫描');}"
"async function scanWifi(){"
"setScan('扫描中…');"
"try{"
"var r=await fetch('/api/scan');"
"var ns=await r.json();"
"var s=document.getElementById('netsel');"
"s.innerHTML='<option value=\"\">— 选择网络 —</option>';"
"ns.forEach(function(n){var o=document.createElement('option');o.value=n.s;o.textContent=n.s+'  '+n.r+'dBm'+(n.e?' 加密':'');s.appendChild(o);});"
"s.style.display='block';"
"}catch(e){alert('扫描失败，请手动输入 WiFi 名称');}"
"setScan('扫描');}"
"function pickNet(){var v=document.getElementById('netsel').value;if(v)document.getElementById('ssid').value=v;}"
"function mvUp(b){var r=b.closest('.orow');var p=r.previousElementSibling;if(p)r.parentNode.insertBefore(r,p);}"
"function mvDn(b){var r=b.closest('.orow');var n=r.nextElementSibling;if(n)r.parentNode.insertBefore(n,r);}"
"function toggleDefault(){var d=document.getElementById('usedefault');document.getElementById('orderlist').classList.toggle('off',d.checked);}"
"function buildOrder(){var f=document.getElementById('orderfield');if(document.getElementById('usedefault').checked){f.value='';return;}var ps=[];var rs=document.querySelectorAll('#orderlist .orow');for(var i=0;i<rs.length;i++){var ck=rs[i].querySelector('.ochk');if(ck&&ck.checked)ps.push(rs[i].getAttribute('data-cmp'));}f.value=ps.join(',');}"
"var submitting=false;"
"function setBtn(t){var b=document.querySelector('button.submit');b.disabled=(t!=='保存并重启设备');b.textContent=t;}"
"async function submitForm(ev,bypass){"
"ev.preventDefault();"
"if(submitting)return false;submitting=true;"
"document.getElementById('forcefield').value=bypass?'1':'';buildOrder();"
"setBtn('正在验证并保存…');"
"var p=document.getElementById('result');"
"p.className='result wait';p.innerHTML='<b>正在验证…</b><br>正在连接 WiFi 并核对序列号对应的打印数据，约需 15~35 秒。长按设备 OK 可取消';"
"try{"
"var body=new URLSearchParams(new FormData(document.querySelector('form'))).toString();"
"var r=await fetch('/save?api=1',{method:'POST',body:body,headers:{'Content-Type':'application/x-www-form-urlencoded'}});"
"var j=await r.json();"
"if(j.ok){"
"p.className='result okr';p.textContent=(j.verified?'配对验证通过':'已跳过验证，配置尚未确认')+'。正在重启试运行；90 秒内未收到有效打印数据将恢复原配置。';"
"}else if(j.stage==='mqtt'){"
"p.className='result errr';p.innerHTML='<b>打印机配对未通过</b><br>'+j.err+'<div class=\"rbtns\"><button type=\"button\" class=\"fbtn\" onclick=\"forceSave()\">跳过验证并试运行</button><button type=\"button\" class=\"rbtn\" onclick=\"closeResult()\">返回修改</button></div>';"
"setBtn('保存并重启设备');"
"}else{"
"p.className='result errr';p.innerHTML='<b>'+j.err+'</b>';"
"setBtn('保存并重启设备');"
"}"
"}catch(e){"
"p.className='result errr';p.innerHTML='<b>网络中断</b><br>验证期间连接到了新 WiFi 会短暂切换信道导致本页面掉线，直接重试即可';"
"setBtn('保存并重启设备');"
"}"
"submitting=false;return false;"
"}"
"function forceSave(){document.getElementById('forcefield').value='1';submitForm({preventDefault:function(){}},true);}"
"function closeResult(){var p=document.getElementById('result');p.className='result';p.innerHTML='';}"
"</script>"
"</body></html>",
        esc_ssid,
        have_pass ? "（已配置，不改留空）" : " <em>*</em>",
        have_pass ? "不改请留空" : "必填",
        esc_ip, esc_ser,
        have_code ? "（已配置，不改留空）" : " <em>*</em>",
        have_code ? "" : "required ",
        have_code ? "不改请留空" : "必填",
        style_opts, lang_opts,
        use_default ? " checked" : "", use_default ? " off" : "", order_rows);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
esp_err_t wifi_prov_start(void) {
    if (s_prov_running) return ESP_OK;

    // 网络栈: 未配置开机路径此时尚未初始化; 运行中路径 bambu_mqtt_stop()
    // 已拆除 STA 但保留了 event loop/netif init, 重复调用会返回错误, 忽略即可
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    // 扫描事件: SCAN_DONE 回调置位, api_scan 等待 (带超时)
    s_scan_events = xEventGroupCreate();
    if (!s_scan_events) return ESP_ERR_NO_MEM;
    ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, scan_done_handler, NULL);
    // 保存前验证: WiFi 连接结果 (prov_wifi_event 填充 s_wifi_verify)
    if (ret == ESP_OK) ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, prov_wifi_event, NULL);
    if (ret == ESP_OK) ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, prov_wifi_event, NULL);
    if (ret != ESP_OK) { wifi_prov_stop(); return ret; }

    // AP + STA 双接口: 扫描 (esp_wifi_scan_start) 依赖 STA 接口,
    // 纯 AP 模式下扫描会失败或只能扫到本信道 —— 这是配网页时扫不出热点的主因
    s_cancel = s_restart_pending = false;
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wcfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init 失败: %s", esp_err_to_name(ret));
        wifi_prov_stop();
        return ret;
    }

    s_wifi_initialized = true;

    // 热点名带 MAC 后缀, 避免同楼层多台设备撞名
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X",
             AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_cfg = { 0 };
    strncpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = AP_MAX_CONN;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;   // 开放网络, 方便手机自动弹配网页

    // APSTA: STA 不配凭证保持空闲, 只为扫描提供接口; 热点功能不受影响
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    ret |= esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    ret |= esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP 启动失败: %s", esp_err_to_name(ret));
        wifi_prov_stop();
        return ret;
    }
    s_prov_running = true;
    ESP_LOGI(TAG, "配网热点已开启: %s (无密码), 配置页 http://192.168.4.1", s_ap_ssid);

    // 降低 httpd 日志级别: 重定向流量会产生大量无效请求日志
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    // HTTP 表单
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.task_priority = 4;              // 低于 LVGL(5), 避免单核 C3 抢 CPU 导致看门狗
    hcfg.stack_size = 8192;             // 默认 4096 装不下 root_get 的 6KB 页面缓冲
    hcfg.max_open_sockets = 4;          // LWIP_MAX_SOCKETS 有限, 留余量给内部 socket
    hcfg.lru_purge_enable = true;   // 满时关闭最久连接
    ret = httpd_start(&s_server, &hcfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败: %s", esp_err_to_name(ret));
        wifi_prov_stop();
        return ret;
    }
    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t uri_save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t uri_scan = { .uri = "/api/scan", .method = HTTP_GET, .handler = api_scan };
    ret = httpd_register_uri_handler(s_server, &uri_root);
    if (ret == ESP_OK) ret = httpd_register_uri_handler(s_server, &uri_save);
    if (ret == ESP_OK) ret = httpd_register_uri_handler(s_server, &uri_scan);
    if (ret != ESP_OK) { wifi_prov_stop(); return ret; }

    // 404 → 302 重定向到配页 (绝对 URL, iOS 要求 302 + body)
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    // 官方 DNS 重定向: 所有 A 查询 → SoftAP IP (192.168.4.1)
    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns = start_dns_server(&dns_cfg);

    ESP_LOGI(TAG, "配置网页已就绪");
    return ESP_OK;
}

void wifi_prov_get_ap_ssid(char *out, size_t outlen) {
    if (!out || outlen == 0) return;
    strncpy(out, s_ap_ssid, outlen - 1);
    out[outlen - 1] = '\0';
}

bool wifi_prov_request_cancel(void) {
    portENTER_CRITICAL(&s_prov_mux);
    bool allowed = !s_restart_pending;
    if (allowed) s_cancel = true;
    portEXIT_CRITICAL(&s_prov_mux);
    return allowed;
}

void wifi_prov_stop(void) {
    s_cancel = true;
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
    if (s_dns) { stop_dns_server(s_dns); s_dns = NULL; }
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, scan_done_handler);
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, prov_wifi_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, prov_wifi_event);
    if (s_wifi_initialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_ap_netif) { esp_netif_destroy_default_wifi(s_ap_netif); s_ap_netif = NULL; }
    if (s_sta_netif) { esp_netif_destroy_default_wifi(s_sta_netif); s_sta_netif = NULL; }
    if (s_scan_events) { vEventGroupDelete(s_scan_events); s_scan_events = NULL; }
    s_prov_running = false;
}

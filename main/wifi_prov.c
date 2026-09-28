// main/wifi_prov.c —— 配网模式实现
//
// 组成:
//   1. SoftAP 热点 "Passport-XXXX" (开放网络, XXXX 取 MAC 后两字节避免撞名)
//   2. captive DNS: 劫持所有域名解析到 192.168.4.1 (手机连上后自动弹页)
//   3. HTTP 表单: 填写 WiFi / 打印机 IP / 序列号 / 访问码, 保存到 NVS 后重启
//
// 网页表单留空的 WiFi 密码/访问码沿用旧值 (方便只改一台打印机或换网)。

#include "wifi_prov.h"
#include "app_config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "dns_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define AP_SSID_PREFIX  "Passport-"
#define AP_MAX_CONN     4

static const char *TAG = "wifi_prov";

static char s_ap_ssid[32] = AP_SSID_PREFIX;
static bool s_prov_running = false;

// DNS 使用官方 dns_server 组件 (components/dns_server/),
// 所有 A 查询重定向到 SoftAP IP (192.168.4.1)

// ---------------------------------------------------------------------------
// 表单解析辅助
// ---------------------------------------------------------------------------
// 取 urlencoded 体中某字段的值 (已 urldecode); 不存在返回 false
static bool form_field(const char *body, const char *key,
                       char *out, size_t outlen) {
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            const char *end = strchr(p, '&');
            size_t raw = end ? (size_t)(end - p) : strlen(p);
            if (raw >= outlen * 3 + 1) raw = outlen * 3;
            // '+' 转空格 + %XX 解码
            size_t o = 0;
            for (size_t i = 0; i < raw && o + 1 < outlen; i++) {
                if (p[i] == '+') out[o++] = ' ';
                else if (p[i] == '%' && i + 2 < raw) {
                    char hex[3] = { p[i + 1], p[i + 2], 0 };
                    out[o++] = (char)strtol(hex, NULL, 16);
                    i += 2;
                } else out[o++] = p[i];
            }
            out[o] = '\0';
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    out[0] = '\0';
    return false;
}

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
static bool valid_ipv4(const char *s) {
    int parts = 0, val = -1;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            if (val < 0) val = 0;
            val = val * 10 + (*s - '0');
            if (val > 255) return false;
        } else if (*s == '.') {
            if (val < 0 || parts > 2) return false;
            parts++; val = -1;
        } else return false;
    }
    return parts == 3 && val >= 0;
}

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
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<style>*{box-sizing:border-box;margin:0}"
"body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(160deg,#0f1923,#16324a 60%,#0f1923);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px}"
".card{max-width:420px;width:100%;background:#fff;border-radius:20px;padding:28px 24px;text-align:center;box-shadow:0 20px 60px rgba(0,0,0,.35)}"
".logo{width:60px;height:60px;border-radius:16px;background:linear-gradient(135deg,#00c853,#009624);display:flex;align-items:center;justify-content:center;font-size:30px;margin:0 auto 16px}"
"h1{font-size:19px;color:#111;margin-bottom:12px}"
".ok{background:#e8f5e9;border-radius:14px;padding:18px 16px;color:#1b5e20;font-size:15px;line-height:1.7;text-align:left}"
".ok b{font-size:16px}.hint{margin-top:14px;font-size:12px;color:#999;line-height:1.6}</style></head><body>"
"<div class=\"card\"><div class=\"logo\">🖨</div>"
"<h1>拓竹打印机监控器</h1>"
"<div class=\"ok\"><b>✔ 配置已保存</b><br>设备正在重启，稍后会自动连接你的 WiFi 并显示打印数据。</div>"
"<div class=\"hint\">若未连上：长按设备 OK 键可重新进入本页面。</div>"
"</div></body></html>";

static const char PAGE_ERR[] =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<style>*{box-sizing:border-box;margin:0}"
"body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(160deg,#0f1923,#16324a 60%%,#0f1923);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px}"
".card{max-width:420px;width:100%%;background:#fff;border-radius:20px;padding:28px 24px;box-shadow:0 20px 60px rgba(0,0,0,.35)}"
".err{background:#fdecea;border-radius:14px;padding:18px 16px;color:#b71c1c;font-size:15px;line-height:1.7}"
".err b{font-size:16px}a{display:inline-block;margin-top:18px;padding:12px 20px;border-radius:12px;background:#0f1923;color:#fff;text-decoration:none;font-size:14px;font-weight:600}</style></head><body>"
"<div class=\"card\"><div class=\"err\"><b>✘ 保存失败</b><br>%s</div>"
"<a href=\"/\">← 返回重新填写</a></div></body></html>";

// http_404: 重定向到绝对 IP, 确保手机 captive portal WebView 能准确找到配置页
// 注意: iOS 要求响应体必须有内容, 否则不识别为 captive portal
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req) {
    char body[1024] = "";
    int len = req->content_len;
    if (len > 0 && len < (int)sizeof(body)) {
        int r = httpd_req_recv(req, body, len);
        if (r < 0) r = 0;
        body[r] = '\0';
    } else if (len >= (int)sizeof(body)) {
        goto bad_request;
    }

    const app_config_t *old = app_config_get();
    app_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    char val[80];
    form_field(body, "ssid", val, sizeof(val));
    strncpy(cfg.wifi_ssid, val, sizeof(cfg.wifi_ssid) - 1);
    form_field(body, "pass", val, sizeof(val));
    if (val[0]) strncpy(cfg.wifi_pass, val, sizeof(cfg.wifi_pass) - 1);
    else strncpy(cfg.wifi_pass, old->wifi_pass, sizeof(cfg.wifi_pass) - 1);
    form_field(body, "ip", val, sizeof(val));
    strncpy(cfg.printer_ip, val, sizeof(cfg.printer_ip) - 1);
    form_field(body, "serial", val, sizeof(val));
    strncpy(cfg.printer_serial, val, sizeof(cfg.printer_serial) - 1);
    form_field(body, "code", val, sizeof(val));
    if (val[0]) strncpy(cfg.access_code, val, sizeof(cfg.access_code) - 1);
    else strncpy(cfg.access_code, old->access_code, sizeof(cfg.access_code) - 1);

    // 校验: 必填项 (密码/访问码可由旧值兜底, 已配置过才允许留空)
    const char *err = NULL;
    if (!cfg.wifi_ssid[0]) err = "WiFi 名称不能为空";
    else if (!cfg.wifi_pass[0] && !old->wifi_pass[0]) err = "WiFi 密码不能为空";
    else if (!cfg.printer_ip[0]) err = "打印机 IP 不能为空";
    else if (!valid_ipv4(cfg.printer_ip)) err = "打印机 IP 格式错误，请输入如 192.168.1.105";
    else if (!cfg.printer_serial[0]) err = "打印机序列号不能为空";
    else if (!cfg.access_code[0]) err = "访问码不能为空";
    if (err) {
        char page[1024];
        snprintf(page, sizeof(page), PAGE_ERR, err);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t ret = app_config_save(&cfg);
    if (ret != ESP_OK) {
        char page[1024];
        snprintf(page, sizeof(page), PAGE_ERR, "写入存储失败");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "配置已保存, 2 秒后重启");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PAGE_OK, HTTPD_RESP_USE_STRLEN);
    schedule_restart();
    return ESP_OK;

bad_request:
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    return ESP_OK;
}

// 配页静态存储: httpd 单线程顺序处理, 用 static 避免 6KB 占用栈内存
static char s_page[6144];

// ---------------------------------------------------------------------------
// WiFi 扫描接口 (GET /api/scan)
// SoftAP 模式下同步扫描周围网络, 返回 JSON. 用户可从列表选择, 防止手输 SSID 错误.
// 扫描占用 1-3 秒, 此时 httpd 任务阻塞 — 对配网页场景完全可以接受.
// ---------------------------------------------------------------------------
static esp_err_t api_scan(httpd_req_t *req) {
    static wifi_ap_record_t recs[12];
    static char json[1024];

    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true /* 阻塞直到扫描完成 */) != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
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

    snprintf(s_page, sizeof(s_page),
"<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
"<title>拓竹打印机监控器 · 配置</title>"
"<style>*{box-sizing:border-box;margin:0}"
"body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(160deg,#0f1923,#16324a 60%%,#0f1923);min-height:100vh;padding:28px 16px}"
".card{max-width:420px;margin:0 auto;background:#fff;border-radius:20px;padding:26px 22px;box-shadow:0 20px 60px rgba(0,0,0,.35)}"
".head{display:flex;align-items:center;gap:14px}"
".logo{width:52px;height:52px;border-radius:14px;background:linear-gradient(135deg,#00c853,#009624);display:flex;align-items:center;justify-content:center;font-size:26px;flex-shrink:0}"
"h1{font-size:19px;color:#111}"
"p.tip{font-size:13px;color:#8a939e;line-height:1.6;margin-top:5px}"
".sec{margin-top:24px;font-size:12px;font-weight:700;color:#009624;letter-spacing:.06em;display:flex;align-items:center;gap:10px}"
".sec:after{content:\"\";flex:1;height:1px;background:#e8ebef}"
"label{display:block;font-size:13px;color:#5a6470;margin:14px 0 6px;font-weight:600}"
"label em{font-style:normal;color:#009624}"
"input{width:100%%;padding:12px;border:1.5px solid #e2e5e9;border-radius:12px;font-size:16px;background:#f7f8fa;color:#111;transition:border-color .2s,box-shadow .2s;-webkit-appearance:none}"
"input:focus{outline:none;border-color:#00ae42;background:#fff;box-shadow:0 0 0 3px rgba(0,174,66,.13)}"
"button{width:100%%;margin-top:28px;padding:14px;border:0;border-radius:14px;"
"background:linear-gradient(135deg,#00c853,#009624);color:#fff;font-size:16px;font-weight:700;"
"box-shadow:0 8px 20px rgba(0,174,66,.32);cursor:pointer}"
"button:active{transform:scale(.98)}"
".foot{max-width:420px;margin:16px auto 0;text-align:center;font-size:12px;color:rgba(255,255,255,.5);line-height:1.6}</style>"
"</head><body>"
"<div class=\"card\">"
"<div class=\"head\"><div class=\"logo\">🖨</div><div><h1>拓竹打印机监控器</h1>"
"<p class=\"tip\">填写后点保存，设备自动重启并连接</p></div></div>"
"<form method=\"POST\" action=\"/save\">"
"<div class=\"sec\">WIFI \u00b7 \u4ec5\u652f持 2.4GHz</div>"
"<label>WiFi \u540d\u79f0 <em>*</em></label>"
"<div style=\"display:flex;gap:8px\">"
"<input id=\"ssid\" name=\"ssid\" required maxlength=\"32\" value=\"%s\" placeholder=\"\u8f93\u5165\u6216\u70b9\u51fb\u626b\u63cf\" autocomplete=\"off\" style=\"flex:1\">"
"<button type=\"button\" onclick=\"scanWifi()\" id=\"scanbtn\" style=\"flex-shrink:0;width:auto;margin:0;padding:0 14px;background:#f0f4f8;border:1.5px solid #d0d8e0;border-radius:12px;color:#1a3a52;font-size:14px;font-weight:600;box-shadow:none;cursor:pointer\">\xf0\x9f\x94\x8d \u626b\u63cf</button>"
"</div>"
"<select id=\"netsel\" onchange=\"pickNet()\" style=\"display:none;width:100%%;margin-top:8px;padding:10px;border-radius:10px;border:1.5px solid #e2e5e9;font-size:15px;background:#f7f8fa;color:#111\"></select>"
"<label>WiFi 密码%s</label>"
"<input name=\"pass\" type=\"password\" maxlength=\"64\" placeholder=\"%s\">"
"<div class=\"sec\">打印机 · 需开启仅局域网模式</div>"
"<label>打印机 IP <em>*</em></label>"
"<input name=\"ip\" required maxlength=\"15\" value=\"%s\" placeholder=\"如 192.168.1.105\" inputmode=\"decimal\" pattern=\"([0-9]{1,3}\\.){3}[0-9]{1,3}\" title=\"输入如 192.168.1.105\" autocomplete=\"off\">"
"<label>序列号（15 位，打印机 设置→设备） <em>*</em></label>"
"<input name=\"serial\" required maxlength=\"30\" value=\"%s\" placeholder=\"打印机序列号\" autocomplete=\"off\">"
"<label>访问码（8 位，打印机 设置→网络）%s</label>"
"<input name=\"code\" maxlength=\"32\" %splaceholder=\"%s\" autocomplete=\"off\">"
"<button type=\"submit\">保存并重启设备</button>"
"</form></div>"
"<div class=\"foot\">\u914d\u7f6e\u4ec5\u4fdd\u5b58\u5728\u8bbe\u5907\u672c\u5730，\u4e0d\u4f1a\u4e0a\u4f20\u4efb\u4f55\u670d\u52a1\u5668<br>\u6b64\u70ed\u70b9\u65e0\u4e92\u8054\u7f51\u5c5e\u6b63\u5e38\u73b0\u8c61，\u76f4\u63a5\u586b\u5199\u8868\u5355\u5373\u53ef</div>"
"<script>"
"async function scanWifi(){"
"var b=document.getElementById('scanbtn');"
"b.textContent='\u626b\u63cf\u4e2d\u2026';b.disabled=true;"
"try{"
"var r=await fetch('/api/scan');"
"var ns=await r.json();"
"var s=document.getElementById('netsel');"
"s.innerHTML='<option value=\"\">\u2014 \u9009\u62e9\u7f51\u7edc \u2014</option>';"
"ns.forEach(function(n){var o=document.createElement('option');o.value=n.s;o.textContent=n.s+'  '+n.r+'dBm'+(n.e?' \xf0\x9f\x94\x92':'');s.appendChild(o);});"
"s.style.display='block';"
"}catch(e){alert('\u626b\u63cf\u5931\u8d25，\u8bf7\u624b\u52a8\u8f93\u5165 WiFi \u540d\u79f0');}"
"b.textContent='\xf0\x9f\x94\x8d \u626b\u63cf';b.disabled=false;}"
"function pickNet(){var v=document.getElementById('netsel').value;if(v)document.getElementById('ssid').value=v;}"
"</script>"
"</body></html>",
        esc_ssid,
        have_pass ? "（已配置，不改留空）" : " <em>*</em>",
        have_pass ? "不改请留空" : "必填",
        esc_ip, esc_ser,
        have_code ? "（已配置，不改留空）" : " <em>*</em>",
        have_code ? "" : "required ",
        have_code ? "不改请留空" : "必填");

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
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&wcfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init 失败: %s", esp_err_to_name(ret));
        return ret;
    }

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

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    ret |= esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    ret |= esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP 启动失败: %s", esp_err_to_name(ret));
        return ret;
    }
    s_prov_running = true;
    ESP_LOGI(TAG, "配网热点已开启: %s (无密码), 配置页 http://192.168.4.1", s_ap_ssid);

    // 降低 httpd 日志级别: 重定向流量会产生大量无效请求日志
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    // HTTP 表单
    httpd_handle_t server = NULL;
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.task_priority = 4;              // 低于 LVGL(5), 避免单核 C3 抢 CPU 导致看门狗
    hcfg.stack_size = 8192;             // 默认 4096 装不下 root_get 的 6KB 页面缓冲
    hcfg.max_open_sockets = 4;          // LWIP_MAX_SOCKETS 有限, 留余量给内部 socket
    hcfg.lru_purge_enable = true;   // 满时关闭最久连接
    ret = httpd_start(&server, &hcfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败: %s", esp_err_to_name(ret));
        return ret;
    }
    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t uri_save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t uri_scan = { .uri = "/api/scan", .method = HTTP_GET, .handler = api_scan };
    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_save);
    httpd_register_uri_handler(server, &uri_scan);

    // 404 → 302 重定向到配页 (绝对 URL, iOS 要求 302 + body)
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    // 官方 DNS 重定向: 所有 A 查询 → SoftAP IP (192.168.4.1)
    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    start_dns_server(&dns_cfg);

    ESP_LOGI(TAG, "配置网页已就绪");
    return ESP_OK;
}

void wifi_prov_get_ap_ssid(char *out, size_t outlen) {
    if (!out || outlen == 0) return;
    strncpy(out, s_ap_ssid, outlen - 1);
    out[outlen - 1] = '\0';
}

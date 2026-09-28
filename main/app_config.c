// main/app_config.c —— 运行时配置实现
//
// 存储布局: NVS 命名空间 "bmon", 每个字段一个 key。
// 编译期默认值: config.h 中的 CFG_* 宏; 若仍是 config.example.h 的
// 占位符 (YOUR_WIFI_SSID 等) 则视为"作者未配置", 固件进入未配置状态。

#include "app_config.h"
#include "config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "app_config";

#define CFG_NS     "bmon"
#define KEY_SSID   "ssid"
#define KEY_PASS   "pass"
#define KEY_IP     "ip"
#define KEY_SERIAL "serial"
#define KEY_CODE   "code"
#define KEY_STYLE  "style"
#define KEY_LANG   "lang"
#define KEY_CORDER "corder"

#define STYLE_MIN  1      // STYLE_BAMBU
#define STYLE_MAX  12     // STYLE_APPLE

static app_config_t s_cfg;
static bool s_provisioned = false;

// 把 CSV 顺序串 (如 "5,4,1,2,6,8") 解析为有序子集: 校验 1-9、去重、上限 max。
// 返回写入个数 (空串/全非法 -> 0)。
static int parse_comp_csv(const char *csv, uint8_t *out, int max) {
    int n = 0;
    if (!csv) return 0;
    const char *p = csv;
    while (*p && n < max) {
        while (*p == ',' || *p == ' ') p++;
        if (*p < '0' || *p > '9') { while (*p && *p != ',') p++; continue; }
        int v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        if (v < 1 || v > APP_COMP_MAX) continue;
        bool dup = false;
        for (int i = 0; i < n; i++) if (out[i] == (uint8_t)v) { dup = true; break; }
        if (!dup) out[n++] = (uint8_t)v;
        while (*p == ',') p++;
    }
    return n;
}

// 把 cfg->comp_order 序列化为 CSV (无配置时写空串)
static void serialize_comp_csv(const app_config_t *cfg, char *buf, size_t sz) {
    buf[0] = '\0';
    int off = 0;
    for (int i = 0; i < cfg->comp_count && i < APP_COMP_MAX; i++) {
        int w = snprintf(buf + off, (sz > (size_t)off) ? sz - off : 0, "%s%d",
                         i ? "," : "", cfg->comp_order[i]);
        if (w < 0 || (size_t)w >= sz - off) break;
        off += w;
    }
}

// config.example.h 的占位符视为未配置
static bool compile_cfg_valid(void) {
    return CFG_WIFI_SSID[0] != '\0' && strcmp(CFG_WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

// 显示偏好 (风格/语言/组件顺序) 独立于网络配网状态: 只要 NVS 有合法值就采用
static void load_display_prefs(nvs_handle_t h) {
    uint8_t v;
    if (nvs_get_u8(h, KEY_STYLE, &v) == ESP_OK && v >= STYLE_MIN && v <= STYLE_MAX) {
        s_cfg.ui_style = v;
    }
    if (nvs_get_u8(h, KEY_LANG, &v) == ESP_OK && (v == LANG_EN || v == LANG_CN)) {
        s_cfg.lang = v;
    }
    // 组件顺序: key 存在则解析 (空串表示用户显式选择“走风格默认”= count 0);
    // key 不存在则保留编译期 CFG_COMPONENT_ORDER 种子。
    char csv[32];
    size_t len = sizeof(csv);
    if (nvs_get_str(h, KEY_CORDER, csv, &len) == ESP_OK) {
        s_cfg.comp_count = (uint8_t)parse_comp_csv(csv, s_cfg.comp_order, APP_COMP_MAX);
    }
}

esp_err_t app_config_init(void) {
    memset(&s_cfg, 0, sizeof(s_cfg));
    // 显示默认值: 编译期 config.h (NVS 有记录时被覆盖)
    s_cfg.ui_style = CFG_UI_STYLE;
    s_cfg.lang     = CFG_LANG;
    // 组件顺序编译期默认 (向后兼容 CFG_COMPONENT_ORDER); NVS 无 key 时保留
#ifdef CFG_COMPONENT_ORDER
    {
        static const int k_cfg_comp[] = CFG_COMPONENT_ORDER;
        size_t cn = sizeof(k_cfg_comp) / sizeof(k_cfg_comp[0]);
        if (cn > APP_COMP_MAX) cn = APP_COMP_MAX;
        for (size_t i = 0; i < cn; i++) s_cfg.comp_order[i] = (uint8_t)k_cfg_comp[i];
        s_cfg.comp_count = (uint8_t)cn;
    }
#endif
    s_provisioned = false;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要擦除重建");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_handle_t h;
    ret = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (ret == ESP_OK) {
        size_t len;
        bool ok = true;
        load_display_prefs(h);
        len = sizeof(s_cfg.wifi_ssid);
        ok = ok && nvs_get_str(h, KEY_SSID, s_cfg.wifi_ssid, &len) == ESP_OK;
        len = sizeof(s_cfg.wifi_pass);
        ok = ok && nvs_get_str(h, KEY_PASS, s_cfg.wifi_pass, &len) == ESP_OK;
        len = sizeof(s_cfg.printer_ip);
        ok = ok && nvs_get_str(h, KEY_IP, s_cfg.printer_ip, &len) == ESP_OK;
        len = sizeof(s_cfg.printer_serial);
        ok = ok && nvs_get_str(h, KEY_SERIAL, s_cfg.printer_serial, &len) == ESP_OK;
        len = sizeof(s_cfg.access_code);
        ok = ok && nvs_get_str(h, KEY_CODE, s_cfg.access_code, &len) == ESP_OK;
        nvs_close(h);
        s_provisioned = ok && s_cfg.wifi_ssid[0] != '\0';
        if (!s_provisioned) {
            // 网络配置不完整: 清回默认, 但保留已读到的显示偏好 (风格/语言/组件顺序)
            uint8_t keep_style = s_cfg.ui_style, keep_lang = s_cfg.lang;
            uint8_t keep_corder[APP_COMP_MAX]; uint8_t keep_count = s_cfg.comp_count;
            memcpy(keep_corder, s_cfg.comp_order, sizeof(keep_corder));
            memset(&s_cfg, 0, sizeof(s_cfg));
            s_cfg.ui_style = keep_style;
            s_cfg.lang     = keep_lang;
            memcpy(s_cfg.comp_order, keep_corder, sizeof(s_cfg.comp_order));
            s_cfg.comp_count = keep_count;
        }
    }

    if (s_provisioned) {
        ESP_LOGI(TAG, "已加载 NVS 配置 (SSID: %s, 打印机: %s)",
                 s_cfg.wifi_ssid, s_cfg.printer_ip);
        return ESP_OK;
    }

    // NVS 无记录: 回退到编译期默认值 (作者在自己电脑上编译时填的 config.h)
    if (compile_cfg_valid()) {
        strncpy(s_cfg.wifi_ssid, CFG_WIFI_SSID, sizeof(s_cfg.wifi_ssid) - 1);
        strncpy(s_cfg.wifi_pass, CFG_WIFI_PASSWORD, sizeof(s_cfg.wifi_pass) - 1);
        strncpy(s_cfg.printer_ip, CFG_PRINTER_IP, sizeof(s_cfg.printer_ip) - 1);
        strncpy(s_cfg.printer_serial, CFG_PRINTER_SERIAL, sizeof(s_cfg.printer_serial) - 1);
        strncpy(s_cfg.access_code, CFG_ACCESS_CODE, sizeof(s_cfg.access_code) - 1);
        ESP_LOGI(TAG, "NVS 无记录, 使用编译期默认配置 (SSID: %s)", s_cfg.wifi_ssid);
    } else {
        ESP_LOGW(TAG, "固件未包含任何网络配置, 应进入配网模式");
    }
    return ESP_OK;
}

const app_config_t *app_config_get(void) {
    return &s_cfg;
}

bool app_config_is_provisioned(void) {
    return s_provisioned;
}

int app_config_comp_order(int *out, int max) {
    if (!out || max <= 0) return 0;
    int n = s_cfg.comp_count;
    if (n > APP_COMP_MAX) n = APP_COMP_MAX;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = s_cfg.comp_order[i];
    return n;
}

int app_config_set_comp_order_csv(app_config_t *cfg, const char *csv) {
    if (!cfg) return 0;
    memset(cfg->comp_order, 0, sizeof(cfg->comp_order));
    cfg->comp_count = (uint8_t)parse_comp_csv(csv, cfg->comp_order, APP_COMP_MAX);
    return cfg->comp_count;
}

esp_err_t app_config_save(const app_config_t *cfg) {
    if (!cfg || cfg->wifi_ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(h, KEY_SSID, cfg->wifi_ssid);
    if (ret == ESP_OK) ret = nvs_set_str(h, KEY_PASS, cfg->wifi_pass);
    if (ret == ESP_OK) ret = nvs_set_str(h, KEY_IP, cfg->printer_ip);
    if (ret == ESP_OK) ret = nvs_set_str(h, KEY_SERIAL, cfg->printer_serial);
    if (ret == ESP_OK) ret = nvs_set_str(h, KEY_CODE, cfg->access_code);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_STYLE, cfg->ui_style);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_LANG, cfg->lang);
    char corder_csv[32];
    serialize_comp_csv(cfg, corder_csv, sizeof(corder_csv));
    if (ret == ESP_OK) ret = nvs_set_str(h, KEY_CORDER, corder_csv);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "配置已保存到 NVS");
    } else {
        ESP_LOGE(TAG, "配置保存失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

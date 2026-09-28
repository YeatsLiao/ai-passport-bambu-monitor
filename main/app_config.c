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
#include <string.h>

static const char *TAG = "app_config";

#define CFG_NS     "bmon"
#define KEY_SSID   "ssid"
#define KEY_PASS   "pass"
#define KEY_IP     "ip"
#define KEY_SERIAL "serial"
#define KEY_CODE   "code"

static app_config_t s_cfg;
static bool s_provisioned = false;

// config.example.h 的占位符视为未配置
static bool compile_cfg_valid(void) {
    return CFG_WIFI_SSID[0] != '\0' && strcmp(CFG_WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

esp_err_t app_config_init(void) {
    memset(&s_cfg, 0, sizeof(s_cfg));
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
        if (!s_provisioned) memset(&s_cfg, 0, sizeof(s_cfg));
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
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "配置已保存到 NVS");
    } else {
        ESP_LOGE(TAG, "配置保存失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

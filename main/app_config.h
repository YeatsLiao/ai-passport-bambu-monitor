// main/app_config.h —— 运行时配置 (NVS 持久化 + 编译期默认值)
//
// 优先级: NVS 中已保存的配置 > config.h 编译期默认值 > 空白(未配置)。
// 未配置的固件开机直接进入配网模式 (见 wifi_prov)，用户通过手机网页
// 填写 WiFi 与打印机信息，保存到 NVS 后重启生效——无需重新编译。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char wifi_ssid[33];        // 2.4GHz WiFi 名称
    char wifi_pass[65];        // WiFi 密码
    char printer_ip[16];       // 打印机局域网 IP
    char printer_serial[32];   // 打印机序列号 (15 位)
    char access_code[33];      // 打印机访问码 (8 位, 仅局域网模式)
} app_config_t;

// 初始化 NVS 并加载配置 (NVS 无记录时回退到 config.h 编译期默认值)
esp_err_t app_config_init(void);

// 全局配置单例 (app_config_init 之后有效)
const app_config_t *app_config_get(void);

// NVS 中是否已保存过配置 (决定开机是否直接进配网模式)
bool app_config_is_provisioned(void);

// 保存配置到 NVS (整体覆盖)
esp_err_t app_config_save(const app_config_t *cfg);

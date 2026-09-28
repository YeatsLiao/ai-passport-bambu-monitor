// main/wifi_prov.h —— 配网模式 (SoftAP 热点 + 网页表单)
//
// 流程: 设备开启热点 → 用户手机连上热点 → (多数手机自动弹出) 打开
// http://192.168.4.1 → 填写 WiFi 与打印机信息 → 保存 → 设备自动重启。
#pragma once

#include "esp_err.h"
#include <stddef.h>

// 启动配网模式: SoftAP 热点 + captive DNS + 配置页面。
// 幂等 (重复调用无副作用)。调用方需保证 WiFi 驱动未被 STA 占用
// (未配置的固件开机即调；运行中进入配网前先 bambu_mqtt_stop())。
esp_err_t wifi_prov_start(void);

// 获取热点名称 (wifi_prov_start 后有效；未启动时返回默认前缀)
void wifi_prov_get_ap_ssid(char *out, size_t outlen);

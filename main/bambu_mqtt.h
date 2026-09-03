// main/bambu_mqtt.h —— 拓竹打印机 MQTT-TLS 客户端
#pragma once

#include "esp_err.h"
#include "bambu_state.h"

// 启动 MQTT 连接（内部会先连 WiFi）
esp_err_t bambu_mqtt_start(void);

// 断开并释放资源
void bambu_mqtt_stop(void);

// 当前连接状态
bool bambu_mqtt_connected(void);

// 手动请求全量推送（pushall）
void bambu_mqtt_pushall(void);

// 获取连接状态描述（用于 UI 显示）
const char *bambu_mqtt_status_str(void);

// main/main.c —— ai-passport-bambu-monitor 入口
//
// 启动流程:
//   1. 初始化状态
//   2. 初始化显示硬件（SPI + 面板）
//   3. 初始化按键
//   4. 连接 WiFi + MQTT（TLS 握手峰值 ~50KB）
//   5. MQTT 连接成功后 TLS 释放内存
//   6. 初始化 LVGL + 创建 UI（此时堆内存充足）

#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "config.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "ui/ui_monitor.h"
#include "ui/ui_theme.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "main";

// 按键回调（运行在 button 组件任务中，操作 LVGL 需加锁）
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        ESP_LOGI(TAG, "长按 OK");
    } else {
        ui_monitor_key(btn, ev);
    }

    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "ai-passport-bambu-monitor 启动");
    ESP_LOGI(TAG, "UI Style: %d", CFG_UI_STYLE);

    // 1. 初始化状态
    bambu_state_init();

    // 2. 初始化显示硬件（仅 SPI + 面板，不初始化 LVGL）
    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    bsp_display_backlight(CFG_BACKLIGHT_PERCENT);
    ESP_LOGI(TAG, "显示硬件就绪");

    // 3. 初始化按键
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
    }

    // 3.5 初始化电池（可选，无电量计时会静默跳过）
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电池未检测到，跳过");
    }

    // 4. 连接 WiFi + MQTT（TLS 握手峰值 ~50KB）
    esp_err_t ret = bambu_mqtt_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(ret));
        return;
    }

    // 5. 等待 MQTT 连接成功（TLS 握手完成后释放内存）
    ESP_LOGI(TAG, "等待 MQTT 连接...");
    int wait_count = 0;
    while (!bambu_mqtt_connected() && wait_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_count++;
    }

    if (!bambu_mqtt_connected()) {
        ESP_LOGE(TAG, "MQTT 连接超时！仍然启动 UI...");
    } else {
        ESP_LOGI(TAG, "MQTT 已连接，TLS 内存已释放");
    }

    // 6. 初始化 LVGL
    ESP_LOGI(TAG, "初始化 LVGL (可用堆: %lu)...", esp_get_free_heap_size());

    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "LVGL 初始化成功");

    // 7. 加载监控页面
    ui_monitor_enter();

    ESP_LOGI(TAG, "启动完成 堆=%lu", esp_get_free_heap_size());

    // 主循环（保持运行，LVGL 在内部任务中刷新）
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

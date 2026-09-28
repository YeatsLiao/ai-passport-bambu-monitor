// main/main.c —— ai-passport-bambu-monitor 入口
//
// 启动流程:
//   1. 初始化状态 + 加载运行时配置 (NVS / config.h 编译期默认值)
//   2. 初始化显示硬件（SPI + 面板）
//   3. 初始化按键
//   4. 已配置 → 连接 WiFi + MQTT（TLS 握手峰值 ~50KB）→ 连接后释放 TLS 内存
//      未配置 → 跳过联网，直接进配网模式（SoftAP 热点 + 手机网页填写配置）
//   5. 初始化 LVGL + 创建 UI
//   6. 已配置但连接失败 → 显示指引卡（长按 OK 可进配网模式）

#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "config.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "app_config.h"
#include "wifi_prov.h"
#include "ui/ui_monitor.h"
#include "ui/ui_theme.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "main";

static volatile bool s_ui_ready = false;   // LVGL 就绪前丢弃按键, 防止误锁未初始化的 port 层
static volatile bool s_prov_mode = false;  // 配网模式中 (重启生效, 无退出)

// 进入配网模式: 拆除监控连接 → 开热点+网页 → 屏上显示指引。
// 网络操作放 worker 任务, 不阻塞按键回调; 保存配置后设备自动重启。
static void prov_worker(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "进入配网模式, 拆除监控连接...");
    bambu_mqtt_stop();
    if (wifi_prov_start() != ESP_OK) {
        ESP_LOGE(TAG, "配网服务启动失败, 允许用户重新尝试");
        s_prov_mode = false;  // 重置标志, 让用户可以再次长按 OK 重试
        vTaskDelete(NULL);
        return;
    }
    char ssid[32];
    wifi_prov_get_ap_ssid(ssid, sizeof(ssid));
    if (bsp_lvgl_lock(1000)) {
        ui_monitor_show_prov_mode(ssid);
        bsp_lvgl_unlock();
    }
    vTaskDelete(NULL);
}

static void enter_prov_mode(void) {
    if (s_prov_mode || !s_ui_ready) return;
    s_prov_mode = true;
    if (xTaskCreate(prov_worker, "prov", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "配网任务创建失败");
        s_prov_mode = false;
    }
}

// 按键回调（运行在 button 组件任务中，操作 LVGL 需加锁）
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_ui_ready) return;   // MQTT/TLS 握手窗口内 ADC 抖动可能产生误按键

    // 长按 OK: 任何时刻都进入配网模式 (重新配置 WiFi/打印机)
    // 用户主动长按 = 明确意图，无需先断开才允许触发
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        enter_prov_mode();
        return;
    }
    if (s_prov_mode) return;   // 配网模式下不再翻页/刷新

    if (!bsp_lvgl_lock(500)) return;
    ui_monitor_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "ai-passport-bambu-monitor 启动");
    ESP_LOGI(TAG, "UI Style: %d", CFG_UI_STYLE);

    // 1. 初始化状态 + 运行时配置 (NVS 优先, 回退 config.h 编译期默认值)
    bambu_state_init();
    app_config_init();
    bool provisioned = app_config_is_provisioned();
    if (!provisioned) {
        ESP_LOGW(TAG, "固件未配置网络, 将直接进入配网模式");
    }

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

    // 4. 已配置: 连接 WiFi + MQTT（TLS 握手峰值 ~50KB）; 未配置: 跳过直接配网
    bool mqtt_ok = false;
    if (provisioned) {
        esp_err_t ret = bambu_mqtt_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(ret));
        } else {
            // 等待 MQTT 连接成功（TLS 握手完成后释放内存）
            ESP_LOGI(TAG, "等待 MQTT 连接...");
            int wait_count = 0;
            while (!bambu_mqtt_connected() && wait_count < 30) {
                vTaskDelay(pdMS_TO_TICKS(500));
                wait_count++;
            }
            mqtt_ok = bambu_mqtt_connected();
        }
    }

    if (mqtt_ok) {
        ESP_LOGI(TAG, "MQTT 已连接，TLS 内存已释放");
        // 5. 初始化 SNTP 时间同步（WiFi 已连接，中国时区 UTC+8）
        setenv("TZ", "CST-8", 1);
        tzset();
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP 时间同步已启动");
    }

    // 6. 初始化 LVGL
    ESP_LOGI(TAG, "初始化 LVGL (可用堆: %lu)...", esp_get_free_heap_size());

    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "LVGL 初始化成功");

    // 7. 加载监控页面 (LVGL 任务自 bsp_lvgl_init 后已在跑 lv_timer_handler,
    //    构建/修改对象树必须持锁, 否则与布局遍历并发 → 对象树写坏 → layout 崩溃)
    if (bsp_lvgl_lock(2000)) {
        ui_monitor_enter();
        bsp_lvgl_unlock();
    }
    s_ui_ready = true;

    if (!provisioned) {
        // 未配置固件 (刷入他人 bin / config.h 全占位符): 开机直接进配网模式
        enter_prov_mode();
    } else if (!mqtt_ok) {
        // 已配置但 15s 内未连上: 叠加指引卡, 长按 OK 可进配网模式;
        // 若只是暂时断网, 连接成功后指引卡由刷新定时器自动撤除
        if (bsp_lvgl_lock(2000)) {
            ui_monitor_show_setup_hint();
            bsp_lvgl_unlock();
        }
    }

    ESP_LOGI(TAG, "启动完成 堆=%lu", esp_get_free_heap_size());

    // 主循环（保持运行，LVGL 在内部任务中刷新）
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

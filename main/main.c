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
#include "screenshot.h"
#include "ui_monitor.h"
#include "ui/ui_settings.h"
#include "ui_theme.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "main";

static volatile bool s_ui_ready = false;   // LVGL 就绪前丢弃按键, 防止误锁未初始化的 port 层
static volatile bool s_prov_mode = false;  // 配网模式中 (重启生效, 无退出)
static volatile bool s_screen_off = false; // 熄屏中 (任意按键唤醒, 该次按键不产生其他效果)
static int64_t s_last_key_us = 0;          // 上次按键时刻 (esp_timer 微秒)

// 进入配网模式: 拆除监控连接 → 开热点+网页 → 屏上显示指引。
// 网络操作放 worker 任务, 不阻塞按键回调; 保存配置后设备自动重启。
static volatile bool s_prov_started, s_cancel_prov;

static void enter_prov_mode(void) {
    if (s_prov_mode || !s_ui_ready) return;
    s_prov_mode = true;
}

// Main task owns all network lifecycles; button callbacks only set requests.
static void begin_provisioning(void) {
    bambu_mqtt_stop();
    if (wifi_prov_start() != ESP_OK) {
        s_prov_mode = false;
        if (app_config_is_provisioned()) bambu_mqtt_start();
        return;
    }
    s_prov_started = true;
    char ssid[32];
    wifi_prov_get_ap_ssid(ssid, sizeof(ssid));
    if (bsp_lvgl_lock(1000)) {
        ui_monitor_show_prov_mode(ssid);
        bsp_lvgl_unlock();
    }
}

// 按键回调（运行在 button 组件任务中，操作 LVGL 需加锁）
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_ui_ready) return;   // MQTT/TLS 握手窗口内 ADC 抖动可能产生误按键

    s_last_key_us = esp_timer_get_time();

    // 熄屏中: 任意按键只负责唤醒并吞掉本次事件 (含长按 OK), 防止误触发翻页/配网
    if (s_screen_off) {
        s_screen_off = false;
        if (bsp_lvgl_lock(500)) {
            bsp_display_wake();
            bsp_lvgl_unlock();
        }
        ESP_LOGI(TAG, "按键唤醒屏幕");
        return;
    }

    // 配网入口在设置页 → "重置网络" (监控最后一页按↓进入设置页);
    // 配网模式下不再翻页/刷新
    if (s_prov_mode) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) s_cancel_prov = true;
        return;
    }

    if (!bsp_lvgl_lock(500)) return;
    ui_monitor_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "ai-passport-bambu-monitor 启动");
    ESP_LOGI(TAG, "UI Style: %d", CFG_UI_STYLE);

    // 拓竹打印机用自签名证书, MQTT 必须跳过主机名校验, esp-tls 每次连接会
    // 打一条 "skip_common_name=true" 提示; 已知且无害, 压到 ERROR 级别不再刷屏
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_ERROR);

    // 1. 初始化状态 + 运行时配置 (NVS 优先, 回退 config.h 编译期默认值)
    bambu_state_init();
    app_config_init();
    const int64_t trial_deadline = esp_timer_get_time() + 90000000;
    bool provisioned = app_config_is_provisioned();
    if (!provisioned) {
        ESP_LOGW(TAG, "固件未配置网络, 将直接进入配网模式");
    }

    // 2. 初始化显示硬件（仅 SPI + 面板，不初始化 LVGL）
    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    {
        uint8_t bl = app_config_get()->backlight;   // 设置页调整过的值 (NVS)
        if (bl < 10 || bl > 100) bl = CFG_BACKLIGHT_PERCENT;
        bsp_display_backlight(bl);
    }
    ESP_LOGI(TAG, "显示硬件就绪");

    // 3. 初始化按键
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
    }
    // 设置页"重置网络" → 进入配网模式 (原"长按 OK"入口已移除)
    ui_settings_set_prov_cb(enter_prov_mode);

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
                bambu_mqtt_poll();
                vTaskDelay(pdMS_TO_TICKS(500));
                wait_count++;
            }
            mqtt_ok = bambu_mqtt_connected();
        }
    }

    bool sntp_started = false;
    if (mqtt_ok) {
        ESP_LOGI(TAG, "MQTT 已连接，TLS 内存已释放");
        // 5. 初始化 SNTP 时间同步（WiFi 已连接，中国时区 UTC+8）
        setenv("TZ", "CST-8", 1);
        tzset();
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_init();
        sntp_started = true;
        ESP_LOGI(TAG, "SNTP 时间同步已启动");
    }

    // 6. 初始化 LVGL
    ESP_LOGI(TAG, "初始化 LVGL (可用堆: %lu)...", esp_get_free_heap_size());

    lv_display_t *disp = bsp_lvgl_init();
    if (!disp) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }
    // 串口截屏监听 (tools/device/capture_screen.ps1): 在显示对象创建后注册
    // FLUSH_START 钩子, 截屏经 tools/device 截图脚本触发
    if (bsp_lvgl_lock(2000)) {
        screenshot_init(disp);
        bsp_lvgl_unlock();
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
    s_last_key_us = esp_timer_get_time();
    bool trial_proven = false;
    int64_t next_retry = 0;
    while (1) {
        if (s_prov_mode && !s_prov_started) begin_provisioning();
        if (s_cancel_prov) {
            s_cancel_prov = false;
            if (s_prov_started && wifi_prov_request_cancel()) {
                wifi_prov_stop();
                s_prov_started = s_prov_mode = false;
                if (bsp_lvgl_lock(1000)) {
                    ui_monitor_hide_setup();
                    bsp_lvgl_unlock();
                }
                if (app_config_is_provisioned()) bambu_mqtt_start();
            }
        }
        if (!s_prov_mode) {
            if (app_config_is_provisioned() && esp_timer_get_time() >= next_retry) {
                bambu_mqtt_start();
                next_retry = esp_timer_get_time() + 10000000;
            }
            bambu_mqtt_poll();
            if (bambu_mqtt_connected() && !sntp_started) {
                setenv("TZ", "CST-8", 1); tzset();
                esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "ntp.aliyun.com");
                esp_sntp_setservername(1, "pool.ntp.org");
                esp_sntp_init();
                sntp_started = true;
            }
            if (app_config_has_trial()) {
                if (bambu_mqtt_has_report()) trial_proven = true;
                if (trial_proven) {
                    if (app_config_confirm_trial() != ESP_OK)
                        ESP_LOGE(TAG, "Trial confirmation storage failed; retrying");
                } else if (esp_timer_get_time() >= trial_deadline) {
                    if (app_config_rollback_trial() == ESP_OK) {
                        ESP_LOGW(TAG, "Trial timed out; restoring previous configuration");
                        esp_restart();
                    }
                }
            }
        }

        // 空闲超时自动熄屏 (任意按键唤醒, 见 on_key); 分钟数在设置页可调, 0=关闭
        uint8_t soff = app_config_get()->screen_off_min;
        if (soff > 0 && !s_prov_mode && s_ui_ready && !s_screen_off &&
            esp_timer_get_time() - s_last_key_us >
                (int64_t)soff * 60 * 1000000) {
            if (bsp_lvgl_lock(500)) {
                bsp_display_sleep();
                bsp_lvgl_unlock();
                s_screen_off = true;
                ESP_LOGI(TAG, "空闲 %d 分钟, 熄屏", soff);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

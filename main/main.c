// main/main.c —— ai-passport-bambu-monitor 入口
//
// 启动流程:
//   1. 初始化显示 + LVGL
//   2. 初始化按键
//   3. 显示启动画面
//   4. 启动 WiFi + MQTT 连接
//   5. 进入监控页面
//
// 按键交互:
//   UP/DOWN 短按: 翻页（CARD/CUTE 风格）
//   OK 短按:      刷新数据
//   OK 长按:      退出监控页面（预留）

#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_pins.h"
#include "config.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "ui/ui_monitor.h"
#include "ui/ui_theme.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "main";

// 启动画面
static lv_obj_t *s_boot_scr = NULL;
static lv_obj_t *s_boot_label = NULL;

static void show_boot_screen(void) {
    s_boot_scr = lv_obj_create(NULL);
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_set_style_bg_color(s_boot_scr, lv_color_hex(c->bg), 0);

    // 标题
    lv_obj_t *title = lv_label_create(s_boot_scr);
    lv_label_set_text(title, "Bambu Monitor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(c->accent), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    // 风格信息
    const char *style_names[] = {"", "Compact", "Dashboard", "Card", "Cute"};
    const char *theme_names[] = {"", "Dark", "Light", "Bambu", "Pastel"};

    char info[64];
    snprintf(info, sizeof(info), "Style: %s / %s",
             style_names[CFG_UI_STYLE], theme_names[CFG_THEME]);

    s_boot_label = lv_label_create(s_boot_scr);
    lv_label_set_text(s_boot_label, info);
    lv_obj_set_style_text_font(s_boot_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_boot_label, lv_color_hex(c->text_secondary), 0);
    lv_obj_align(s_boot_label, LV_ALIGN_CENTER, 0, 20);

    // 连接状态
    lv_obj_t *status = lv_label_create(s_boot_scr);
    lv_label_set_text(status, "Connecting...");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(c->warning), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 50);

    lv_screen_load(s_boot_scr);
}

static void show_monitor(void) {
    if (s_boot_scr) {
        lv_obj_delete(s_boot_scr);
        s_boot_scr = NULL;
        s_boot_label = NULL;
    }
    ui_monitor_enter();
}

// 按键回调（运行在 button 组件任务中，操作 LVGL 需加锁）
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    if (s_boot_scr) {
        // 启动画面中，任意按键无操作
    } else {
        // 监控页面中
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            // 长按 OK 退出（预留）
            ESP_LOGI(TAG, "长按 OK - 退出监控页面");
        } else {
            ui_monitor_key(btn, ev);
        }
    }

    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "ai-passport-bambu-monitor 启动");
    ESP_LOGI(TAG, "UI Style: %d, Theme: %d", CFG_UI_STYLE, CFG_THEME);

    // 初始化状态
    bambu_state_init();

    // 初始化显示
    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    bsp_display_backlight(CFG_BACKLIGHT_PERCENT);

    // 初始化 LVGL
    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }

    // 初始化按键
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
    }

    // 显示启动画面
    if (bsp_lvgl_lock(1000)) {
        show_boot_screen();
        bsp_lvgl_unlock();
    }

    // 启动 MQTT 连接（在后台任务中）
    esp_err_t ret = bambu_mqtt_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(ret));
    }

    // 等待连接成功后切换到监控页面
    // 这里简化处理：延迟 3 秒后切换（实际应该等 MQTT 连接成功事件）
    vTaskDelay(pdMS_TO_TICKS(3000));

    if (bsp_lvgl_lock(1000)) {
        show_monitor();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪");
}

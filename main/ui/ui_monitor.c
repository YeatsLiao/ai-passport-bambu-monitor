// main/ui/ui_monitor.c —— 监控页面框架
//
// 职责:
//   1. 管理 LVGL 屏幕生命周期 (enter/exit)
//   2. 定时刷新 (1s → 调用当前风格的 update)
//   3. 按键分发 (UP/DOWN 翻页, OK 刷新)
//   4. 页面重建调度 (rebuild_page → 清空屏幕 → 调用当前风格的 build)
//
// 具体 UI 渲染由各 style_xxx.c 实现。

#include "ui_monitor.h"
#include "ui_theme.h"
#include "ui_lang.h"
#include "ui_settings.h"
#include "ui_filament.h"
#include "bambu_pages.h"
#include "app_config.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_monitor";

lv_obj_t *s_scr = NULL;
static lv_timer_t *s_refresh_timer = NULL;
lv_obj_t *s_content_area  = NULL;   // 内容容器
static lv_obj_t *s_setup_overlay = NULL;   // 首次使用配置指引卡 (NULL=未显示)
static bambu_state_t s_frame;
static int s_page;
static int s_page_device_id = -1;
static uint32_t s_auto_request_ms;
static lv_obj_t *s_page_label, *s_filament_title, *s_notice, *s_notice_label;
static lv_obj_t *s_nav_label;
static lv_obj_t *s_printer_title;

bambu_state_t *ui_monitor_state(void) { return &s_frame; }
int ui_monitor_current_page(void) { return s_page; }
int ui_monitor_page_count(void) { return bambu_pages_count(&s_frame); }
void ui_monitor_bind_page_label(lv_obj_t *l) { s_page_label = l; }
void ui_monitor_bind_nav_label(lv_obj_t *l) { s_nav_label = l; }
void ui_monitor_settings_chrome(const char *hint, int row, int count) {
    if (s_content_area) lv_obj_add_flag(s_content_area, LV_OBJ_FLAG_HIDDEN);
    ui_filament_hide();
    if (s_notice) lv_obj_add_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
    if (s_nav_label) {
        lv_obj_set_width(s_nav_label, 184);
        lv_label_set_long_mode(s_nav_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(s_nav_label, hint);
    }
    if (s_page_label) lv_label_set_text_fmt(s_page_label, "%d/%d", row + 1, count);
}
void ui_monitor_bind_filament_title(lv_obj_t *l) { s_filament_title = l; }
void ui_monitor_bind_printer_title(lv_obj_t *l) {
    s_printer_title = l;
    if (l) {
        lv_obj_set_width(l, 76);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
}
static void update_printer_title(void) {
    if (s_printer_title) lv_label_set_text(s_printer_title,
        s_frame.printer_model[0] ? s_frame.printer_model : L_TITLE_BAMBU);
}
bool ui_monitor_data_ready(void) {
    return bambu_mqtt_connected() && bambu_state_fresh(&s_frame, (uint32_t)(esp_timer_get_time() / 1000));
}
const char *ui_monitor_connection_text(void) {
    if (!bambu_mqtt_connected()) return L_DISCONNECTED;
    return s_frame.data_valid ? L_STALE_DATA : L_WAIT_DATA;
}
int ui_monitor_printf(char *out, size_t n, uint32_t fields, const char *fmt, ...) {
    char unknown[96];
    if ((s_frame.valid_mask & fields) != fields) {
        // Keep leading icon/name %s conversions, replace the numeric portion.
        const char *p = fmt, *cut = fmt;
        while (*p) {
            if (*p++ != '%') continue;
            const char *start = p - 1;
            if (*p == '%') { ++p; continue; }
            while (*p && strchr("-+ #0.123456789*hlz", *p)) ++p;
            if (*p != 's' && *p != 'c') { cut = start; break; }
            if (*p) ++p;
        }
        snprintf(unknown, sizeof(unknown), "%.*s--", (int)(cut - fmt), fmt);
        fmt = unknown;
    }
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(out, n, fmt, args);
    va_end(args);
    return result;
}
int ui_monitor_tray_printf(char *out, size_t n, const bambu_ams_tray_t *t, const char *fmt, ...) {
    char unknown[96];
    if (t->remain < 0) {
        const char *pct = strstr(fmt, "%d%%");
        if (pct) {
            // Remaining percentage is the final conversion in theme formats.
            snprintf(unknown, sizeof(unknown), "%.*s--%s", (int)(pct - fmt), fmt, pct + 4);
            fmt = unknown;
        }
    }
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(out, n, fmt, args);
    va_end(args);
    return result;
}

// ---------------------------------------------------------------------------
// 风格注册表: 全部 12 套风格都编译进固件, 运行时按 app_config.ui_style 选择
// 索引 = STYLE_* 编号 - 1 (0..11)
// ---------------------------------------------------------------------------
typedef struct {
    void (*build)(void);
    void (*update)(void);
    int  (*page_count)(void);
    int  (*cur_page)(void);
    void (*next)(void);
    void (*prev)(void);
} style_ops_t;

#define STYLE_OPS(name) { \
    style_##name##_build, style_##name##_update, style_##name##_page_count, \
    style_##name##_current_page, style_##name##_next_page, style_##name##_prev_page \
}

static const style_ops_t s_styles[12] = {
    STYLE_OPS(bambu),      // 1  STYLE_BAMBU
    STYLE_OPS(cyber),      // 2  STYLE_CYBER
    STYLE_OPS(sheikah),    // 3  STYLE_SHEIKAH
    STYLE_OPS(white),      // 4  STYLE_WHITE
    STYLE_OPS(industrial), // 5  STYLE_INDUSTRIAL
    STYLE_OPS(neon),       // 6  STYLE_NEON
    STYLE_OPS(pixel),      // 7  STYLE_PIXEL
    STYLE_OPS(ssd),        // 8  STYLE_SSD
    STYLE_OPS(f1),         // 9  STYLE_F1
    STYLE_OPS(gauge),      // 10 STYLE_GAUGE
    STYLE_OPS(geist),      // 11 STYLE_GEIST
    STYLE_OPS(apple),      // 12 STYLE_APPLE
};

// 当前风格操作集 (配置值非法时回退 Bambu)
static const style_ops_t *cur_style(void) {
    uint8_t s = app_config_get()->ui_style;
    if (s < 1 || s > 12) s = STYLE_BAMBU;
    return &s_styles[s - 1];
}

static void refresh_frame(void) {
    bambu_state_copy(&s_frame);
    // Selection follows hardware identity, never the order of an MQTT array.
    if (s_page_device_id >= 0) s_page = bambu_pages_find(&s_frame, s_page_device_id);
    if (s_page >= bambu_pages_count(&s_frame)) s_page = 0;
    int idx = bambu_pages_unit(&s_frame, s_page);
    s_page_device_id = idx >= 0 ? s_frame.units[idx].id : idx == -2 ? 254 : -1;
    bambu_state_project(&s_frame, idx);
}

static void render_page(void) {
    const style_ops_t *style = cur_style();
    int idx = bambu_pages_unit(&s_frame, s_page);
    s_page_device_id = idx >= 0 ? s_frame.units[idx].id : idx == -2 ? 254 : -1;
    bambu_state_project(&s_frame, idx);
    bool single = idx == -2 || (idx >= 0 && s_frame.units[idx].slot_count == 1);
    int physical = s_page > 0 && !single ? 1 : 0;
    if (style->cur_page() != physical) style->next();
    style->update();
    update_printer_title();
    if (single) {
        lv_obj_add_flag(s_content_area, LV_OBJ_FLAG_HIDDEN);
        ui_filament_show(&s_frame, idx);
    } else {
        lv_obj_clear_flag(s_content_area, LV_OBJ_FLAG_HIDDEN);
        ui_filament_hide();
        if (idx >= 0 && s_filament_title) {
            char title[40];
            const bambu_ams_unit_t *u = &s_frame.units[idx];
            snprintf(title, sizeof(title), "%s %d", bambu_ams_kind_name(u->kind), u->id + 1);
            lv_label_set_text(s_filament_title, title);
        }
    }
    if (s_page_label) lv_label_set_text_fmt(s_page_label, "%d/%d", s_page + 1, bambu_pages_count(&s_frame));
    if (s_notice) {
        if (!ui_monitor_data_ready()) {
            lv_obj_set_pos(s_notice, 8, 34);
            lv_obj_set_size(s_notice, 224, 252);
            lv_label_set_text(s_notice_label, ui_monitor_connection_text());
            lv_obj_center(s_notice_label);
            lv_obj_clear_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
            if (!ui_settings_is_open()) lv_obj_move_foreground(s_notice);
        } else if (s_frame.ams_overflow) {
            lv_obj_set_pos(s_notice, 8, 266);
            lv_obj_set_size(s_notice, 224, 20);
            lv_label_set_text(s_notice_label, L_DEVICE_LIMIT);
            lv_obj_center(s_notice_label);
            lv_obj_clear_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_monitor_restore(void) {
    if (s_nav_label) {
        lv_obj_set_width(s_nav_label, LV_SIZE_CONTENT);
        lv_label_set_text(s_nav_label, L_NAV_HINT);
    }
    refresh_frame();
    render_page();
}

// ---------------------------------------------------------------------------
// 页面重建（只清理内容区域，标题栏和底部栏保持不变）
// ---------------------------------------------------------------------------
void rebuild_page(void) {
    // 当前风格使用显示/隐藏切换, 此函数保留供兼容
    const style_ops_t *st = cur_style();
    st->build();
    ESP_LOGI(TAG, "页面已重建 (style=%s, page=%d/%d)",
             ui_theme_style_name(),
             st->cur_page() + 1, st->page_count());
}

// ---------------------------------------------------------------------------
// 首次使用配置指引: 刷入他人固件 / config.h 未配置时 WiFi 无法连接,
// MQTT 启动 15s 超时后由 main.c 调用, 在监控页上方叠加全屏指引卡。
// 连接成功后由刷新定时器自动撤除, 无需按键。
// ---------------------------------------------------------------------------
void ui_monitor_show_setup_hint(void) {
    if (!s_scr || s_setup_overlay) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_t *ov = lv_obj_create(s_scr);
    if (!ov) return;
    s_setup_overlay = ov;
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_size(ov, 240, 320);
    lv_obj_set_style_bg_color(ov, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 16, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ov);

    // 警告图标 (图标字形只在 Montserrat, 用 SYMBOL 字体)
    lv_obj_t *ico = lv_label_create(ov);
    if (ico) {
        lv_label_set_text(ico, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_font(ico, L_FONT_SYMBOL_BIG, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(c->warning), 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 40);
    }

    // 标题
    lv_obj_t *title = lv_label_create(ov);
    if (title) {
        lv_label_set_text(title, L_SETUP_TITLE);
        lv_obj_set_style_text_font(title, L_FONT_TEXT_BIG, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(c->text_primary), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 86);
    }

    // 正文 (自动换行, 预留左右内边距后的宽度)
    lv_obj_t *body = lv_label_create(ov);
    if (body) {
        lv_label_set_text(body, L_SETUP_HINT);
        lv_obj_set_style_text_font(body, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(body, lv_color_hex(c->text_secondary), 0);
        lv_obj_set_width(body, 208);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 124);
    }

    ESP_LOGW(TAG, "显示首次使用配置指引 (MQTT 未连接)");
}

// ---------------------------------------------------------------------------
// 配网模式指引: 全屏覆盖, 显示热点名与配置页地址。
// 用户保存配置后设备自动重启, 因此无需撤除逻辑。
// ---------------------------------------------------------------------------
void ui_monitor_show_prov_mode(const char *ap_ssid) {
    if (!s_scr) return;

    // 从失败指引切入配网时, 先撤掉旧指引卡避免叠加
    if (s_setup_overlay) {
        lv_obj_delete(s_setup_overlay);
        s_setup_overlay = NULL;
    }
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_t *ov = lv_obj_create(s_scr);
    if (!ov) return;
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_size(ov, 240, 320);
    lv_obj_set_style_bg_color(ov, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 16, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ov);

    // WiFi 图标 (图标字形只在 Montserrat, 用 SYMBOL 字体)
    lv_obj_t *ico = lv_label_create(ov);
    if (ico) {
        lv_label_set_text(ico, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(ico, L_FONT_SYMBOL_BIG, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(c->accent), 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 36);
    }

    // 标题
    lv_obj_t *title = lv_label_create(ov);
    if (title) {
        lv_label_set_text(title, L_PROV_TITLE);
        lv_obj_set_style_text_font(title, L_FONT_TEXT_BIG, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(c->text_primary), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 78);
    }

    // 步骤 1: 手机连接热点 (热点名用主文字色: 各套在卡面上均 ≥4.5:1;
    // accent 仅留给图标/色块, 否则 Pixel 围巾橙 1.6:1 会重新看不清)
    lv_obj_t *s1 = lv_label_create(ov);
    if (s1) {
        lv_label_set_text_fmt(s1, "1. %s", L_PROV_STEP1);
        lv_obj_set_style_text_font(s1, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s1, lv_color_hex(c->text_secondary), 0);
        lv_obj_align(s1, LV_ALIGN_TOP_MID, 0, 122);
    }
    lv_obj_t *ssid_lbl = lv_label_create(ov);
    if (ssid_lbl) {
        lv_label_set_text(ssid_lbl, ap_ssid ? ap_ssid : "");
        lv_obj_set_style_text_font(ssid_lbl, L_FONT_NUM_BIG, 0);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(c->text_primary), 0);
        lv_obj_align(ssid_lbl, LV_ALIGN_TOP_MID, 0, 144);
    }

    // 步骤 2: 浏览器打开 (地址纯 ASCII, 用 Montserrat 数字字体清晰易读)
    lv_obj_t *s2 = lv_label_create(ov);
    if (s2) {
        lv_label_set_text_fmt(s2, "2. %s", L_PROV_STEP2);
        lv_obj_set_style_text_font(s2, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s2, lv_color_hex(c->text_secondary), 0);
        lv_obj_align(s2, LV_ALIGN_TOP_MID, 0, 192);
    }
    lv_obj_t *url = lv_label_create(ov);
    if (url) {
        lv_label_set_text(url, L_PROV_URL);
        lv_obj_set_style_text_font(url, L_FONT_NUM_BIG, 0);
        lv_obj_set_style_text_color(url, lv_color_hex(c->text_primary), 0);
        lv_obj_align(url, LV_ALIGN_TOP_MID, 0, 214);
    }

    // 步骤 3: 说明 (自动换行)
    lv_obj_t *s3 = lv_label_create(ov);
    if (s3) {
        lv_label_set_text_fmt(s3, "3. %s", L_PROV_STEP3);
        lv_obj_set_style_text_font(s3, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s3, lv_color_hex(c->text_secondary), 0);
        lv_obj_set_width(s3, 208);
        lv_label_set_long_mode(s3, LV_LABEL_LONG_WRAP);
        lv_obj_align(s3, LV_ALIGN_TOP_MID, 0, 262);
    }

    ESP_LOGI(TAG, "显示配网模式指引 (热点: %s)", ap_ssid ? ap_ssid : "?");
    s_setup_overlay = ov;  // 记录指针, 让 refresh_timer 跳过背景刷新 + 后续可删除
}

void ui_monitor_hide_setup(void) {
    if (s_setup_overlay) { lv_obj_delete(s_setup_overlay); s_setup_overlay = NULL; }
    ui_monitor_restore();
}

// ---------------------------------------------------------------------------
// 定时刷新
// ---------------------------------------------------------------------------
static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    // 指引卡在连接成功后自动撤除, 露出下方监控页
    if (s_setup_overlay && bambu_mqtt_connected()) {
        lv_obj_delete(s_setup_overlay);
        s_setup_overlay = NULL;
        ESP_LOGI(TAG, "MQTT 已连接, 撤除配置指引");
    }
    // 配网指引显示时跳过背景刷新 (省 CPU, 避免看门狗超时)
    if (s_setup_overlay) return;
    refresh_frame();
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    // Idle LAN printers may remain silent. Request a snapshot before the stale
    // threshold instead of making a healthy idle printer look permanently stale.
    if (bambu_mqtt_connected() && (uint32_t)(now - s_frame.last_update_ms) >= 30000U &&
        (uint32_t)(now - s_auto_request_ms) >= 30000U) {
        s_auto_request_ms = now;
        bambu_mqtt_pushall();
    }
    if (ui_settings_is_open()) {
        // Keep the native clock/battery alive, then restore the settings footer.
        cur_style()->update();
        update_printer_title();
        ui_settings_refresh();
        return;
    }
    render_page();
}

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
void ui_monitor_enter(void) {
    s_page = 0;
    s_page_device_id = -1;
    bambu_state_copy(&s_frame);
    s_scr = lv_obj_create(NULL);
    if (!s_scr) {
        ESP_LOGE(TAG, "屏幕创建失败！内存不足");
        return;
    }
    lv_screen_load(s_scr);

    // 由风格 build 函数创建标题栏/底部栏/内容区（使用主题颜色）
    cur_style()->build();
    ui_filament_create(s_scr);
    const ui_theme_colors_t *c = ui_theme_get_colors();
    s_notice = lv_obj_create(s_scr);
    lv_obj_set_pos(s_notice, 8, 34);
    lv_obj_set_size(s_notice, 224, 252);
    lv_obj_set_style_bg_color(s_notice, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_bg_opa(s_notice, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_notice, 0, 0);
    lv_obj_set_style_pad_all(s_notice, 12, 0);
    lv_obj_set_style_radius(s_notice, c->radius, 0);
    lv_obj_remove_flag(s_notice, LV_OBJ_FLAG_SCROLLABLE);
    s_notice_label = lv_label_create(s_notice);
    lv_obj_set_width(s_notice_label, 196);
    lv_obj_set_style_text_font(s_notice_label, L_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_notice_label, lv_color_hex(c->text_primary), 0);
    lv_obj_set_style_text_align(s_notice_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_notice_label);
    refresh_frame();
    render_page();

    s_refresh_timer = lv_timer_create(refresh_timer_cb, 1000, NULL);
    ESP_LOGI(TAG, "监控页面已加载 (style=%s)", ui_theme_style_name());
}

void ui_monitor_exit(void) {
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_content_area  = NULL;
    ui_filament_forget();
    s_page_label = s_filament_title = s_notice = s_notice_label = NULL;
    s_nav_label = NULL;
    s_printer_title = NULL;
}

void ui_monitor_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 设置页打开时按键全部交给它 (含 LONG OK 退出)
    if (ui_settings_is_open()) {
        ui_settings_key(btn, ev);
        return;
    }
    if (ev != BSP_BTN_CLICK) return;   // 监控页长按无功能 (配网入口已移到设置页)

    switch (btn) {
        case BSP_BTN_UP:
            refresh_frame();
            s_page = (s_page + bambu_pages_count(&s_frame) - 1) % bambu_pages_count(&s_frame);
            render_page();
            break;
        case BSP_BTN_DOWN:
            // 最后一页再向下 → 打开设置页 (亮度/熄屏/重置网络)
            refresh_frame();
            if (s_page >= bambu_pages_count(&s_frame) - 1)
                ui_settings_open();
            else {
                ++s_page;
                render_page();
            }
            break;
        case BSP_BTN_OK:
            bambu_mqtt_pushall();
            ESP_LOGI(TAG, "手动刷新 (pushall)");
            break;
        default:
            break;
    }
}

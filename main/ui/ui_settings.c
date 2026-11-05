// Shared settings behavior with native monitor chrome and theme presentation.
#include "ui_settings.h"
#include "ui_lang.h"
#include "ui_theme.h"
#include "ui_monitor.h"
#include "app_config.h"
#include "bsp_display.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_settings";
enum { ROW_BRIGHT, ROW_SLEEP, ROW_RESET, ROW_BACK, ROW_COUNT };
static const uint8_t k_sleep_preset[] = {0, 1, 3, 5, 10, 30, 60};
static bool s_open, s_edit, s_reset_armed;
static int s_row;
static uint8_t s_bright, s_sleep;
static lv_obj_t *s_cover, *s_panel, *s_status;
static lv_obj_t *s_rows[ROW_COUNT], *s_names[ROW_COUNT], *s_values[ROW_COUNT];
static lv_obj_t *s_markers[ROW_COUNT], *s_cursors[ROW_COUNT], *s_bars[2];
static const char *s_feedback;
static bool s_error;
static void (*s_prov_cb)(void);

bool ui_settings_is_open(void) { return s_open; }
void ui_settings_set_prov_cb(void (*cb)(void)) { s_prov_cb = cb; }

static uint32_t panel_color(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    return ui_theme_settings_skin()->flat ? c->bg : c->card_bg;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font) {
    lv_obj_t *o = lv_label_create(parent);
    if (!o) return NULL;
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(ui_theme_get_colors()->text_primary), 0);
    return o;
}

static void sync_footer(void) {
    const char *hint = s_reset_armed ? L_SETTINGS_CONFIRM_HINT : s_edit ? L_SETTINGS_EDIT_HINT : L_SETTINGS_HINT;
    ui_monitor_settings_chrome(hint, s_row, ROW_COUNT);
}

static void draw(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    const ui_settings_skin_t *skin = ui_theme_settings_skin();
    const char *names[] = {L_BRIGHTNESS, L_SLEEP, L_RESET_NET, L_BACK};
    uint32_t panel = panel_color();
    const char *status = s_reset_armed ? L_CONFIRM_RESET : s_feedback ? s_feedback : s_edit ? L_SETTINGS_EDITING : L_DISPLAY_OPTIONS;
    lv_label_set_text(s_status, status);
    lv_obj_set_style_text_color(s_status, lv_color_hex(ui_theme_readable(s_error ? c->error : c->text_secondary, panel)), 0);

    for (int i = 0; i < ROW_COUNT; ++i) {
        bool selected = i == s_row;
        uint32_t fill = panel;
        if (selected) fill = skin->hard_shadow ? c->warning : ui_theme_mix(s_reset_armed ? c->error : c->accent, panel, s_edit ? 24 : 13);
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(fill), 0);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(selected ? c->accent : c->border), 0);
        // Border geometry never changes with selection or edit state.
        lv_obj_set_style_border_width(s_rows[i], skin->row_border, 0);
        lv_label_set_text(s_names[i], names[i]);
        lv_obj_set_style_text_color(s_names[i], lv_color_hex(ui_theme_readable(c->text_primary, fill)), 0);
        lv_obj_set_style_text_color(s_values[i], lv_color_hex(ui_theme_readable(selected ? c->text_primary : c->text_secondary, fill)), 0);
        lv_obj_set_style_text_color(s_markers[i], lv_color_hex(ui_theme_readable(c->text_secondary, fill)), 0);
        if (skin->rank_numbers) lv_label_set_text_fmt(s_markers[i], "%d", i + 1);
        else lv_label_set_text(s_markers[i], selected ? (s_edit ? "=" : LV_SYMBOL_RIGHT) : "");
        lv_obj_set_style_bg_color(s_cursors[i], lv_color_hex(selected ? (s_reset_armed ? c->error : c->accent) : fill), 0);
        switch (i) {
            case ROW_BRIGHT: lv_label_set_text_fmt(s_values[i], "%d%%", s_bright); break;
            case ROW_SLEEP:
                if (s_sleep) lv_label_set_text_fmt(s_values[i], "%d%s", s_sleep, L_MIN);
                else lv_label_set_text(s_values[i], L_OFF);
                break;
            case ROW_RESET: lv_label_set_text(s_values[i], s_reset_armed ? LV_SYMBOL_WARNING : LV_SYMBOL_RIGHT); break;
            case ROW_BACK: lv_label_set_text(s_values[i], LV_SYMBOL_RIGHT); break;
        }
        if (i < 2 && s_bars[i]) {
            lv_bar_set_value(s_bars[i], i == ROW_BRIGHT ? s_bright : s_sleep * 100 / 60, LV_ANIM_OFF);
        }
    }
    sync_footer();
}

void ui_settings_refresh(void) { if (s_open) sync_footer(); }

static void reload_saved(void) {
    const app_config_t *cfg = app_config_get();
    s_bright = cfg->backlight;
    s_sleep = cfg->screen_off_min;
    bsp_display_backlight(s_bright);
}

static void close_settings(void) {
    if (s_cover) lv_obj_delete(s_cover);
    s_cover = s_panel = s_status = NULL;
    for (int i = 0; i < ROW_COUNT; ++i) {
        s_rows[i] = s_names[i] = s_values[i] = s_markers[i] = s_cursors[i] = NULL;
    }
    s_bars[0] = s_bars[1] = NULL;
    s_open = s_edit = s_reset_armed = false;
    reload_saved();
    ui_monitor_restore();
}

void ui_settings_open(void) {
    if (s_open || !s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    const ui_settings_skin_t *skin = ui_theme_settings_skin();
    uint32_t panel = panel_color();
    bool compact = skin->hard_shadow;
    int width = compact ? 214 : 224;
    int height = compact ? 218 : 252;
    int row_height = compact ? 34 : 40;
    int row_start = compact ? 52 : 58;
    s_row = 0;
    s_edit = s_reset_armed = s_error = false;
    s_feedback = NULL;
    reload_saved();

    // Only the monitor's content area is covered. Its themed header/footer,
    // battery, clock, SSD contacts and pixel landscape stay in their own layers.
    s_cover = box(s_scr, 4, 30, 232, 256, c->bg);
    if (!s_cover) return;
    lv_obj_set_style_bg_opa(s_cover, LV_OPA_TRANSP, 0);
    if (skin->hard_shadow && !box(s_cover, 8, 8, width, height, c->border)) goto fail;
    s_panel = box(s_cover, 4, 4, width, height, panel);
    if (!s_panel) goto fail;
    int radius = skin->flat ? 0 : c->radius;
    lv_obj_set_style_radius(s_panel, radius, 0);
    lv_obj_set_style_border_width(s_panel, skin->frame_border, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(c->border), 0);
    // A small, theme-native title rule; no accent-colored body text.
    lv_obj_t *title = label(s_panel, 12, 4, width - 24, 24, L_FONT_TEXT_BIG);
    if (!title) goto fail;
    lv_label_set_text(title, L_SETTINGS);
    lv_obj_set_style_text_color(title, lv_color_hex(ui_theme_readable(c->text_primary, panel)), 0);
    s_status = label(s_panel, 12, 34, width - 24, 18, L_FONT_TEXT);
    if (!s_status) goto fail;
    if (!box(s_panel, 12, 29, width - 24, app_config_get()->ui_style == STYLE_SSD ? 2 : 1, c->accent)) goto fail;

    for (int i = 0; i < ROW_COUNT; ++i) {
        lv_obj_t *row = box(s_panel, 8, row_start + i * (row_height + 4), width - 16, row_height, panel);
        if (!row) goto fail;
        s_rows[i] = row;
        lv_obj_set_style_radius(row, skin->row_radius, 0);
        lv_obj_set_style_border_width(row, skin->row_border, 0);
        int label_y = (row_height - 18) / 2;
        s_cursors[i] = box(row, 0, 6, 3, row_height - 12, c->accent);
        s_markers[i] = label(row, 7, label_y, 14, 18, L_FONT_SYMBOL);
        s_names[i] = label(row, 26, label_y, 110, 18, L_FONT_TEXT);
        s_values[i] = label(row, width - 16 - 62, label_y, 54, 18, L_FONT_TEXT);
        if (!s_cursors[i] || !s_markers[i] || !s_names[i] || !s_values[i]) goto fail;
        // Fixed, disjoint columns; English names and edit values cannot overlap.
        lv_obj_set_width(s_names[i], width - 16 - 26 - 62 - 4);
        lv_obj_set_style_text_align(s_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        if (skin->separators && i < ROW_COUNT - 1) {
            if (!box(s_panel, 34, row_start + (i + 1) * (row_height + 4) - 2, width - 50, 1, c->border)) goto fail;
        }
        if (skin->value_bars && i < 2) {
            s_bars[i] = lv_bar_create(row);
            if (!s_bars[i]) goto fail;
            lv_obj_set_pos(s_bars[i], 26, row_height - 5);
            lv_obj_set_size(s_bars[i], width - 58, 2);
            lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(c->border), LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(c->accent), LV_PART_INDICATOR);
        }
    }
    s_open = true;
    draw();
    return;
fail:
    close_settings();
    ESP_LOGE(TAG, "Cannot allocate settings view");
}

static void adjust(int direction) {
    if (s_row == ROW_BRIGHT) {
        int next = s_bright + direction * 10;
        if (next < 10) next = 10;
        if (next > 100) next = 100;
        s_bright = next;
        bsp_display_backlight(s_bright);
    } else if (s_row == ROW_SLEEP) {
        // Preserve values such as 7 minutes until the user actually changes it.
        if (direction > 0) {
            for (unsigned i = 0; i < sizeof(k_sleep_preset); ++i)
                if (k_sleep_preset[i] > s_sleep) { s_sleep = k_sleep_preset[i]; break; }
        } else {
            for (int i = (int)sizeof(k_sleep_preset) - 1; i >= 0; --i)
                if (k_sleep_preset[i] < s_sleep) { s_sleep = k_sleep_preset[i]; break; }
        }
    }
    s_feedback = NULL;
    s_error = false;
    draw();
}

static void save_current_row(void) {
    const app_config_t *cfg = app_config_get();
    esp_err_t result = app_config_set_display(s_row == ROW_BRIGHT ? s_bright : cfg->backlight,
                                             s_row == ROW_SLEEP ? s_sleep : cfg->screen_off_min);
    if (result == ESP_OK) {
        s_edit = false;
        s_feedback = L_SETTINGS_SAVED;
        s_error = false;
        reload_saved();
    } else {
        // Keep the proposed edit and its preview; OK retries, hold cancels.
        s_feedback = L_SETTINGS_SAVE_FAILED;
        s_error = true;
        ESP_LOGW(TAG, "Display preferences not saved (%d)", result);
    }
    draw();
}

void ui_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_open) return;
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (s_edit || s_reset_armed) {
            reload_saved();
            s_edit = s_reset_armed = s_error = false;
            s_feedback = L_SETTINGS_CANCELLED;
            draw();
        } else close_settings();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (s_edit) adjust(btn == BSP_BTN_UP ? 1 : -1);
        else {
            s_reset_armed = s_error = false;
            s_feedback = NULL;
            s_row = (s_row + (btn == BSP_BTN_DOWN ? 1 : ROW_COUNT - 1)) % ROW_COUNT;
            draw();
        }
        return;
    }
    if (btn != BSP_BTN_OK) return;
    if (s_edit) { save_current_row(); return; }
    if (s_row == ROW_BRIGHT || s_row == ROW_SLEEP) {
        s_edit = true;
        s_feedback = NULL;
        s_error = false;
        draw();
    } else if (s_row == ROW_BACK) close_settings();
    else if (!s_reset_armed) {
        s_reset_armed = true;
        s_feedback = NULL;
        draw();
    } else {
        close_settings();
        if (s_prov_cb) s_prov_cb();
    }
}

// Dedicated single-spool page for HT/external; one reusable object tree.
#include "ui_filament.h"
#include "ui_lang.h"
#include "ui_theme.h"
#include "app_config.h"
#include <stdio.h>

static lv_obj_t *s_panel, *s_title, *s_type, *s_percent, *s_swatch, *s_bar;
static lv_obj_t *s_temp, *s_humidity, *s_dry, *s_status;

static lv_obj_t *label(int y, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(s_panel);
    lv_obj_set_pos(l, 16, y);
    lv_obj_set_width(l, 192);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(ui_theme_get_colors()->text_primary), 0);
    return l;
}

void ui_filament_create(lv_obj_t *screen) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    const ui_settings_skin_t *skin = ui_theme_settings_skin();
    s_panel = lv_obj_create(screen);
    lv_obj_set_pos(s_panel, 8, 34);
    lv_obj_set_size(s_panel, 224, 252);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_set_style_radius(s_panel, skin->flat ? 0 : c->radius, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_width(s_panel, skin->frame_border, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(c->border), 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    if (skin->hard_shadow) {
        lv_obj_set_style_shadow_color(s_panel, lv_color_hex(c->border), 0);
        lv_obj_set_style_shadow_width(s_panel, 0, 0);
        lv_obj_set_style_shadow_spread(s_panel, 2, 0);
        lv_obj_set_style_shadow_offset_x(s_panel, 3, 0);
        lv_obj_set_style_shadow_offset_y(s_panel, 3, 0);
    }
    lv_obj_t *rule = lv_obj_create(s_panel);
    lv_obj_remove_style_all(rule);
    lv_obj_set_pos(rule, 16, 37);
    lv_obj_set_size(rule, 192, app_config_get()->ui_style == STYLE_SSD ? 2 : 1);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(c->accent), 0);
    s_title = label(10, L_FONT_TEXT_BIG);
    s_swatch = lv_obj_create(s_panel);
    lv_obj_set_pos(s_swatch, 16, 46);
    lv_obj_set_size(s_swatch, 28, 28);
    lv_obj_set_style_radius(s_swatch, c->radius, 0);
    s_type = label(49, L_FONT_TEXT_BIG);
    lv_obj_set_pos(s_type, 54, 49);
    lv_obj_set_width(s_type, 150);
    s_percent = label(83, L_FONT_NUM_BIG);
    s_bar = lv_bar_create(s_panel);
    lv_obj_set_pos(s_bar, 16, 123);
    lv_obj_set_size(s_bar, 192, 6);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(c->border), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
    s_temp = label(145, L_FONT_TEXT);
    s_humidity = label(173, L_FONT_TEXT);
    s_dry = label(201, L_FONT_TEXT);
    s_status = label(225, L_FONT_TEXT);
    if (skin->separators) {
        for (int y = 168; y <= 224; y += 28) {
            lv_obj_t *line = lv_obj_create(s_panel);
            lv_obj_remove_style_all(line);
            lv_obj_set_pos(line, 16, y);
            lv_obj_set_size(line, 192, 1);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(line, lv_color_hex(c->border), 0);
        }
    }
    ui_filament_hide();
}

void ui_filament_show(const bambu_state_t *st, int idx) {
    if (!s_panel) return;
    const bambu_ams_unit_t *u = idx >= 0 ? &st->units[idx] : NULL;
    const bambu_ams_tray_t *t = u ? &u->slots[0] : &st->vt_tray;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char text[80];
    if (u) snprintf(text, sizeof(text), "%s %d", bambu_ams_kind_name(u->kind), u->id >= 128 ? u->id - 127 : u->id + 1);
    else snprintf(text, sizeof(text), "%s", L_EXT);
    lv_label_set_text(s_title, text);
    lv_label_set_text(s_type, t->type[0] ? t->type : L_EMPTY);
    ui_theme_tray_swatch(s_swatch, t);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(t->active ? c->accent : c->border), 0);
    if (t->type[0] && t->remain >= 0) snprintf(text, sizeof(text), "%d%%", (int)t->remain);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(s_percent, text);
    lv_bar_set_value(s_bar, t->type[0] && t->remain >= 0 ? t->remain : 0, LV_ANIM_OFF);
    if (u && u->temperature >= 0) snprintf(text, sizeof(text), "%s  %.0f°C", L_TEMPERATURE, u->temperature);
    else snprintf(text, sizeof(text), "%s  --", L_TEMPERATURE);
    lv_label_set_text(s_temp, text);
    if (u && u->humidity >= 0) snprintf(text, sizeof(text), "%s  %d%%", L_HUMIDITY, u->humidity);
    else if (u && u->humidity_level >= 1) snprintf(text, sizeof(text), "%s  L%d", L_HUMIDITY, u->humidity_level);
    else snprintf(text, sizeof(text), "%s  --", L_HUMIDITY);
    lv_label_set_text(s_humidity, text);
    if (u && u->dry_minutes >= 0) snprintf(text, sizeof(text), "%s  %dh%02dm", L_DRYING, u->dry_minutes / 60, u->dry_minutes % 60);
    else snprintf(text, sizeof(text), "%s  --", L_DRYING);
    lv_label_set_text(s_dry, text);
    const char *status = "--";
    if (u) switch (u->dry_state) {
        case 0: status = L_DRY_OFF; break;
        case 1: status = L_DRY_CHECKING; break;
        case 2: status = L_DRY_ACTIVE; break;
        case 3: status = L_COOLING; break;
        case 4: status = L_DRY_STOPPING; break;
        case 5:
        case 6: status = L_DRY_ERROR; break;
        default: break;
    }
    lv_label_set_text(s_status, status);
    lv_obj_set_style_text_color(s_status, lv_color_hex(u && (u->dry_state == 5 || u->dry_state == 6) ? c->error : c->text_secondary), 0);
    // External spool has no drying sensors; do not imply it should report them.
    if (!u) {
        lv_obj_add_flag(s_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_humidity, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dry, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_humidity, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_dry, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}

void ui_filament_hide(void) { if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN); }
void ui_filament_forget(void) { s_panel = NULL; }

// main/ui/ui_theme.c —— 颜色主题实现
#include "ui_theme.h"

// 深色主题（默认）
static const ui_theme_colors_t theme_dark = {
    .bg            = 0x1A1A2E,
    .card_bg       = 0x16213E,
    .text_primary  = 0xEAEAEA,
    .text_secondary= 0x8899AA,
    .accent        = 0x00D2FF,
    .success       = 0x00E676,
    .warning       = 0xFFB74D,
    .error         = 0xFF5252,
    .border        = 0x2A3A5E,
    .gauge_nozzle  = 0xFF6B35,
    .gauge_bed     = 0x00BCD4,
    .gauge_chamber = 0xAB47BC,
};

// 浅色主题
static const ui_theme_colors_t theme_light = {
    .bg            = 0xF5F5F5,
    .card_bg       = 0xFFFFFF,
    .text_primary  = 0x212121,
    .text_secondary= 0x757575,
    .accent        = 0x1976D2,
    .success       = 0x388E3C,
    .warning       = 0xF57C00,
    .error         = 0xD32F2F,
    .border        = 0xE0E0E0,
    .gauge_nozzle  = 0xE64A19,
    .gauge_bed     = 0x0097A7,
    .gauge_chamber = 0x7B1FA2,
};

// 拓竹绿主题
static const ui_theme_colors_t theme_bambu = {
    .bg            = 0x0D1117,
    .card_bg       = 0x161B22,
    .text_primary  = 0xC9D1D9,
    .text_secondary= 0x8B949E,
    .accent        = 0x00C853,
    .success       = 0x00E676,
    .warning       = 0xFFB74D,
    .error         = 0xFF5252,
    .border        = 0x30363D,
    .gauge_nozzle  = 0xFF6B35,
    .gauge_bed     = 0x00BCD4,
    .gauge_chamber = 0xAB47BC,
};

// 马卡龙 pastel 主题（可爱风默认）
static const ui_theme_colors_t theme_pastel = {
    .bg            = 0xFFF0F5,
    .card_bg       = 0xFFFFFF,
    .text_primary  = 0x4A4A6A,
    .text_secondary= 0x9B8EC4,
    .accent        = 0xFF8FAB,
    .success       = 0xB5EAD7,
    .warning       = 0xFFDAC1,
    .error         = 0xFFB7B2,
    .border        = 0xE8D5F5,
    .gauge_nozzle  = 0xFF9AA2,
    .gauge_bed     = 0xB5EAD7,
    .gauge_chamber = 0xC7CEEA,
};

const ui_theme_colors_t *ui_theme_get_colors(void) {
    switch (CFG_THEME) {
        case THEME_LIGHT:  return &theme_light;
        case THEME_BAMBU:  return &theme_bambu;
        case THEME_PASTEL: return &theme_pastel;
        default:           return &theme_dark;
    }
}

const char *ui_theme_name(void) {
    switch (CFG_THEME) {
        case THEME_LIGHT:  return "Light";
        case THEME_BAMBU:  return "Bambu";
        case THEME_PASTEL: return "Pastel";
        default:           return "Dark";
    }
}

bool ui_theme_is_cute(void) {
    return CFG_UI_STYLE == STYLE_CUTE || CFG_THEME == THEME_PASTEL;
}

bool ui_theme_is_dark(void) {
    return CFG_THEME == THEME_DARK || CFG_THEME == THEME_BAMBU;
}

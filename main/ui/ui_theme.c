// main/ui/ui_theme.c —— 6 套 UI 配色方案实现
// 只编译当前选中风格的配色，避免 unused-const-variable 警告
#include "ui_theme.h"
#include "ui_lang.h"
#include <string.h>
#include <stdlib.h>

#if CFG_UI_STYLE == STYLE_BAMBU
// 风格1: 拓竹原厂工业风 — 深蓝标题 + 白卡片 + 绿进度
static const ui_theme_colors_t theme_bambu = {
    .bg            = 0x1A2332,  // 深蓝灰底
    .card_bg       = 0xFFFFFF,  // 白色卡片
    .header_bg     = 0x1565C0,  // 拓竹蓝
    .footer_bg     = 0x2E7D32,  // 拓竹绿
    .text_primary  = 0x212121,  // 卡片内深灰字
    .text_secondary= 0x757575,  // 次要灰
    .accent        = 0x43A047,  // 绿色进度
    .success       = 0x43A047,
    .warning       = 0xF9A825,
    .error         = 0xE53935,
    .border        = 0xE0E0E0,
    .gauge_nozzle  = 0xE65100,  // 橙色喷嘴
    .gauge_bed     = 0x1565C0,  // 蓝色热床
    .gauge_chamber = 0x6A1B9A,  // 紫色腔体
    .radius        = 6,
};

#elif CFG_UI_STYLE == STYLE_CYBER
// 风格2: 赛博极简监控风 — 纯黑底 + 冰蓝霓虹
static const ui_theme_colors_t theme_cyber = {
    .bg            = 0x0A0A0A,  // 纯黑
    .card_bg       = 0x111111,  // 深灰卡片
    .header_bg     = 0x0D0D0D,  // 近黑标题栏
    .footer_bg     = 0x0D0D0D,
    .text_primary  = 0xE0E0E0,  // 浅白正文
    .text_secondary= 0x888888,  // 中灰
    .accent        = 0x00E5FF,  // 冰蓝霓虹
    .success       = 0x00E676,
    .warning       = 0xFFD600,
    .error         = 0xFF1744,
    .border        = 0x00E5FF,  // 冰蓝细边框
    .gauge_nozzle  = 0xFF6D00,
    .gauge_bed     = 0x00B0FF,
    .gauge_chamber = 0xD500F9,
    .radius        = 2,
};

#elif CFG_UI_STYLE == STYLE_SHEIKAH
// 风格3: 希卡石板风 — 深蓝科技 + 青蓝冷光
static const ui_theme_colors_t theme_sheikah = {
    .bg            = 0x0D1B2A,  // 希卡深蓝
    .card_bg       = 0x1B2838,  // 深蓝卡片
    .header_bg     = 0x0A1628,
    .footer_bg     = 0x0A1628,
    .text_primary  = 0xB0BEC5,  // 冷灰白
    .text_secondary= 0x607D8B,
    .accent        = 0x3CD3FC,  // 希卡青蓝
    .success       = 0x69F0AE,
    .warning       = 0xFFD54F,
    .error         = 0xFF5252,
    .border        = 0x1A3A5C,
    .gauge_nozzle  = 0xFF8A65,
    .gauge_bed     = 0x4FC3F7,
    .gauge_chamber = 0xBA68C8,
    .radius        = 4,
};

#elif CFG_UI_STYLE == STYLE_WHITE
// 风格4: 纯白素雅风 — 白底灰字 + 淡蓝提示
static const ui_theme_colors_t theme_white = {
    .bg            = 0xF5F5F5,  // 浅灰白
    .card_bg       = 0xFFFFFF,  // 纯白卡片
    .header_bg     = 0xEEEEEE,
    .footer_bg     = 0xEEEEEE,
    .text_primary  = 0x333333,  // 深灰字
    .text_secondary= 0x999999,
    .accent        = 0x42A5F5,  // 淡蓝
    .success       = 0x66BB6A,
    .warning       = 0xFFA726,
    .error         = 0xEF5350,
    .border        = 0xE0E0E0,
    .gauge_nozzle  = 0xFF7043,
    .gauge_bed     = 0x42A5F5,
    .gauge_chamber = 0xAB47BC,
    .radius        = 8,
};

#elif CFG_UI_STYLE == STYLE_INDUSTRIAL
// 风格5: 硬核工控风 — 深灰黑 + 绿/黄/红状态灯
static const ui_theme_colors_t theme_industrial = {
    .bg            = 0x1A1A1A,  // 深灰黑
    .card_bg       = 0x242424,  // 稍亮卡片
    .header_bg     = 0x141414,
    .footer_bg     = 0x141414,
    .text_primary  = 0xC0C0C0,  // 银灰
    .text_secondary= 0x808080,
    .accent        = 0x00C853,  // 工控绿
    .success       = 0x00C853,  // 绿=正常
    .warning       = 0xFFD600,  // 黄=警告
    .error         = 0xFF1744,  // 红=异常
    .border        = 0x404040,
    .gauge_nozzle  = 0xFF6E40,
    .gauge_bed     = 0x448AFF,
    .gauge_chamber = 0x7C4DFF,
    .radius        = 0,         // 无圆角，硬朗
};

#elif CFG_UI_STYLE == STYLE_NEON
// 风格6: 极简霓虹极客风 — 哑光黑 + 浅紫/浅青
static const ui_theme_colors_t theme_neon = {
    .bg            = 0x121212,  // 哑光黑
    .card_bg       = 0x1E1E1E,  // 深灰卡片
    .header_bg     = 0x161616,
    .footer_bg     = 0x161616,
    .text_primary  = 0xE8E8E8,
    .text_secondary= 0x9E9E9E,
    .accent        = 0xB388FF,  // 浅紫
    .success       = 0x84FFFF,  // 浅青
    .warning       = 0xFFD740,
    .error         = 0xFF8A80,
    .border        = 0x333333,
    .gauge_nozzle  = 0xFF8A65,
    .gauge_bed     = 0x80D8FF,
    .gauge_chamber = 0xEA80FC,
    .radius        = 10,
};

#else
// 默认使用 Bambu 风格
static const ui_theme_colors_t theme_bambu = {
    .bg            = 0x1A2332,
    .card_bg       = 0xFFFFFF,
    .header_bg     = 0x1565C0,
    .footer_bg     = 0x2E7D32,
    .text_primary  = 0x212121,
    .text_secondary= 0x757575,
    .accent        = 0x43A047,
    .success       = 0x43A047,
    .warning       = 0xF9A825,
    .error         = 0xE53935,
    .border        = 0xE0E0E0,
    .gauge_nozzle  = 0xE65100,
    .gauge_bed     = 0x1565C0,
    .gauge_chamber = 0x6A1B9A,
    .radius        = 6,
};
#endif

const ui_theme_colors_t *ui_theme_get_colors(void) {
#if CFG_UI_STYLE == STYLE_BAMBU
    return &theme_bambu;
#elif CFG_UI_STYLE == STYLE_CYBER
    return &theme_cyber;
#elif CFG_UI_STYLE == STYLE_SHEIKAH
    return &theme_sheikah;
#elif CFG_UI_STYLE == STYLE_WHITE
    return &theme_white;
#elif CFG_UI_STYLE == STYLE_INDUSTRIAL
    return &theme_industrial;
#elif CFG_UI_STYLE == STYLE_NEON
    return &theme_neon;
#else
    return &theme_bambu;
#endif
}

const char *ui_theme_style_name(void) {
#if CFG_UI_STYLE == STYLE_BAMBU
    return "Bambu";
#elif CFG_UI_STYLE == STYLE_CYBER
    return "Cyber";
#elif CFG_UI_STYLE == STYLE_SHEIKAH
    return "Sheikah";
#elif CFG_UI_STYLE == STYLE_WHITE
    return "White";
#elif CFG_UI_STYLE == STYLE_INDUSTRIAL
    return "Industrial";
#elif CFG_UI_STYLE == STYLE_NEON
    return "Neon";
#else
    return "Unknown";
#endif
}

// ---------------------------------------------------------------------------
// 颜色工具 (多个风格共用)
// ---------------------------------------------------------------------------
lv_color_t ui_theme_hex_color(const char *hex) {
    // tray_color 格式 "RRGGBBAA" 或 "RRGGBB", 取前 6 位
    uint32_t rgb = 0x888888;                       // 无效时灰色兜底
    if (hex && strlen(hex) >= 6) {
        char buf[7] = {0};
        memcpy(buf, hex, 6);
        rgb = (uint32_t)strtol(buf, NULL, 16);
    }
    return lv_color_hex(rgb);
}

lv_color_t ui_theme_contrast_text(uint32_t rgb) {
    // sRGB 相对亮度 (移植自 BambuHelper contrastTextColor565)
    float r = ((rgb >> 16) & 0xFF) / 255.0f;
    float g = ((rgb >>  8) & 0xFF) / 255.0f;
    float b = ( rgb        & 0xFF) / 255.0f;
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return (lum > 0.5f) ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
}

void ui_theme_tray_swatch(lv_obj_t *swatch, const bambu_ams_tray_t *t) {
    if (!swatch || !t) return;
    if (t->translucent) {
        // 透明/透光耗材: 空心描边 (淡灰框 + 微透底色), 不误显示为黑色
        lv_obj_set_style_bg_color(swatch, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_20, 0);
        lv_obj_set_style_border_width(swatch, 2, 0);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0xCCCCCC), 0);
    } else {
        // 常规耗材: 填充 tray_color 前 6 位 RGB
        uint32_t rgb = 0x888888;
        if (strlen(t->color) >= 6) {
            char buf[7] = {0};
            memcpy(buf, t->color, 6);
            rgb = (uint32_t)strtoul(buf, NULL, 16);
        }
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(rgb), 0);
    }
}

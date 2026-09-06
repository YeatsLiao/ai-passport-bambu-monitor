// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入层
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "bsp_lvgl";
static lv_display_t *s_disp;

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        .buffer_size   = (uint32_t)BSP_LCD_W * 10,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) {
    // LVGL 未就绪时直接返回失败, 避免触发 lvgl_port_lock 的
    // "lvgl_port_init must be called first" 断言崩溃
    // (启动期 MQTT/TLS 握手窗口内, ADC 按键抖动可能误触发回调)
    if (!s_disp) return false;
    return lvgl_port_lock(timeout_ms);
}
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

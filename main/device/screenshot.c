// main/device/screenshot.c —— 串口截屏 (移植自 ai-passport-sheikah-slate firmware/main/offline/screenshot.c)
//
// 串口命令 (USB-CDC, 即日志所在的 COM 口):
//   "SLATE_SCREENSHOT_PING" → 回 "@SLATE_READY 2 FAP_SCREENSHOT_V1 screenshot" (主机探测固件支持)
//   "screenshot"            → @SLATE 文本协议: 逐行 hex + CRC32, 可容忍日志穿插
//   "FAP_SCREENSHOT_V1"     → 官方二进制协议: 头行 + RGB565LE 裸流
//
// 实现要点 (踩坑记录见 ai-passport docs/reference/y2lin/serial-screenshot-protocol.md):
//   - 在 LV_EVENT_FLUSH_START 时从渲染缓冲截像素 (此时还是未字节交换的小端 RGB565,
//     esp_lvgl_port 的 swap 发生在其 flush 回调里, 晚于本事件), 无需整屏缓冲
//   - 无 REPL 的固件必须自己装 USB-Serial-JTAG 驱动, 否则读串口直接崩溃重启
//   - 二进制窗口期静默日志 (esp_log_set_vprintf 换空实现, 传完恢复原实现)
//   - 命令按子串匹配, 不依赖换行 (Windows 终端工具可能吞行尾)
//   - 失败静默: 只记日志不应答, 主机得到干净的超时

#include "screenshot.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static lv_display_t *disp;
static bool capturing, failed, official;
static uint32_t pixels, crc;
static uint16_t capture_width, capture_height;
static int64_t started;

// 环形缓冲条目保持小尺寸; 主机断开/卡住时立即停止。
// 用驱动原始写避免 VFS 对二进制像素做 CRLF 转换。
static bool send_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size) {
        size_t count = size > 256 ? 256 : size;
        if (usb_serial_jtag_write_bytes(bytes, count, pdMS_TO_TICKS(500)) != (int)count) return false;
        bytes += count;
        size -= count;
    }
    return true;
}

static int mute_log(const char *format, va_list args)
{
    (void)format;
    (void)args;
    return 0;
}

// flush 钩子: LVGL 每次刷一帧区域时把该区域像素编码发出 (持 LVGL 锁内执行)
static void flush_start(lv_event_t *event)
{
    if (!capturing || failed) return;
    const lv_area_t *area = lv_event_get_param(event);
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    if (!buf || !buf->data || width <= 0 || width > 320 || height <= 0 ||
        area->x1 < 0 || area->x2 >= capture_width || area->y1 < 0 || area->y2 >= capture_height ||
        buf->header.stride < (unsigned)width * 2 ||
        (size_t)buf->header.stride * height > buf->data_size) {
        failed = true;
        return;
    }
    static const char hex[] = "0123456789abcdef";
    char encoded[1281];
    for (int row = 0; row < height; row++) {
        if (esp_timer_get_time() - started > 15000000) { failed = true; break; }
        const uint8_t *src = buf->data + row * buf->header.stride;
        if (official) {
            // 全屏失效必须产生全宽、有序的条带, 不静默拍平意外的局部/重叠区域
            if (area->x1 != 0 || width != capture_width || pixels != (uint32_t)(area->y1 + row) * capture_width ||
                !send_bytes(src, width * 2)) { failed = true; break; }
            pixels += width;
            vTaskDelay(1);
            continue;
        }
        for (int i = 0; i < width * 2; i++) {
            encoded[i * 2] = hex[src[i] >> 4];
            encoded[i * 2 + 1] = hex[src[i] & 15];
        }
        encoded[width * 4] = 0;
        crc = esp_rom_crc32_le(crc, src, width * 2);
        pixels += width;
        char record[1344];
        int length = snprintf(record, sizeof(record), "@SLATE_ROW %" PRId32 " %" PRId32 " %d %s\n",
                              area->x1, (int32_t)(area->y1 + row), width, encoded);
        if (length < 0 || length >= (int)sizeof(record) || !send_bytes(record, length)) {
            failed = true;
            break;
        }
        vTaskDelay(1);
    }
}

static void capture(bool official_protocol)
{
    if (!bsp_lvgl_lock(1000)) { ESP_LOGW("screenshot", "Display busy"); return; }
    capture_width = lv_display_get_horizontal_resolution(disp);
    capture_height = lv_display_get_vertical_resolution(disp);
    if (lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565 ||
        !((capture_width == 320 && capture_height == 240) || (capture_width == 240 && capture_height == 320))) {
        ESP_LOGW("screenshot", "Unsupported display");
        bsp_lvgl_unlock();
        return;
    }
    pixels = crc = 0;
    failed = false;
    official = official_protocol;
    // 整个传输期间静默 ESP 日志, 传完恢复原实现, 不改任何组件的日志级别
    vprintf_like_t log_output = esp_log_set_vprintf(mute_log);
    fflush(stdout);
    if (usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000)) != ESP_OK) {
        esp_log_set_vprintf(log_output);
        bsp_lvgl_unlock();
        return;
    }
    started = esp_timer_get_time();
    char header[80];
    if (official) snprintf(header, sizeof(header), "FAP_SCREENSHOT_V1 %u %u RGB565LE 153600\n",
                           (unsigned)capture_width, (unsigned)capture_height);
    else snprintf(header, sizeof(header), "@SLATE_BEGIN 1 %u %u RGB565LE\n",
                  (unsigned)capture_width, (unsigned)capture_height);
    failed = !send_bytes(header, strlen(header));
    capturing = !failed;
    // 传输期间持续持 LVGL 锁, 防止动画/输入在条带间改变画面;
    // 正常的 SPI 面板刷写不受影响, 照常进行
    if (!failed) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(disp);
    }
    capturing = false;
    failed = failed || pixels != (uint32_t)capture_width * capture_height;
    if (!official) {
        char footer[80];
        int length = failed ? snprintf(footer, sizeof(footer), "@SLATE_ERROR incomplete_frame\n")
                            : snprintf(footer, sizeof(footer), "@SLATE_END %" PRIu32 " %08" PRIx32 "\n", pixels, crc);
        if (!send_bytes(footer, length)) failed = true;
    }
    if (usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000)) != ESP_OK) failed = true;
    esp_log_set_vprintf(log_output);
    bsp_lvgl_unlock();
    if (failed) ESP_LOGW("screenshot", "Transfer incomplete (%" PRIu32 " pixels)", pixels);
}

// 串口监听任务: 行缓冲 + 子串匹配命令 (不依赖换行终结)
static void console_task(void *arg)
{
    (void)arg;
    char command[96];
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        uint8_t data[64];
        int length = usb_serial_jtag_read_bytes(data, sizeof(data), pdMS_TO_TICKS(100));
        if (length <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        for (int i = 0; i < length; i++) {
            int c = data[i];
            if (c == '\r' || c == '\n') {
                command[used] = 0;
                used = 0; overflow = false; continue;
            }
            if (overflow) continue;
            if (used == sizeof(command) - 1) {
                overflow = true; continue;
            }
            command[used++] = (char)c;
            command[used] = 0;
            // 浏览器类主机工具可能不给命令加换行, 按子串匹配
            if (strstr(command, "SLATE_SCREENSHOT_PING")) {
                static const char ready[] = "@SLATE_READY 2 FAP_SCREENSHOT_V1 screenshot\n";
                used = 0;
                send_bytes(ready, sizeof(ready) - 1);
            }
            else if (strstr(command, "FAP_SCREENSHOT_V1")) { used = 0; capture(true); }
            else if (strstr(command, "screenshot")) { used = 0; capture(false); }
        }
    }
}

bool screenshot_init(struct _lv_display_t *display)
{
    if (disp || !display) return false;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 2048, .rx_buffer_size = 256};
        esp_err_t result = usb_serial_jtag_driver_install(&config);
        if (result != ESP_OK) {
            ESP_LOGE("screenshot", "USB driver init failed: %s", esp_err_to_name(result));
            return false;
        }
    }
    // 控制台 VFS 切到驱动通道 (日志照常走 USB-CDC, 读命令才有数据)
    usb_serial_jtag_vfs_use_driver();
    lv_display_t *d = (lv_display_t *)display;
    lv_display_add_event_cb(d, flush_start, LV_EVENT_FLUSH_START, NULL);
    disp = d;   // 先于任务创建赋值, 防止任务一启动就来命令时钩子拿到空指针
    // 该任务会经 lv_refr_now 做整屏软件渲染 (含 CJK 字体), 8KB 栈按官方经验宽裕;
    // 优先级 2 低于 LVGL 任务, 不抢渲染
    if (xTaskCreate(console_task, "screenshot", 8192, NULL, 2, NULL) != pdPASS) {
        lv_display_remove_event_cb_with_user_data(d, flush_start, NULL);
        disp = NULL;
        ESP_LOGE("screenshot", "screenshot task create failed");
        return false;
    }
    ESP_LOGI("screenshot", "USB ready: FAP_SCREENSHOT_V1 + screenshot, stack=8192");
    return true;
}

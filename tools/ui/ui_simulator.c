// Host-only harness. Links the actual theme/router/parser sources with LVGL.
#include "lvgl.h"
#include "app_config.h"
#include "bambu_report.h"
#include "bambu_mqtt.h"
#include "ui/ui_monitor.h"
#include "ui/ui_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
// Fail on stderr without invoking the Windows CRT's interactive abort dialog.
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "Assertion failed: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
    exit(1); \
} } while (0)

static app_config_t s_config;
static int64_t s_now = 5000000;
static bool s_connected = true;
static bool s_save_fail;
static int s_backlight, s_provision_calls;
static uint16_t s_pixels[240 * 320], s_framebuffer[240 * 320];
const app_config_t *app_config_get(void) { return &s_config; }
int app_config_comp_order(int *out, int max) { (void)out; (void)max; return 0; }
esp_err_t app_config_set_display(uint8_t b, uint8_t s) {
    if (s_save_fail) return ESP_FAIL;
    s_config.backlight = b; s_config.screen_off_min = s; return ESP_OK;
}
int bsp_battery_soc(void) { return 83; }
void bsp_display_backlight(int brightness) { s_backlight = brightness; }
static void provision(void) { ++s_provision_calls; }
int64_t esp_timer_get_time(void) { return s_now; }
bool bambu_mqtt_connected(void) { return s_connected; }
void bambu_mqtt_pushall(void) {}
const char *bambu_mqtt_status_str(void) { return "Connected"; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *data) {
    const uint16_t *p = (const uint16_t *)data;
    for (int y = a->y1; y <= a->y2; ++y)
        for (int x = a->x1; x <= a->x2; ++x) s_framebuffer[y * 240 + x] = *p++;
    lv_display_flush_ready(d);
}

static double luminance(uint32_t rgb) {
    double v[3];
    for (int i = 0; i < 3; ++i) {
        double c = ((rgb >> (16 - 8 * i)) & 255) / 255.0;
        v[i] = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
    }
    return v[0] * 0.2126 + v[1] * 0.7152 + v[2] * 0.0722;
}

static void check_settings_labels(lv_obj_t *root) {
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(root, &lv_label_class)) {
        const char *text = lv_label_get_text(root);
        lv_point_t needed;
        lv_text_get_size(&needed, text, lv_obj_get_style_text_font(root, 0),
                         lv_obj_get_style_text_letter_space(root, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (needed.x > lv_obj_get_width(root) || needed.y > lv_obj_get_height(root) || strstr(text, "...")) {
            fprintf(stderr, "Clipped settings text: %s (need %d x %d; have %d x %d)\n", text,
                    (int)needed.x, (int)needed.y, (int)lv_obj_get_width(root), (int)lv_obj_get_height(root));
            assert(0);
        }
        lv_area_t bounds;
        lv_obj_get_coords(root, &bounds);
        assert(bounds.x1 >= 4 && bounds.x2 < 236 && bounds.y1 >= 30 && bounds.y2 < 290);
        lv_obj_t *parent = lv_obj_get_parent(root);
        uint32_t fg = lv_color_to_u32(lv_obj_get_style_text_color(root, 0)) & 0xffffff;
        uint32_t bg = lv_color_to_u32(lv_obj_get_style_bg_color(parent, 0)) & 0xffffff;
        double a = luminance(fg), b = luminance(bg);
        double contrast = (fmax(a, b) + 0.05) / (fmin(a, b) + 0.05);
        assert(contrast >= 4.49);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        check_settings_labels(lv_obj_get_child(root, i));
}

static void render(const char *directory, const char *name) {
    for (int i = 0; i < 5; ++i) { lv_tick_inc(20); s_now += 20000; lv_timer_handler(); }
    lv_refr_now(NULL);
    if (ui_settings_is_open()) check_settings_labels(lv_obj_get_child(s_scr, -1));
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n240 320\n255\n");
    for (int i = 0; i < 240 * 320; ++i) {
        uint16_t c = s_framebuffer[i];
        uint8_t rgb[3] = {((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void click(bsp_btn_t button) { ui_monitor_key(button, BSP_BTN_CLICK); }
static void hold(void) { ui_monitor_key(BSP_BTN_OK, BSP_BTN_LONG); }
static bool visible_label(lv_obj_t *obj, const char *text) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (visible_label(lv_obj_get_child(obj, i), text)) return true;
    return false;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    s_config.ui_style = atoi(argv[1]); s_config.lang = atoi(argv[2]);
    s_config.backlight = 70; s_config.screen_off_min = 5;
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_pixels, NULL, sizeof(s_pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    bambu_state_init();
    const char *report = "{\"print\":{\"printer_type\":\"O1D\",\"mc_percent\":37,\"mc_remaining_time\":68,\"layer_num\":123,\"total_layer_num\":330,\"nozzle_temper\":220,\"nozzle_target_temper\":220,\"bed_temper\":55,\"bed_target_temper\":55,\"gcode_state\":\"RUNNING\",\"spd_lvl\":2,\"spd_mag\":100,\"cooling_fan_speed\":\"15\",\"big_fan2_speed\":\"6\",\"ams\":{\"tray_now\":\"128\",\"ams\":[{\"id\":0,\"info\":\"3\",\"tray\":[{\"id\":0,\"tray_type\":\"PLA\",\"tray_color\":\"00AAFFFF\",\"remain\":76},{\"id\":1,\"tray_type\":\"PETG\",\"tray_color\":\"000000FF\",\"remain\":-1}]},{\"id\":128,\"info\":\"24\",\"temp\":65,\"humidity_raw\":12,\"dry_time\":93,\"tray\":[{\"id\":0,\"tray_type\":\"PA-CF\",\"tray_color\":\"444444FF\",\"remain\":42}]}]},\"vt_tray\":{\"id\":254,\"tray_type\":\"TPU\",\"tray_color\":\"F0A000FF\",\"remain\":-1}}}";
    assert(bambu_report_apply(report, strlen(report), s_now / 1000));
    ui_monitor_enter();
    assert(ui_monitor_page_count() == 3 && ui_monitor_current_page() == 0);
    char unknown[64];
    ui_monitor_printf(unknown, sizeof(unknown), BAMBU_VALID_CHAMBER, "%s %s %d", "X", "Chamber", 0);
    assert(!strcmp(unknown, "X Chamber --"));
    render(argv[3], "overview");
    assert(visible_label(s_scr, "H2D"));
    assert(visible_label(s_scr, "1/3"));
    ui_monitor_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ui_monitor_current_page() == 1 && ui_monitor_state()->trays[0].remain == 76);
    render(argv[3], "ams");
    assert(visible_label(s_scr, "H2D"));
    assert(visible_label(s_scr, "2/3"));
    ui_monitor_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ui_monitor_current_page() == 2 && ui_monitor_state()->ams_count == 1);
    render(argv[3], "ht");
    assert(visible_label(s_scr, "3/3"));
    // A topology update keeps the selected HT, even if a lower-ID AMS appears.
    const char *insert = "{\"print\":{\"ams\":{\"ams\":[{\"id\":1,\"tray\":[{\"id\":0,\"tray_type\":\"ABS\"}]}]}}}";
    assert(bambu_report_apply(insert, strlen(insert), s_now / 1000));
    lv_tick_inc(1100); s_now += 1100000; lv_timer_handler();
    assert(ui_monitor_current_page() == 3 && ui_monitor_page_count() == 4);
    ui_monitor_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ui_settings_is_open());
    render(argv[3], "settings");
    ui_settings_set_prov_cb(provision);
    click(BSP_BTN_OK); click(BSP_BTN_UP);
    assert(s_backlight == 80 && s_config.backlight == 70);
    render(argv[3], "settings-bright-edit");
    ui_monitor_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(s_backlight == 80 && ui_settings_is_open());
    hold();
    assert(s_backlight == 70 && s_config.backlight == 70 && ui_settings_is_open());
    render(argv[3], "settings-cancelled");
    click(BSP_BTN_OK); click(BSP_BTN_UP);
    s_save_fail = true; click(BSP_BTN_OK);
    assert(s_config.backlight == 70 && s_backlight == 80 && ui_settings_is_open());
    render(argv[3], "settings-save-failed");
    lv_tick_inc(1100); s_now += 1100000; lv_timer_handler();
    assert(s_config.backlight == 70 && s_backlight == 80);
    s_save_fail = false; click(BSP_BTN_OK);
    assert(s_config.backlight == 80 && s_backlight == 80);
    render(argv[3], "settings-saved");
    hold();
    // A saved value outside the preset list must survive OK-without-adjusting.
    s_config.screen_off_min = 7;
    click(BSP_BTN_DOWN); click(BSP_BTN_DOWN); click(BSP_BTN_OK); click(BSP_BTN_OK);
    assert(s_config.screen_off_min == 7);
    render(argv[3], "settings-sleep-custom");
    click(BSP_BTN_OK);
    for (int i = 0; i < 8; ++i) click(BSP_BTN_UP);
    assert(s_config.screen_off_min == 7);
    render(argv[3], "settings-sleep-max");
    click(BSP_BTN_OK);
    assert(s_config.screen_off_min == 60);
    click(BSP_BTN_OK);
    for (int i = 0; i < 10; ++i) click(BSP_BTN_DOWN);
    render(argv[3], "settings-sleep-off");
    click(BSP_BTN_OK);
    assert(s_config.screen_off_min == 0);
    click(BSP_BTN_DOWN); click(BSP_BTN_OK);
    assert(!s_provision_calls);
    render(argv[3], "settings-network-confirm");
    click(BSP_BTN_DOWN); click(BSP_BTN_UP); click(BSP_BTN_OK);
    assert(!s_provision_calls);
    hold();
    assert(!s_provision_calls && ui_settings_is_open());
    render(argv[3], "settings-network-cancelled");
    click(BSP_BTN_OK); click(BSP_BTN_OK);
    assert(s_provision_calls == 1 && !ui_settings_is_open());
    click(BSP_BTN_DOWN);
    for (int i = 0; i < 3; ++i) click(BSP_BTN_DOWN);
    click(BSP_BTN_OK);
    assert(!ui_settings_is_open() && ui_monitor_current_page() == 3);
    render(argv[3], "settings-return");
    // Removing the selected device takes the user to overview without invalid pointers.
    const char *remove = "{\"print\":{\"ams\":{\"ams_exist_bits\":\"3\"}}}";
    assert(bambu_report_apply(remove, strlen(remove), s_now / 1000));
    lv_tick_inc(1100); s_now += 1100000; lv_timer_handler();
    assert(ui_monitor_current_page() == 0 && ui_monitor_page_count() == 3);
    s_now += 61000000; lv_tick_inc(61000); lv_timer_handler();
    assert(!ui_monitor_data_ready());
    render(argv[3], "stale");
    s_connected = false;
    ui_monitor_show_prov_mode("Passport-ABCD");
    render(argv[3], "provisioning");
    lv_obj_t *overlay = lv_obj_get_child(s_scr, -1);
    for (uint32_t i = 0; i < lv_obj_get_child_count(overlay); ++i) {
        lv_area_t bounds;
        lv_obj_get_coords(lv_obj_get_child(overlay, i), &bounds);
        assert(bounds.x1 >= 0 && bounds.x2 < 240 && bounds.y1 >= 0 && bounds.y2 < 320);
    }
    ui_monitor_hide_setup();
    render(argv[3], "provisioning-cancelled");

    bambu_state_init();
    const char *external = "{\"print\":{\"vt_tray\":{\"tray_type\":\"TPU\",\"remain\":-1}}}";
    assert(bambu_report_apply(external, strlen(external), s_now / 1000));
    s_connected = true;
    ui_monitor_restore();
    assert(ui_monitor_page_count() == 2);
    click(BSP_BTN_DOWN);
    assert(ui_monitor_current_page() == 1);
    render(argv[3], "external-only");
    assert(visible_label(s_scr, "Bambu"));
    click(BSP_BTN_DOWN);
    assert(ui_settings_is_open());
    hold();
    printf("PASS style=%d lang=%d: overview/AMS/HT/external/settings, stable selection, removal, stale data\n", s_config.ui_style, s_config.lang);
    return 0;
}

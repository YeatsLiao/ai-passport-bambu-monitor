// components/bsp/src/bsp_i2c.c
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "bsp_i2c";

static i2c_master_bus_handle_t s_bus;

esp_err_t bsp_i2c_init(void) {
    if (s_bus) return ESP_OK;                 // 幂等
    i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_PORT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线创建失败 (%s)", esp_err_to_name(e));
        s_bus = NULL;
        return e;
    }
    ESP_LOGI(TAG, "I2C 就绪 SDA=GPIO%d SCL=GPIO%d", BSP_I2C_SDA, BSP_I2C_SCL);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_bus(void) { return s_bus; }

esp_err_t bsp_i2c_scan(void) {
    if (!s_bus) {
        ESP_LOGE(TAG, "请先成功调用 bsp_i2c_init()");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "I2C 扫描开始:");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_bus, addr, 50) == ESP_OK) {
            const char *who = (addr == BSP_I2C_CW2017_ADDR) ? "  <- CW2017 电量计" : "";
            ESP_LOGI(TAG, "  发现设备 @ 0x%02X%s", addr, who);
            found++;
        }
    }
    if (found == 0) ESP_LOGW(TAG, "  未发现任何 I2C 设备");
    else            ESP_LOGI(TAG, "I2C 扫描完成,共 %d 个设备", found);
    return ESP_OK;
}

#include "ina219.h"
#include "audio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "ina219";

#define INA219_ADDR      0x40
#define REG_BUS_V        0x02
#define REG_CURRENT      0x04
#define REG_CALIBRATION  0x05

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t write_reg(uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = { reg, (val >> 8) & 0xFF, val & 0xFF };
    return i2c_master_transmit(s_dev, buf, 3, 100);
}

static esp_err_t read_reg(uint8_t reg, uint16_t *out)
{
    uint8_t rx[2];
    esp_err_t r = i2c_master_transmit_receive(s_dev, &reg, 1, rx, 2, 100);
    if (r == ESP_OK)
        *out = ((uint16_t)rx[0] << 8) | rx[1];
    return r;
}

esp_err_t ina219_init(void)
{
    i2c_master_bus_handle_t bus = audio_get_i2c_bus();
    if (!bus) {
        ESP_LOGW(TAG, "I2C bus not ready — skipping INA219");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = INA219_ADDR,
        .scl_speed_hz    = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Cal = 0.04096 / (0.0001 A * 0.1 Ω) = 4096 → Current_LSB = 0.1 mA
    ret = write_reg(REG_CALIBRATION, 4096);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "INA219 not responding (check wiring): %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "INA219 ready at 0x%02X", INA219_ADDR);
    return ESP_OK;
}

esp_err_t ina219_read(float *voltage_v, float *current_ma, float *power_w)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint16_t bus_raw, cur_raw;
    esp_err_t r;

    r = read_reg(REG_BUS_V, &bus_raw);
    if (r != ESP_OK) return r;

    r = read_reg(REG_CURRENT, &cur_raw);
    if (r != ESP_OK) return r;

    // Bus voltage: bits [15:3], LSB = 4 mV
    *voltage_v  = (float)((bus_raw >> 3) * 4) / 1000.0f;
    // Current: signed 16-bit, LSB = 0.1 mA
    *current_ma = (float)(int16_t)cur_raw * 0.1f;
    // Power = V * I
    *power_w    = (*voltage_v) * (*current_ma) / 1000.0f;

    return ESP_OK;
}

#include "shtc3.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ======== Tunable macros ======== */
#define SHTC3_I2C_SDA_GPIO            13
#define SHTC3_I2C_SCL_GPIO            14
#define SHTC3_I2C_PORT_NUM            0
#define SHTC3_I2C_CLK_HZ              100000
#define SHTC3_I2C_TIMEOUT_MS          100
#define SHTC3_I2C_ADDR                0x70
#define SHTC3_WAKEUP_WAIT_MS          2
#define SHTC3_MEASURE_WAIT_MS         12
#define SHTC3_TEMP_OFFSET_C           3.0f
#define SHTC3_ERROR_VALUE             (-999.0f)

/* SHTC3 command set */
#define SHTC3_CMD_WAKEUP              0x3517
#define SHTC3_CMD_SLEEP               0xB098
#define SHTC3_CMD_MEASURE_T_FIRST_NCS 0x7866

static const char *TAG = "SHTC3";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_shtc3_dev = NULL;
static bool s_i2c_inited = false;

static uint8_t shtc3_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t shtc3_send_cmd(uint16_t cmd)
{
    uint8_t tx[2];
    tx[0] = (uint8_t)(cmd >> 8);
    tx[1] = (uint8_t)(cmd & 0xFF);
    return i2c_master_transmit(s_shtc3_dev, tx, sizeof(tx), SHTC3_I2C_TIMEOUT_MS);
}

esp_err_t shtc3_i2c_init(void)
{
    if (s_i2c_inited) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = SHTC3_I2C_PORT_NUM,
        .sda_io_num = SHTC3_I2C_SDA_GPIO,
        .scl_io_num = SHTC3_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_I2C_ADDR,
        .scl_speed_hz = SHTC3_I2C_CLK_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_shtc3_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    s_i2c_inited = true;
    return ESP_OK;
}

void shtc3_i2c_deinit(void)
{
    if (s_shtc3_dev) {
        i2c_master_bus_rm_device(s_shtc3_dev);
        s_shtc3_dev = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    s_i2c_inited = false;
}

i2c_master_bus_handle_t shtc3_i2c_get_bus_handle(void)
{
    return s_i2c_bus;
}

float get_current_temperature(void)
{
    if (!s_i2c_inited) {
        if (shtc3_i2c_init() != ESP_OK) {
            ESP_LOGE(TAG, "I2C init failed before temperature read");
            return SHTC3_ERROR_VALUE;
        }
    }

    esp_err_t ret = shtc3_send_cmd(SHTC3_CMD_WAKEUP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHTC3 wakeup failed: %s", esp_err_to_name(ret));
        return SHTC3_ERROR_VALUE;
    }
    vTaskDelay(pdMS_TO_TICKS(SHTC3_WAKEUP_WAIT_MS));

    ret = shtc3_send_cmd(SHTC3_CMD_MEASURE_T_FIRST_NCS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHTC3 measure command failed: %s", esp_err_to_name(ret));
        (void)shtc3_send_cmd(SHTC3_CMD_SLEEP);
        return SHTC3_ERROR_VALUE;
    }

    vTaskDelay(pdMS_TO_TICKS(SHTC3_MEASURE_WAIT_MS));

    uint8_t rx[6] = {0};
    ret = i2c_master_receive(s_shtc3_dev, rx, sizeof(rx), SHTC3_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHTC3 receive failed: %s", esp_err_to_name(ret));
        (void)shtc3_send_cmd(SHTC3_CMD_SLEEP);
        return SHTC3_ERROR_VALUE;
    }

    if (shtc3_crc8(&rx[0], 2) != rx[2]) {
        ESP_LOGE(TAG, "SHTC3 temperature CRC mismatch");
        (void)shtc3_send_cmd(SHTC3_CMD_SLEEP);
        return SHTC3_ERROR_VALUE;
    }

    /* Humidity bytes rx[3..5] are intentionally ignored by business requirement. */
    const uint16_t raw_t = ((uint16_t)rx[0] << 8) | rx[1];
    const float temperature_raw = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    const float temperature = temperature_raw - SHTC3_TEMP_OFFSET_C;

    ret = shtc3_send_cmd(SHTC3_CMD_SLEEP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHTC3 sleep failed: %s", esp_err_to_name(ret));
        return SHTC3_ERROR_VALUE;
    }

    return temperature;
}

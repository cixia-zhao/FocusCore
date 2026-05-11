#include "pcf85063.h"

#include <stdint.h>
#include <sys/time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "shtc3.h"

#define PCF85063_I2C_ADDR         0x51
#define PCF85063_I2C_TIMEOUT_MS   100

/* Time register map */
#define PCF85063_REG_SECONDS      0x04

static const char *TAG = "PCF85063";
static i2c_master_dev_handle_t s_rtc_dev = NULL;

static uint8_t dec_to_bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

esp_err_t pcf85063_init(void)
{
    if (s_rtc_dev != NULL) {
        return ESP_OK;
    }

    esp_err_t ret = shtc3_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Shared I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_master_bus_handle_t bus = shtc3_i2c_get_bus_handle();
    if (bus == NULL) {
        ESP_LOGE(TAG, "Shared I2C bus handle is NULL");
        return ESP_FAIL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Add RTC I2C device failed: %s", esp_err_to_name(ret));
        s_rtc_dev = NULL;
        return ret;
    }
    return ESP_OK;
}

esp_err_t pcf85063_set_time(const struct tm *in_time)
{
    if (in_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = pcf85063_init();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Normalize calendar fields to avoid stale/partial tm input. */
    struct tm tm_norm = *in_time;
    tm_norm.tm_isdst = -1;
    if (mktime(&tm_norm) < 0) {
        ESP_LOGE(TAG, "Invalid input tm for RTC write");
        return ESP_ERR_INVALID_ARG;
    }

    if (tm_norm.tm_year < 100 || tm_norm.tm_mon < 0 || tm_norm.tm_mon > 11 ||
        tm_norm.tm_mday < 1 || tm_norm.tm_mday > 31 || tm_norm.tm_wday < 0 || tm_norm.tm_wday > 6) {
        ESP_LOGE(TAG, "Normalized tm out of range: y=%d m=%d d=%d wd=%d",
                 tm_norm.tm_year + 1900, tm_norm.tm_mon + 1, tm_norm.tm_mday, tm_norm.tm_wday);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG,
             "RTC write request: %04d-%02d-%02d %02d:%02d:%02d wday=%d",
             tm_norm.tm_year + 1900,
             tm_norm.tm_mon + 1,
             tm_norm.tm_mday,
             tm_norm.tm_hour,
             tm_norm.tm_min,
             tm_norm.tm_sec,
             tm_norm.tm_wday);

    uint8_t tx[8] = {0};
    tx[0] = PCF85063_REG_SECONDS;
    tx[1] = dec_to_bcd((uint8_t)tm_norm.tm_sec) & 0x7F; /* Ensure OS bit cleared */
    tx[2] = dec_to_bcd((uint8_t)tm_norm.tm_min) & 0x7F;
    tx[3] = dec_to_bcd((uint8_t)tm_norm.tm_hour) & 0x3F;
    tx[4] = dec_to_bcd((uint8_t)tm_norm.tm_mday) & 0x3F;
    tx[5] = dec_to_bcd((uint8_t)tm_norm.tm_wday) & 0x07;
    tx[6] = dec_to_bcd((uint8_t)(tm_norm.tm_mon + 1)) & 0x1F;
    tx[7] = dec_to_bcd((uint8_t)((tm_norm.tm_year + 1900) % 100));

    ret = i2c_master_transmit(s_rtc_dev, tx, sizeof(tx), PCF85063_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC write time failed: %s", esp_err_to_name(ret));
        return ret;
    }

    struct tm verify_tm = {0};
    ret = pcf85063_read_time(&verify_tm);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "RTC readback verify: %04d-%02d-%02d %02d:%02d:%02d wday=%d",
                 verify_tm.tm_year + 1900,
                 verify_tm.tm_mon + 1,
                 verify_tm.tm_mday,
                 verify_tm.tm_hour,
                 verify_tm.tm_min,
                 verify_tm.tm_sec,
                 verify_tm.tm_wday);
    } else {
        ESP_LOGE(TAG, "RTC readback verify failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t pcf85063_read_time(struct tm *out_time)
{
    if (out_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = pcf85063_init();
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t reg = PCF85063_REG_SECONDS;
    uint8_t data[7] = {0};

    ret = i2c_master_transmit_receive(s_rtc_dev, &reg, 1, data, sizeof(data), PCF85063_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (data[0] & 0x80U) {
        ESP_LOGW(TAG, "RTC oscillator stop (OS); sync time via BLE — not fabricating calendar");
        return ESP_ERR_INVALID_STATE;
    }

    struct tm t = {0};
    t.tm_sec = bcd_to_dec(data[0] & 0x7F);
    t.tm_min = bcd_to_dec(data[1] & 0x7F);
    t.tm_hour = bcd_to_dec(data[2] & 0x3F);
    t.tm_mday = bcd_to_dec(data[3] & 0x3F);
    t.tm_wday = bcd_to_dec(data[4] & 0x07);
    t.tm_mon = (int)bcd_to_dec(data[5] & 0x1F) - 1;
    t.tm_year = (int)bcd_to_dec(data[6]) + 100; /* 2000 + year -> struct tm from 1900 */
    t.tm_isdst = -1;

    *out_time = t;
    return ESP_OK;
}

esp_err_t pcf85063_sync_system_time(void)
{
    struct tm rtc_tm = {0};
    const esp_err_t ret = pcf85063_read_time(&rtc_tm);
    if (ret != ESP_OK) {
        /* OS flag or I2C failure: do not seed RTC with build/system time (misleading UI). */
        return ret;
    }

    time_t ts = mktime(&rtc_tm);
    if (ts <= 0) {
        ESP_LOGE(TAG, "mktime failed for RTC time");
        return ESP_FAIL;
    }

    struct timeval tv = {
        .tv_sec = ts,
        .tv_usec = 0
    };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "System time synchronized from RTC");
    return ESP_OK;
}

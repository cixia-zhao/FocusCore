#include "battery_monitor.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

/* ======== Tunable hardware/software macros ======== */
#define BATTERY_ADC_GPIO                  GPIO_NUM_4
#define BATTERY_DIVIDER_MULTIPLIER        3.0f
#define BATTERY_SAMPLE_COUNT              10
#define BATTERY_EMPTY_VOLTAGE             3.3f
#define BATTERY_FULL_VOLTAGE              4.2f
#define BATTERY_ADC_ATTEN                 ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH              ADC_BITWIDTH_DEFAULT

static const char *TAG = "BATTERY_MON";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static adc_channel_t s_adc_channel = ADC_CHANNEL_0;
static adc_unit_t s_adc_unit = ADC_UNIT_1;
static bool s_battery_inited = false;
static bool s_cali_enabled = false;

static int battery_mv_to_percentage(int mv)
{
    const float voltage = (float)mv / 1000.0f;
    const float ratio = (voltage - BATTERY_EMPTY_VOLTAGE) /
                        (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE);
    int percentage = (int)(ratio * 100.0f + 0.5f);
    if (percentage < 0) {
        percentage = 0;
    } else if (percentage > 100) {
        percentage = 100;
    }
    return percentage;
}

esp_err_t battery_monitor_init(void)
{
    if (s_battery_inited) {
        return ESP_OK;
    }

    esp_err_t ret = adc_oneshot_io_to_channel(BATTERY_ADC_GPIO, &s_adc_unit, &s_adc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d is not ADC capable: %s", (int)BATTERY_ADC_GPIO, esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = s_adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_adc_unit,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
    if (ret == ESP_OK) {
        s_cali_enabled = true;
    } else {
        s_cali_enabled = false;
        ESP_LOGW(TAG, "Curve fitting calibration unavailable: %s", esp_err_to_name(ret));
    }

    s_battery_inited = true;
    return ESP_OK;
}

float get_battery_voltage(void)
{
    if (!s_battery_inited || s_adc_handle == NULL) {
        return -1.0f;
    }

    int raw = 0;
    int raw_sum = 0;
    int raw_max = -1;
    int raw_min = 4096;
    int valid_count = 0;

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        if (adc_oneshot_read(s_adc_handle, s_adc_channel, &raw) != ESP_OK) {
            continue;
        }
        raw_sum += raw;
        if (raw > raw_max) {
            raw_max = raw;
        }
        if (raw < raw_min) {
            raw_min = raw;
        }
        valid_count++;
    }

    if (valid_count < 3) {
        return -1.0f;
    }

    const int trimmed_sum = raw_sum - raw_max - raw_min;
    const int trimmed_avg = trimmed_sum / (valid_count - 2);

    int pin_mv = 0;
    if (s_cali_enabled) {
        if (adc_cali_raw_to_voltage(s_adc_cali_handle, trimmed_avg, &pin_mv) != ESP_OK) {
            return -1.0f;
        }
    } else {
        /* Fallback only when calibration is unavailable. */
        pin_mv = trimmed_avg * 3300 / 4095;
    }

    const float battery_mv = (float)pin_mv * BATTERY_DIVIDER_MULTIPLIER;
    return battery_mv / 1000.0f;
}

int get_battery_percentage(void)
{
    const float battery_voltage = get_battery_voltage();
    if (battery_voltage < 0.0f) {
        return -1;
    }
    const int battery_mv = (int)(battery_voltage * 1000.0f + 0.5f);
    return battery_mv_to_percentage(battery_mv);
}

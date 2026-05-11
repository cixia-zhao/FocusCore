#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf85063_init(void);
/**
 * Read RTC calendar registers.
 * Returns ESP_ERR_INVALID_STATE if oscillator-stop (OS) flag is set (power lost); *out_time is unchanged.
 * Returns the underlying I2C error if the bus transfer fails.
 */
esp_err_t pcf85063_read_time(struct tm *out_time);
esp_err_t pcf85063_set_time(const struct tm *in_time);
esp_err_t pcf85063_sync_system_time(void);

#ifdef __cplusplus
}
#endif

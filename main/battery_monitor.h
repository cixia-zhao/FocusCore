#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t battery_monitor_init(void);
float get_battery_voltage(void);
int get_battery_percentage(void);

#ifdef __cplusplus
}
#endif

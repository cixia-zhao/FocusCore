#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t shtc3_i2c_init(void);
void shtc3_i2c_deinit(void);
i2c_master_bus_handle_t shtc3_i2c_get_bus_handle(void);
float get_current_temperature(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MANAGER_STATE_CONNECTING = 0,
    WIFI_MANAGER_STATE_CONNECTED,
    WIFI_MANAGER_STATE_FAILED
} wifi_manager_state_t;

esp_err_t wifi_manager_start(const char *ssid, const char *password);
esp_err_t wifi_manager_stop(void);
wifi_manager_state_t wifi_manager_get_state(void);

#ifdef __cplusplus
}
#endif

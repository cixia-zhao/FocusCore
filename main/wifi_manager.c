#include "wifi_manager.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, format, ...)            \
    do {                                                    \
        esp_err_t __err_rc = (x);                           \
        if (__err_rc != ESP_OK) {                           \
            ESP_LOGE((tag), format ": %s", ##__VA_ARGS__,   \
                     esp_err_to_name(__err_rc));            \
            return __err_rc;                                \
        }                                                   \
    } while (0)
#endif

static const char *TAG = "FOCUS_CORE";

static wifi_manager_state_t s_wifi_state = WIFI_MANAGER_STATE_CONNECTING;
static esp_netif_t *s_sta_netif = NULL;
static bool s_handlers_registered = false;
static bool s_wifi_stop_requested = false;

static void wifi_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            s_wifi_state = WIFI_MANAGER_STATE_CONNECTING;
            ESP_LOGI(TAG, "Wi-Fi station started, connecting...");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_wifi_stop_requested) {
                s_wifi_state = WIFI_MANAGER_STATE_FAILED;
                ESP_LOGI(TAG, "Wi-Fi stopped by request");
                return;
            }
            s_wifi_state = WIFI_MANAGER_STATE_FAILED;
            ESP_LOGI(TAG, "Wi-Fi disconnected, retry connecting...");
            esp_wifi_connect();
            s_wifi_state = WIFI_MANAGER_STATE_CONNECTING;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_wifi_state = WIFI_MANAGER_STATE_CONNECTED;
        ESP_LOGI(TAG, "Wi-Fi connected, got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_manager_start(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_handlers_registered) {
        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL, NULL),
            TAG,
            "register WIFI_EVENT handler failed");
        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_manager_event_handler, NULL, NULL),
            TAG,
            "register IP_EVENT handler failed");
        s_handlers_registered = true;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    s_wifi_stop_requested = false;
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    s_wifi_state = WIFI_MANAGER_STATE_CONNECTING;
    ESP_LOGI(TAG, "Wi-Fi manager started in STA mode");

    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    s_wifi_stop_requested = true;
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGI(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGI(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_wifi_state = WIFI_MANAGER_STATE_FAILED;
    ESP_LOGI(TAG, "Wi-Fi stopped");
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    return s_wifi_state;
}

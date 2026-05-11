#include "http_downloader.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    char *url;
} download_task_args_t;

typedef struct {
    uint8_t *buffer;
    size_t total_bytes;
    size_t capacity;
} download_context_t;

static const char *TAG = "HTTP_DOWNLOADER";
uint8_t *g_img_bin_data = NULL;
uint32_t g_img_bin_size = 0;
volatile bool g_img_data_ready = false;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt == NULL || evt->user_data == NULL) {
        return ESP_OK;
    }

    download_context_t *ctx = (download_context_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        size_t required = ctx->total_bytes + (size_t)evt->data_len;
        if (required > ctx->capacity) {
            size_t new_capacity = (ctx->capacity == 0) ? 2048 : ctx->capacity;
            while (new_capacity < required) {
                new_capacity *= 2;
            }

            uint8_t *new_buffer = (uint8_t *)realloc(ctx->buffer, new_capacity);
            if (new_buffer == NULL) {
                ESP_LOGI(TAG, "Out of memory while buffering HTTP data");
                return ESP_ERR_NO_MEM;
            }

            ctx->buffer = new_buffer;
            ctx->capacity = new_capacity;
        }

            memcpy(ctx->buffer + ctx->total_bytes, evt->data, (size_t)evt->data_len);
        ctx->total_bytes += (size_t)evt->data_len;
    }

    return ESP_OK;
}

static void http_download_task(void *pv_parameters)
{
    download_task_args_t *args = (download_task_args_t *)pv_parameters;
    download_context_t download_ctx = {0};
    int status_code = -1;
    int content_length = -1;

    if (args == NULL || args->url == NULL) {
        ESP_LOGI(TAG, "Invalid download task arguments");
        free(args);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = args->url,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .user_data = &download_ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGI(TAG, "Failed to init HTTP client");
        free(args->url);
        free(args);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        content_length = esp_http_client_get_content_length(client);
    } else {
        ESP_LOGI(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    ESP_LOGI(
        TAG,
        "Download done. HTTP status=%d, bytes=%u, content_length=%d",
        status_code,
        (unsigned)download_ctx.total_bytes,
        content_length);

    if (err == ESP_OK && status_code == 200 && download_ctx.total_bytes > 12) {
        g_img_bin_data = download_ctx.buffer;
        g_img_bin_size = (uint32_t)download_ctx.total_bytes;
        g_img_data_ready = true;
    } else {
        free(download_ctx.buffer);
    }

    free(args->url);
    free(args);
    vTaskDelete(NULL);
}

void start_test_download(const char *url)
{
    if (url == NULL) {
        ESP_LOGI(TAG, "URL is NULL, skip download");
        return;
    }

    download_task_args_t *args = (download_task_args_t *)malloc(sizeof(download_task_args_t));
    if (args == NULL) {
        ESP_LOGI(TAG, "Failed to allocate task args");
        return;
    }

    args->url = strdup(url);
    if (args->url == NULL) {
        ESP_LOGI(TAG, "Failed to allocate URL copy");
        free(args);
        return;
    }

    BaseType_t task_ok = xTaskCreate(http_download_task, "http_download_task", 6 * 1024, args, 4, NULL);
    if (task_ok != pdPASS) {
        ESP_LOGI(TAG, "Failed to create HTTP download task");
        free(args->url);
        free(args);
    }
}


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_heap_caps.h"

#include "display_bsp.h"
#include "lvgl_bsp.h"
#include "user_app.h"
#include "user_config.h"
#include "http_downloader.h"
#include "wifi_manager.h"
#include "ui_menu.h"
#include "ui_home.h"
#include "shtc3.h"
#include "pcf85063.h"
#include "ble_memo.h"
#include "sd_card.h"
#include "cJSON.h"

static_assert(BLE_MEMO_MAX_TODO_LINES == UI_HOME_TODO_LINE_COUNT, "BLE memo and home todo line counts must match");

DisplayPort RlcdPort(12,11,5,40,41,LCD_WIDTH,LCD_HEIGHT);
static bool s_img_rendered = false;
static lv_image_dsc_t *s_runtime_img_dsc = NULL;
static lv_obj_t *s_runtime_img_obj = NULL;
lv_obj_t * g_main_img_obj = NULL;
static lv_obj_t *s_digital_garden_screen = NULL;
static lv_obj_t *s_file_browser_screen = NULL;
static TickType_t s_last_key_release_tick = 0;
static TimerHandle_t s_key_single_click_timer = NULL;
static lv_obj_t *s_loading_label = NULL;
static int32_t s_img_y_offset = 0;

/* File browser state */
#define MAX_FILE_ITEMS 30
#define MAX_PATH_LEN 128

static char s_file_paths[MAX_FILE_ITEMS][MAX_PATH_LEN];
static int s_file_browser_idx = 0;
static lv_obj_t *s_file_list_container = NULL;
#define FILE_BTN_MAX 6
static lv_obj_t *s_file_browser_btns[FILE_BTN_MAX] = {NULL};

/* Viewmodel cache for current navigation level */
static cJSON *s_current_nodes[MAX_FILE_ITEMS];
static int s_current_list_count = 0;

/* Navigation stack for tree-based File Browser */
#define NAV_STACK_MAX 10
static cJSON *s_nav_stack[NAV_STACK_MAX];
static int s_nav_depth = 0;
static cJSON *g_index_json = NULL;

static void RuntimeImageReleaseLocked(void)
{
	/* Must be called in LVGL context (timer callback or under Lvgl_lock). */
	if(s_runtime_img_obj) {
		lv_image_set_src(s_runtime_img_obj, NULL);
		lv_obj_del(s_runtime_img_obj);
		s_runtime_img_obj = NULL;
		g_main_img_obj = NULL;
	}

	if(s_runtime_img_dsc) {
		free(s_runtime_img_dsc);
		s_runtime_img_dsc = NULL;
	}

	if(g_img_bin_data) {
		free(g_img_bin_data);
		g_img_bin_data = NULL;
	}
	g_img_bin_size = 0;
	g_img_data_ready = false;
	s_img_rendered = false;
}

static void RuntimeImageRenderLocked(void)
{
	/* Must be called in LVGL context (timer callback or under Lvgl_lock). */

	/* Remove loading indicator if present. */
	if(s_loading_label) {
		lv_obj_del(s_loading_label);
		s_loading_label = NULL;
	}

	if(!g_img_data_ready || g_img_bin_data == NULL || g_img_bin_size <= 12) {
		return;
	}

	const uint32_t pure_data_size = g_img_bin_size - 12;
	if(pure_data_size == 0 || (pure_data_size % 400) != 0) {
		RuntimeImageReleaseLocked();
		return;
	}

	/* Release old LVGL objects only; keep g_img_bin_data for rebinding. */
	if(s_runtime_img_obj) {
		lv_image_set_src(s_runtime_img_obj, NULL);
		lv_obj_del(s_runtime_img_obj);
		s_runtime_img_obj = NULL;
		g_main_img_obj = NULL;
	}
	if(s_runtime_img_dsc) {
		free(s_runtime_img_dsc);
		s_runtime_img_dsc = NULL;
	}
	s_img_rendered = false;

	lv_image_dsc_t *img_dsc = (lv_image_dsc_t *)malloc(sizeof(lv_image_dsc_t));
	if(img_dsc == NULL) {
		RuntimeImageReleaseLocked();
		return;
	}
	memset(img_dsc, 0, sizeof(lv_image_dsc_t));

	img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
	img_dsc->header.cf = LV_COLOR_FORMAT_L8;
	img_dsc->header.flags = 0;
	img_dsc->header.w = 400;
	img_dsc->header.stride = 400;
	img_dsc->header.h = pure_data_size / 400;
	img_dsc->data_size = pure_data_size;
	img_dsc->data = g_img_bin_data + 12;

	ESP_LOGI("RENDER", "Creating image object: w=%d, h=%d", img_dsc->header.w, img_dsc->header.h);

	lv_obj_t *img = lv_image_create(s_digital_garden_screen);
	if(img == NULL) {
		free(img_dsc);
		RuntimeImageReleaseLocked();
		return;
	}

	lv_image_set_src(img, img_dsc);
	lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_move_foreground(img);

	g_main_img_obj = img;
	s_runtime_img_obj = img;
	s_runtime_img_dsc = img_dsc;
	s_img_rendered = true;
	g_img_data_ready = false;
}


static void RenderCheckTimerCb(lv_timer_t *timer)
{
    (void)timer;
    if(g_img_data_ready) {
        ESP_LOGI("RENDER", "Timer caught ready flag! Rendering...");
        RuntimeImageRenderLocked();
    }
}

#if !DEV_MODE_NO_WIFI
static void WifiStatusTimerCb(lv_timer_t *timer)
{
    (void)timer;
    if(s_wifi_status_label == NULL) return;
    switch(wifi_manager_get_state()) {
        case WIFI_MANAGER_STATE_CONNECTING:
            lv_label_set_text(s_wifi_status_label, "WiFi: Connecting");
            break;
        case WIFI_MANAGER_STATE_CONNECTED:
            lv_label_set_text(s_wifi_status_label, "WiFi: Connected");
            break;
        case WIFI_MANAGER_STATE_FAILED:
        default:
            lv_label_set_text(s_wifi_status_label, "WiFi: Error");
            break;
    }
}
#endif

#if !DEV_MODE_NO_WIFI
static lv_obj_t *s_wifi_status_label = NULL;
static bool s_download_started = false;
#endif

static constexpr gpio_num_t PIN_ENC_A = GPIO_NUM_3;
static constexpr gpio_num_t PIN_ENC_B = GPIO_NUM_2;
static constexpr gpio_num_t PIN_KEY_BACK = GPIO_NUM_18;
static constexpr TickType_t kKeyDebounceTicks = pdMS_TO_TICKS(30);
static constexpr TickType_t kKeyLongPressTicks = pdMS_TO_TICKS(3000);
static constexpr int kPcntLowLimit = -8;
static constexpr int kPcntHighLimit = 8;
static constexpr int kPcntStepWatchNeg = -4;
static constexpr int kPcntStepWatchPos = 4;

enum class InputEventType : uint8_t {
	EncoderStep,
	KeyBackPressed,
	KeyBackLongPressed,
	KeyBackSingleClick,
	BleAdvTimeout
};

struct InputEvent {
	InputEventType type;
	int8_t encoder_step;
};

static QueueHandle_t s_input_event_queue = NULL;
static volatile TickType_t s_last_key_edge_tick = 0;
static volatile bool s_key_is_pressed = false;
static volatile bool s_key_long_press_reported = false;
static TimerHandle_t s_key_long_press_timer = NULL;
static pcnt_unit_handle_t s_pcnt_unit = NULL;
static pcnt_channel_handle_t s_pcnt_chan_a = NULL;
static pcnt_channel_handle_t s_pcnt_chan_b = NULL;
static lv_obj_t *s_menu_screen = NULL;
static lv_obj_t *s_home_screen = NULL;
static QueueHandle_t s_ble_memo_queue = NULL;
static TimerHandle_t s_ble_adv_session_timer = nullptr;

#ifndef CONFIG_FOCUSCORE_WIFI_SSID
#define CONFIG_FOCUSCORE_WIFI_SSID ""
#endif

#ifndef CONFIG_FOCUSCORE_WIFI_PASSWORD
#define CONFIG_FOCUSCORE_WIFI_PASSWORD ""
#endif

static void BleAdvSessionConsumeSuccess(void)
{
	if(s_ble_adv_session_timer != nullptr) {
		xTimerStop(s_ble_adv_session_timer, 0);
	}
	(void)ble_memo_stop_adv();
	if(Lvgl_lock(0)) {
		ui_home_ble_indicator_set_visible(false);
		Lvgl_unlock();
	}
}

static void BleAdvSessionTimeoutCb(TimerHandle_t t)
{
	(void)t;
	(void)ble_memo_stop_adv();
	InputEvent ev{};
	ev.type = InputEventType::BleAdvTimeout;
	ev.encoder_step = 0;
	(void)xQueueSend(s_input_event_queue, &ev, pdMS_TO_TICKS(10));
}

static void BleMemoUiTask(void *pvParameters)
{
	(void)pvParameters;
	ble_memo_msg_t msg = {};
	char lines[BLE_MEMO_MAX_TODO_LINES][BLE_MEMO_MAX_LEN] = {{0}};
	static const char *TAG = "BLE_TIME";

	while(1) {
		if(xQueueReceive(s_ble_memo_queue, &msg, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		/* Strict constraint: never touch I2C/RTC in GATT write callback.
		 * Time sync is handled here in the queue-consumer task.
		 */
		if(msg.len >= 5 && strncmp(msg.text, "TIME:", 5) == 0) {
			/* Memory safety: convert raw payload to a valid C string first. */
			char buffer[32] = {0};
			size_t len = msg.len;
			if(len >= sizeof(buffer)) {
				len = sizeof(buffer) - 1;
			}
			memcpy(buffer, msg.text, len);
			buffer[len] = '\0';

			int year = 0, mon = 0, mday = 0, hour = 0, min = 0, sec = 0;
			const int matched = sscanf(buffer, "TIME:%d-%d-%d-%d-%d-%d", &year, &mon, &mday, &hour, &min, &sec);
			if(matched != 6) {
				ESP_LOGW(TAG, "Invalid TIME payload: '%s'", buffer);
				continue;
			}

			if(year < 2000 || year > 2099 ||
			   mon < 1 || mon > 12 ||
			   mday < 1 || mday > 31 ||
			   hour < 0 || hour > 23 ||
			   min < 0 || min > 59 ||
			   sec < 0 || sec > 59) {
				ESP_LOGW(TAG, "TIME fields out of range: '%s'", buffer);
				continue;
			}

			struct tm tm_time = {};
			tm_time.tm_year = year - 1900;
			tm_time.tm_mon = mon - 1;
			tm_time.tm_mday = mday;
			tm_time.tm_hour = hour;
			tm_time.tm_min = min;
			tm_time.tm_sec = sec;
			tm_time.tm_isdst = -1;
			(void)mktime(&tm_time); /* Compute wday/yday (and normalize). */

			esp_err_t ret = pcf85063_set_time(&tm_time);
			if(ret != ESP_OK) {
				ESP_LOGW(TAG, "pcf85063_set_time failed: %s", esp_err_to_name(ret));
				continue;
			}

			const time_t ts = mktime(&tm_time);
			if(ts <= 0) {
				ESP_LOGW(TAG, "mktime failed for parsed time");
				continue;
			}
			struct timeval tv = {
				.tv_sec = ts,
				.tv_usec = 0
			};
			if(settimeofday(&tv, NULL) != 0) {
				ESP_LOGW(TAG, "settimeofday failed after RTC write");
				continue;
			}

			ESP_LOGI(TAG, "Time synchronized via BLE and written to RTC: %04d-%02d-%02d %02d:%02d:%02d",
			         tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
			         tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
			BleAdvSessionConsumeSuccess();
			continue;
		}

		ble_memo_split_todo_payload(msg.text, lines);
		if(Lvgl_lock(0)) {
			const char *todo_rows[BLE_MEMO_MAX_TODO_LINES];
			for(int i = 0; i < BLE_MEMO_MAX_TODO_LINES; ++i) {
				todo_rows[i] = lines[i];
			}
			ui_home_set_todo_lines(todo_rows);
			ui_home_ble_indicator_set_visible(false);
			Lvgl_unlock();
		}
		if(s_ble_adv_session_timer != nullptr) {
			xTimerStop(s_ble_adv_session_timer, 0);
		}
		(void)ble_memo_stop_adv();
	}
}

static bool IRAM_ATTR EncoderPcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
	BaseType_t high_task_woken = pdFALSE;
	QueueHandle_t queue = static_cast<QueueHandle_t>(user_ctx);
	InputEvent event{};

	if(edata->watch_point_value == kPcntStepWatchPos || edata->watch_point_value == kPcntStepWatchNeg) {
		event.type = InputEventType::EncoderStep;
		event.encoder_step = (edata->watch_point_value > 0) ? 1 : -1;
		xQueueSendFromISR(queue, &event, &high_task_woken);
		pcnt_unit_clear_count(unit);
	}

	return (high_task_woken == pdTRUE);
}

static void IRAM_ATTR KeyBackIsrHandler(void *arg)
{
	(void)arg;
	BaseType_t high_task_woken = pdFALSE;
	const TickType_t now_tick = xTaskGetTickCountFromISR();
	if((now_tick - s_last_key_edge_tick) < kKeyDebounceTicks) {
		if(high_task_woken == pdTRUE) {
			portYIELD_FROM_ISR();
		}
		return;
	}
	s_last_key_edge_tick = now_tick;

	const int level = gpio_get_level(PIN_KEY_BACK);
	if(level == 0) {
		s_key_is_pressed = true;
		s_key_long_press_reported = false;
		if(s_key_long_press_timer != NULL) {
			xTimerStopFromISR(s_key_long_press_timer, &high_task_woken);
			xTimerChangePeriodFromISR(s_key_long_press_timer, kKeyLongPressTicks, &high_task_woken);
			xTimerStartFromISR(s_key_long_press_timer, &high_task_woken);
		}
	} else {
		if(s_key_long_press_timer != NULL) {
			xTimerStopFromISR(s_key_long_press_timer, &high_task_woken);
		}
		if(s_key_is_pressed && !s_key_long_press_reported) {
			InputEvent event{};
			event.type = InputEventType::KeyBackPressed;
			event.encoder_step = 0;
			xQueueSendFromISR(s_input_event_queue, &event, &high_task_woken);
		}
		s_key_is_pressed = false;
	}
	if(high_task_woken == pdTRUE) {
		portYIELD_FROM_ISR();
	}
}

static void KeyLongPressTimerCb(TimerHandle_t timer)
{
	(void)timer;
	if(!s_key_is_pressed || s_key_long_press_reported) {
		return;
	}
	if(gpio_get_level(PIN_KEY_BACK) != 0) {
		return;
	}

	s_key_long_press_reported = true;
	InputEvent event{};
	event.type = InputEventType::KeyBackLongPressed;
	event.encoder_step = 0;
	(void)xQueueSend(s_input_event_queue, &event, 0);
}

static void KeySingleClickTimerCb(TimerHandle_t t)
{
	(void)t;
	InputEvent ev{};
	ev.type = InputEventType::KeyBackSingleClick;
	ev.encoder_step = 0;
	(void)xQueueSend(s_input_event_queue, &ev, pdMS_TO_TICKS(10));
}

static esp_err_t EncoderPcntInit(void)
{
	pcnt_unit_config_t unit_config = {};
	unit_config.low_limit = kPcntLowLimit;
	unit_config.high_limit = kPcntHighLimit;
	unit_config.intr_priority = 0;
	ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &s_pcnt_unit));

	const pcnt_glitch_filter_config_t filter_config = {
		.max_glitch_ns = 3000
	};
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_config));

	pcnt_chan_config_t chan_a_config = {};
	chan_a_config.edge_gpio_num = PIN_ENC_A;
	chan_a_config.level_gpio_num = PIN_ENC_B;

	pcnt_chan_config_t chan_b_config = {};
	chan_b_config.edge_gpio_num = PIN_ENC_B;
	chan_b_config.level_gpio_num = PIN_ENC_A;
	ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_a_config, &s_pcnt_chan_a));
	ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_b_config, &s_pcnt_chan_b));

	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(s_pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
	ESP_ERROR_CHECK(pcnt_channel_set_level_action(s_pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(s_pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
	ESP_ERROR_CHECK(pcnt_channel_set_level_action(s_pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

	ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt_unit, kPcntStepWatchNeg));
	ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt_unit, kPcntStepWatchPos));

	const pcnt_event_callbacks_t callbacks = {
		.on_reach = EncoderPcntOnReach
	};
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(s_pcnt_unit, &callbacks, s_input_event_queue));
	ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
	ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
	ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));
	/* Clear possible power-on ghost pulses after the unit starts running. */
	ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));

	return ESP_OK;
}

LV_FONT_DECLARE(ui_font_custom);
static esp_err_t load_bin_from_sd(const char *filepath);

static void render_file_browser_list(void)
{
    s_current_list_count = 0;
    memset(s_file_paths, 0, sizeof(s_file_paths));
    s_file_browser_idx = 0;

    if (s_nav_depth < 0) return;
    cJSON *current_parent = s_nav_stack[s_nav_depth];
    if (current_parent == NULL) {
        for (int i = 0; i < FILE_BTN_MAX; i++) {
            lv_obj_add_flag(s_file_browser_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    /* Traverse children via linked list, fill at most FILE_BTN_MAX slots */
    cJSON *child = current_parent->child;
    if (child == NULL) {
        for (int i = 0; i < FILE_BTN_MAX; i++) {
            lv_obj_add_flag(s_file_browser_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    /* Start from first child, walk the circular list with safety limit */
    cJSON *first_child = child;
    int _safety = 0;
    do {
        if ((++_safety) > 2000 || s_current_list_count >= FILE_BTN_MAX) break;

        const char *text_to_show = "?";
        if (cJSON_IsArray(current_parent)) {
            if(cJSON_IsObject(child)) {
                /* Manual traversal to find "title" */
                cJSON *title_node = NULL;
                cJSON *key_child = child->child;
                if(key_child) {
                    cJSON *first_key = key_child;
                    do {
                        if(key_child->string && strcmp(key_child->string, "title") == 0) {
                            title_node = key_child;
                            break;
                        }
                        key_child = key_child->next;
                    } while(key_child != first_key);
                }
                
                if (title_node && title_node->valuestring) {
                    text_to_show = title_node->valuestring;
                }
            }
        } else {
            if (child->string && strcmp(child->string, "__DEFAULT__") != 0) {
                text_to_show = child->string;
            } else if (child->string && strcmp(child->string, "__DEFAULT__") == 0) {
                /* Skip __DEFAULT__ label, descend into its children */
                cJSON *grand = child->child;
                if (grand != NULL) {
                    cJSON *first_grand = grand;
                    int _gs = 0;
                    do {
                        if ((++_gs) > 2000 || s_current_list_count >= FILE_BTN_MAX) break;
                        if (grand->string) {
                            lv_obj_t *btn = s_file_browser_btns[s_current_list_count];
                            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
                            lv_label_set_text(lbl, grand->string);
                            lv_obj_set_style_text_font(lbl, &ui_font_custom, 0);
                            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
                            snprintf(s_file_paths[s_current_list_count], MAX_PATH_LEN, "%s", grand->string);
                            s_current_nodes[s_current_list_count] = grand;
                            s_current_list_count++;
                        }
                        grand = grand->next;
                    } while (grand != first_grand && _gs <= 2000);
                }
                child = child->next;
                continue;
            }
        }

        lv_obj_t *btn = s_file_browser_btns[s_current_list_count];
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        lv_label_set_text(lbl, text_to_show);
        lv_obj_set_style_text_font(lbl, &ui_font_custom, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        snprintf(s_file_paths[s_current_list_count], MAX_PATH_LEN, "%s", text_to_show);
        s_current_nodes[s_current_list_count] = child;
        s_current_list_count++;

        child = child->next;
    } while (child != first_child && _safety <= 2000);

    /* Hide unused buttons */
    for (int i = s_current_list_count; i < FILE_BTN_MAX; i++) {
        lv_obj_add_flag(s_file_browser_btns[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_file_browser_idx = 0;

}

static void update_file_browser_highlight(void)
{
    if(!s_file_list_container) return;
    if(s_current_list_count == 0) return;

    const int32_t child_cnt = (int32_t)lv_obj_get_child_count(s_file_list_container);
    if(child_cnt == 0) return;

    if(s_file_browser_idx < 0) s_file_browser_idx = 0;
    if(s_file_browser_idx >= s_current_list_count) s_file_browser_idx = s_current_list_count - 1;

    int32_t limit = child_cnt < s_current_list_count ? child_cnt : s_current_list_count;
    for(int32_t i = 0; i < limit; i++) {
        lv_obj_t *child = lv_obj_get_child(s_file_list_container, i);
        lv_obj_t *label = lv_obj_get_child(child, 0);
        if(i == s_file_browser_idx) {
            lv_obj_set_style_bg_color(child, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_white(), 0);
            if(label) lv_obj_set_style_text_color(label, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_color(child, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_black(), 0);
            if(label) lv_obj_set_style_text_color(label, lv_color_black(), 0);
        }
    }
    if(s_current_list_count > 0) {
        lv_obj_scroll_to_view(lv_obj_get_child(s_file_list_container, s_file_browser_idx), LV_ANIM_OFF);
    }
}

static esp_err_t load_bin_from_sd(const char *filepath)
{
    static const char *TAG = "SD_LOAD";
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)file_size, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "OOM: cannot allocate %ld bytes in PSRAM", file_size);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    if ((long)bytes_read != file_size) {
        ESP_LOGE(TAG, "Read %zu bytes, expected %ld", bytes_read, file_size);
        free(buf);
        return ESP_ERR_INVALID_SIZE;
    }

    RuntimeImageReleaseLocked();
    g_img_bin_data = buf;
    g_img_bin_size = (uint32_t)file_size;
    g_img_data_ready = true;

    ESP_LOGI(TAG, "Loaded %s (%ld bytes) into PSRAM", filepath, file_size);
    return ESP_OK;
}

static void InputEventTask(void *pvParameters)
{
    (void)pvParameters;
    InputEvent event{};
    while(1) {
        if(xQueueReceive(s_input_event_queue, &event, portMAX_DELAY) != pdTRUE) continue;

        if(event.type == InputEventType::EncoderStep) {
            const int32_t delta = static_cast<int32_t>(event.encoder_step);
            if(delta == 0) continue;
            if(Lvgl_lock(0)) {
                lv_obj_t *current_scr = lv_screen_active();
                if(current_scr == s_menu_screen) {
                    ui_menu_scroll((delta > 0) ? 1 : -1);
                } else if(current_scr == s_file_browser_screen) {
                    int new_idx = s_file_browser_idx + ((delta > 0) ? 1 : -1);
                    if(new_idx < 0) new_idx = 0;
                    if(new_idx >= s_current_list_count) new_idx = s_current_list_count > 0 ? s_current_list_count - 1 : 0;
                    if(new_idx != s_file_browser_idx) {
                        s_file_browser_idx = new_idx;
                        update_file_browser_highlight();
                    }
                } else if(current_scr == s_digital_garden_screen) {
                    if(s_runtime_img_obj) {
                        lv_obj_update_layout(s_runtime_img_obj);
                        const lv_coord_t img_h = lv_obj_get_height(s_runtime_img_obj);
                        const int32_t max_scroll = (img_h > 300) ? (img_h - 300) : 0;
                        s_img_y_offset -= (int32_t)delta * 60;
                        if(s_img_y_offset > 0) s_img_y_offset = 0;
                        if(s_img_y_offset < -max_scroll) s_img_y_offset = -max_scroll;
                        lv_obj_set_y(s_runtime_img_obj, s_img_y_offset);
                    }
                }
                Lvgl_unlock();
            }

        } else if(event.type == InputEventType::KeyBackPressed) {
            TickType_t now = xTaskGetTickCount();
            if(s_key_single_click_timer == NULL) continue;

            if((now - s_last_key_release_tick) < pdMS_TO_TICKS(250)) {
                xTimerStop(s_key_single_click_timer, 0);
                s_last_key_release_tick = 0;

                if(Lvgl_lock(0)) {
                    lv_obj_t *current_scr = lv_screen_active();
                    if(current_scr == s_menu_screen) {
                        lv_screen_load(s_home_screen);
                    } else if(current_scr == s_file_browser_screen) {
                        if(s_nav_depth > 0) {
                            s_nav_depth--;
                            s_file_browser_idx = 0;
                            render_file_browser_list();
                        } else {
                            lv_screen_load(s_menu_screen);
                        }
                    } else if(current_scr == s_digital_garden_screen) {
                        RuntimeImageReleaseLocked();
                        lv_screen_load(s_file_browser_screen);
                    }
                    Lvgl_unlock();
                }
            } else {
                s_last_key_release_tick = now;
                xTimerStop(s_key_single_click_timer, 0);
                xTimerChangePeriod(s_key_single_click_timer, pdMS_TO_TICKS(250), 0);
                (void)xTimerStart(s_key_single_click_timer, 0);
            }

        } else if(event.type == InputEventType::KeyBackSingleClick) {
            s_last_key_release_tick = 0;
            if(Lvgl_lock(0)) {
                lv_obj_t *current_scr = lv_screen_active();
                if(current_scr == s_home_screen) {
                    lv_screen_load(s_menu_screen);
                } else if(current_scr == s_menu_screen) {
                    if(ui_menu_get_selected_index() == 0) {
                        lv_screen_load(s_file_browser_screen);
                    }
                } else if(current_scr == s_file_browser_screen) {
                    if(s_current_list_count == 0) { Lvgl_unlock(); continue; }
                    cJSON *current_parent = s_nav_stack[s_nav_depth];
                    cJSON *selected_node = s_current_nodes[s_file_browser_idx];
                    if(selected_node == NULL) { Lvgl_unlock(); continue; }
                    
                    /* Check if selected_node itself is an Array - treat as directory */
                    if(cJSON_IsArray(selected_node)) {
                        s_nav_stack[++s_nav_depth] = selected_node;
                        s_file_browser_idx = 0;
                        render_file_browser_list();
                        update_file_browser_highlight();
                        Lvgl_unlock();
                        continue;
                    }
                    
                    if(cJSON_IsArray(current_parent)) {
                        /* Leaf level: extract file path with polymorphic compatibility */
                        const char *file_path_str = NULL;
                        
                        if(cJSON_IsString(selected_node)) {
                            /* Branch A: selected_node itself is a string (direct .bin path/hash) */
                            file_path_str = selected_node->valuestring;
                        } else if(cJSON_IsObject(selected_node)) {
                            /* Branch B: selected_node is Object with "file" field */
                            cJSON *file_node = cJSON_GetObjectItem(selected_node, "file");
                            if(file_node != NULL && cJSON_IsString(file_node)) {
                                file_path_str = file_node->valuestring;
                            } else if(selected_node->child != NULL && selected_node->child->next == selected_node->child) {
                                /* Branch C: Object with single child, child's value is the filename */
                                cJSON *only_child = selected_node->child;
                                if(cJSON_IsString(only_child)) {
                                    file_path_str = only_child->valuestring;
                                }
                            }
                        }
                        
                        if(file_path_str == NULL) {
                            ESP_LOGE("FILE_BROWSER", "Cannot extract file path! Node type: %d", selected_node->type);
                            Lvgl_unlock();
                            continue;
                        }
                        
                        char filepath[256];
                        snprintf(filepath, sizeof(filepath), "/sdcard/_bin_output/%s", file_path_str);
                        ESP_LOGI("FILE_BROWSER", "Loading: %s", filepath);
                        s_img_y_offset = 0;
                        lv_screen_load(s_digital_garden_screen);
                        if(s_loading_label == NULL) {
                            s_loading_label = lv_label_create(s_digital_garden_screen);
                            lv_label_set_text(s_loading_label, "Loading...");
                            lv_obj_align(s_loading_label, LV_ALIGN_CENTER, 0, 0);
                        }
                        Lvgl_unlock();
                        load_bin_from_sd(filepath);
                        continue;
                    } else {
                        cJSON *target = selected_node;
                        while(cJSON_IsObject(target) && target->child != NULL && target->child->next == target->child) {
                            cJSON *only_child = target->child;
                            if(only_child && only_child->string && strcmp(only_child->string, "__DEFAULT__") == 0) {
                                target = only_child;
                            } else { break; }
                        }
                        s_nav_stack[++s_nav_depth] = target;
                        s_file_browser_idx = 0;
                        render_file_browser_list();
                        update_file_browser_highlight();
                    }
                }
                Lvgl_unlock();
            }

        } else if(event.type == InputEventType::KeyBackLongPressed) {
            if(ble_memo_start_adv() != ESP_OK) {
                ESP_LOGW("FOCUS_CORE", "BLE advertising not started (host not ready?)");
                continue;
            }
            if(Lvgl_lock(0)) {
                ui_home_ble_indicator_set_visible(true);
                ui_home_todo_set_first_line_plain("等待蓝牙同步...");
                Lvgl_unlock();
            }
            if(s_ble_adv_session_timer != nullptr) {
                xTimerStop(s_ble_adv_session_timer, 0);
                xTimerChangePeriod(s_ble_adv_session_timer, pdMS_TO_TICKS(60000), 0);
                if(xTimerStart(s_ble_adv_session_timer, 0) != pdPASS) {
                    ESP_LOGW("FOCUS_CORE", "BLE session timer start failed");
                }
            }
        } else if(event.type == InputEventType::BleAdvTimeout) {
            if(Lvgl_lock(0)) {
                ui_home_ble_indicator_set_visible(false);
                ui_home_todo_set_first_line_plain("蓝牙连接超时");
                Lvgl_unlock();
            }
        }
    }
}

static esp_err_t InputInit(void)
{
    const gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&enc_cfg));

    const gpio_config_t key_cfg = {
        .pin_bit_mask = (1ULL << PIN_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&key_cfg));

    s_input_event_queue = xQueueCreate(32, sizeof(InputEvent));
    if(s_input_event_queue == NULL) return ESP_ERR_NO_MEM;

    s_last_key_edge_tick = 0;
    s_key_is_pressed = false;
    s_key_long_press_reported = false;

    s_key_long_press_timer = xTimerCreate("key18_long",
                                          kKeyLongPressTicks, pdFALSE, NULL, KeyLongPressTimerCb);
    if(s_key_long_press_timer == NULL) return ESP_ERR_NO_MEM;

    s_ble_adv_session_timer = xTimerCreate("ble_sess",
                                           pdMS_TO_TICKS(60000), pdFALSE, nullptr, BleAdvSessionTimeoutCb);
    if(s_ble_adv_session_timer == nullptr) return ESP_ERR_NO_MEM;

    s_key_single_click_timer = xTimerCreate("key_clk",
                                            pdMS_TO_TICKS(250), pdFALSE, NULL, KeySingleClickTimerCb);
    if(s_key_single_click_timer == NULL) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(EncoderPcntInit());
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_KEY_BACK, KeyBackIsrHandler, reinterpret_cast<void *>(PIN_KEY_BACK)));

    BaseType_t task_ok = xTaskCreate(InputEventTask, "input_evt", 4096, NULL, 6, NULL);
    if(task_ok != pdPASS) return ESP_ERR_NO_MEM;

    return ESP_OK;
}

static void Lvgl_FlushCallback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
  	uint16_t *buffer = (uint16_t *)color_map;
  	for(int y = area->y1; y <= area->y2; y++) 
  	{
  	 	for(int x = area->x1; x <= area->x2; x++) 
  	 	{
  	 	   	uint8_t color = (*buffer < 0x7fff) ? ColorBlack : ColorWhite;
  	 	   	RlcdPort.RLCD_SetPixel(x, y, color);
  	 	   	buffer++;
  	 	}
  	}
  	RlcdPort.RLCD_Display();
	lv_disp_flush_ready(drv);
}



static void *psram_malloc_hook(size_t sz)
{
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
}

static esp_err_t load_index_json(void)
{
    static const char *TAG = "INDEX";

    cJSON_Hooks hooks;
    hooks.malloc_fn = psram_malloc_hook;
    hooks.free_fn = free;
    cJSON_InitHooks(&hooks);

    ESP_LOGI(TAG, "Opening /sdcard/_bin_output/index.json...");
    FILE *f = fopen("/sdcard/_bin_output/index.json", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "File open failed");
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "File size: %ld bytes", fsize);
    if (fsize <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld", fsize);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = (char *)heap_caps_malloc((size_t)fsize + 1, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "PSRAM malloc failed for %ld bytes", fsize + 1);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "PSRAM buffer allocated at %p", buf);

    size_t bytes = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    ESP_LOGI(TAG, "fread returned %zu bytes", bytes);
    if ((long)bytes != fsize) {
        ESP_LOGE(TAG, "Read mismatch: expected %ld, got %zu", fsize, bytes);
        free(buf);
        return ESP_ERR_INVALID_SIZE;
    }
    buf[fsize] = '\0';

    {
        size_t plen = (size_t)fsize < 50 ? (size_t)fsize : 50;
        ESP_LOGI(TAG, "Raw head (text): \"%.*s\"", (int)plen, buf);
    }

    g_index_json = cJSON_Parse(buf);
    free(buf);

    if (g_index_json == NULL) {
        ESP_LOGE(TAG, "cJSON_Parse failed (check hex dump above)");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "cJSON_Parse OK, root type=%d", g_index_json->type);

    cJSON *toc = cJSON_GetObjectItem(g_index_json, "toc");
    if (toc == NULL) {
        ESP_LOGE(TAG, "GetObjectItem(\"toc\") returned NULL");
    } else if (!cJSON_IsObject(toc)) {
        ESP_LOGE(TAG, "'toc' is not an object, type=%d", toc->type);
    } else {
        int n = cJSON_GetArraySize(toc);
        ESP_LOGI(TAG, "'toc' found with %d child(ren)", n);
        cJSON *first = toc->child;
        if (first && first->string) {
            ESP_LOGI(TAG, "First child key: \"%s\"", first->string);
        }
    }

    ESP_LOGI(TAG, "index.json loaded and parsed successfully");
    return ESP_OK;
}

extern "C" void app_main(void)
{
	esp_err_t ret = nvs_flash_init();
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	UserApp_AppInit();
	ret = shtc3_i2c_init();
	if(ret != ESP_OK) {
		ESP_LOGI("SHTC3", "shtc3_i2c_init failed: %s", esp_err_to_name(ret));
	}
	ret = pcf85063_sync_system_time();
	if(ret != ESP_OK) {
		ESP_LOGI("PCF85063", "RTC sync failed: %s", esp_err_to_name(ret));
	}
	ret = focuscore_sd_init();
	if(ret != ESP_OK) {
		ESP_LOGI("FOCUS_CORE", "SD card init failed: %s", esp_err_to_name(ret));
	}
		ret = load_index_json();
		if(ret != ESP_OK) {
			ESP_LOGI("FOCUS_CORE", "index.json load failed: %s", esp_err_to_name(ret));
		}
	RlcdPort.RLCD_Init();
	Lvgl_PortInit(400,300,Lvgl_FlushCallback);
	if(Lvgl_lock(-1)) {
		// Disabled demo UI creation (cat/bear images) to keep a clean solid background.
		// UserApp_UiInit();
  	  	Lvgl_unlock();
  	}
	// Disabled demo image switching task (1.5s alternation).
	// UserApp_TaskInit();

	if(Lvgl_lock(-1)) {
		s_menu_screen = lv_obj_create(NULL);
		ui_menu_create(s_menu_screen);
		s_home_screen = ui_home_create();
		ui_home_bind_menu_screen(s_menu_screen);
		lv_screen_load(s_home_screen);

		s_file_browser_screen = lv_obj_create(NULL);
		lv_obj_set_style_bg_color(s_file_browser_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
		lv_obj_set_style_bg_opa(s_file_browser_screen, LV_OPA_COVER, LV_PART_MAIN);

		s_file_list_container = lv_obj_create(s_file_browser_screen);
		lv_obj_align(s_file_list_container, LV_ALIGN_TOP_LEFT, 0, 0);
		lv_obj_set_size(s_file_list_container, 400, 300);
		lv_obj_set_style_pad_all(s_file_list_container, 0, LV_PART_MAIN);
		lv_obj_set_style_pad_row(s_file_list_container, 0, LV_PART_MAIN);
		lv_obj_set_scrollbar_mode(s_file_list_container, LV_SCROLLBAR_MODE_OFF);
		lv_obj_set_style_border_width(s_file_list_container, 0, LV_PART_MAIN);
		lv_obj_set_style_bg_opa(s_file_list_container, LV_OPA_TRANSP, LV_PART_MAIN);
		lv_obj_set_flex_flow(s_file_list_container, LV_FLEX_FLOW_COLUMN);

		/* Pre-create 6 physical buttons (never deleted, only shown/hidden) */
		for (int i = 0; i < FILE_BTN_MAX; i++) {
			s_file_browser_btns[i] = lv_obj_create(s_file_list_container);
			lv_obj_set_width(s_file_browser_btns[i], lv_pct(100));
			lv_obj_set_height(s_file_browser_btns[i], 50);
			lv_obj_set_style_border_width(s_file_browser_btns[i], 1, LV_PART_MAIN);
			lv_obj_set_style_border_color(s_file_browser_btns[i], lv_color_hex(0x000000), LV_PART_MAIN);
			lv_obj_set_style_radius(s_file_browser_btns[i], 4, LV_PART_MAIN);
			lv_obj_set_style_pad_all(s_file_browser_btns[i], 2, LV_PART_MAIN);
			lv_obj_clear_flag(s_file_browser_btns[i], LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s_file_browser_btns[i], LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s_file_browser_btns[i], LV_OBJ_FLAG_HIDDEN);
			lv_obj_t *lbl = lv_label_create(s_file_browser_btns[i]);
			lv_obj_center(lbl);
			lv_obj_set_style_text_font(lbl, &ui_font_custom, 0);
		}

		if(g_index_json != NULL) {
			s_nav_stack[0] = cJSON_GetObjectItem(g_index_json, "toc");
			s_nav_depth = 0;
		} else {
			s_nav_stack[0] = NULL;
			s_nav_depth = 0;
		}
		render_file_browser_list();

		s_digital_garden_screen = lv_obj_create(NULL);
#if !DEV_MODE_NO_WIFI
		s_wifi_status_label = lv_label_create(s_home_screen);
		lv_label_set_text(s_wifi_status_label, "WiFi: Connecting");
		lv_obj_align(s_wifi_status_label, LV_ALIGN_TOP_LEFT, 4, 4);
		lv_timer_create(WifiStatusTimerCb, 500, NULL);
#endif
		lv_timer_create(RenderCheckTimerCb, 500, NULL);

		Lvgl_unlock();
	}

#if !DEV_MODE_NO_WIFI
	ret = wifi_manager_start(CONFIG_FOCUSCORE_WIFI_SSID, CONFIG_FOCUSCORE_WIFI_PASSWORD);
	if(ret != ESP_OK) {
		ESP_LOGI("FOCUS_CORE", "Wi-Fi start failed: %s", esp_err_to_name(ret));
	}
#endif

	ret = InputInit();
	if(ret != ESP_OK) {
		ESP_LOGI("FOCUS_CORE", "Input init failed: %s", esp_err_to_name(ret));
	}

	s_ble_memo_queue = xQueueCreate(8, sizeof(ble_memo_msg_t));
	if(s_ble_memo_queue == NULL) {
		ESP_LOGI("BLE_MEMO", "Create BLE memo queue failed");
	} else {
		BaseType_t memo_task_ok = xTaskCreate(BleMemoUiTask, "ble_memo_ui", 6144, NULL, 5, NULL);
		if(memo_task_ok != pdPASS) {
			ESP_LOGI("BLE_MEMO", "Create BLE memo UI task failed");
		} else {
			ret = ble_memo_init(s_ble_memo_queue);
			if(ret != ESP_OK) {
				ESP_LOGI("BLE_MEMO", "ble_memo_init failed: %s", esp_err_to_name(ret));
			}
		}
	}

	while(1) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

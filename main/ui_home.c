#include "ui_home.h"

#include "ui_menu.h"
#include "battery_monitor.h"
#include "shtc3.h"
#include "pcf85063.h"
#include "esp_log.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

LV_FONT_DECLARE(ui_font_custom);
LV_FONT_DECLARE(ui_font_date_large);

#define SCREEN_W 400
#define SCREEN_H 300

#define STATUS_H 30
#define GAP 4

#define BODY_X 4
#define BODY_Y (STATUS_H + GAP)
#define BODY_W (SCREEN_W - (2 * GAP))
#define BODY_H (SCREEN_H - STATUS_H - (2 * GAP))

#define LEFT_W 268
#define RIGHT_W (BODY_W - LEFT_W - GAP)
#define TOP_H ((BODY_H - GAP) / 2)
#define BOTTOM_H (BODY_H - TOP_H - GAP)

static lv_obj_t *s_home_screen = NULL;
static lv_obj_t *s_menu_screen = NULL;
static lv_obj_t *s_temp_label = NULL;
static lv_obj_t *s_battery_label = NULL;
static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_week_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_year_label = NULL;
static lv_obj_t *s_ble_label = NULL;
static lv_obj_t *s_todo_labels[UI_HOME_TODO_LINE_COUNT] = {NULL};
static const char *TAG_BAT = "BAT";
static struct tm s_last_rtc_tm = {0};
static const char *kWeekdayTextCn[7] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

static void UiHomeUpdateClockDisplay(void)
{
    static int s_last_mday = -1;

    if(s_clock_label == NULL) {
        return;
    }

    struct tm tm_now = {0};
    const esp_err_t rtc_ret = pcf85063_read_time(&tm_now);
    if(rtc_ret != ESP_OK) {
        s_last_mday = -1;

        lv_label_set_text(s_clock_label, "--:--");
        if(s_date_label != NULL) {
            /* ui_font_date_large may lack space/hyphen; use full custom font for placeholders. */
            lv_obj_set_style_text_font(s_date_label, &ui_font_custom, 0);
            lv_label_set_text(s_date_label, "  月  日");
        }
        if(s_week_label != NULL) {
            lv_label_set_text(s_week_label, "--");
        }
        if(s_year_label != NULL) {
            lv_label_set_text(s_year_label, "--年");
        }
        return;
    }

    s_last_rtc_tm = tm_now;

    time_t ts = mktime(&tm_now);
    if(ts > 0) {
        struct timeval tv = {
            .tv_sec = ts,
            .tv_usec = 0
        };
        (void)settimeofday(&tv, NULL);
    }

    if(s_last_mday != s_last_rtc_tm.tm_mday) {
        if(s_week_label != NULL) {
            int wday = s_last_rtc_tm.tm_wday;
            if(wday < 0 || wday > 6) {
                wday = 0;
            }
            lv_label_set_text(s_week_label, kWeekdayTextCn[wday]);
        }
        if(s_date_label != NULL) {
            lv_obj_set_style_text_font(s_date_label, &ui_font_date_large, 0);
            lv_label_set_text_fmt(s_date_label, "%d月%d日", s_last_rtc_tm.tm_mon + 1, s_last_rtc_tm.tm_mday);
        }
        if(s_year_label != NULL) {
            lv_label_set_text_fmt(s_year_label, "%d年", s_last_rtc_tm.tm_year + 1900);
        }
        s_last_mday = s_last_rtc_tm.tm_mday;
    }

    lv_label_set_text_fmt(s_clock_label, "%02d:%02d", s_last_rtc_tm.tm_hour, s_last_rtc_tm.tm_min);
}

static void UiHomeUpdateTemperature(float temp)
{
    if(s_temp_label == NULL) {
        return;
    }
    /* Re-apply font on every update in case runtime theme/style overrides it. */
    lv_obj_set_style_text_font(s_temp_label, &ui_font_custom, 0);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0x000000), 0);
    if(temp == -999.0f || temp < -10.0f) {
        lv_label_set_text(s_temp_label, "温度: --.-°C");
    } else {
        /* Avoid %f formatting dependency in LVGL (often disabled in embedded builds). */
        int temp_x10 = (int)(temp * 10.0f + ((temp >= 0.0f) ? 0.5f : -0.5f));
        int temp_abs_x10 = (temp_x10 < 0) ? -temp_x10 : temp_x10;
        int int_part = temp_abs_x10 / 10;
        int frac_part = temp_abs_x10 % 10;
        if(temp_x10 < 0) {
            lv_label_set_text_fmt(s_temp_label, "温度: -%d.%d°C", int_part, frac_part);
        } else {
            lv_label_set_text_fmt(s_temp_label, "温度: %d.%d°C", int_part, frac_part);
        }
    }
}

static void UiHomeUpdateBatteryStatus(void)
{
    if(s_battery_label == NULL) {
        return;
    }

    const float voltage = get_battery_voltage();
    const int percent = get_battery_percentage();

    ESP_LOGI(TAG_BAT, "Voltage: %.2fV, Level: %d%%", voltage, percent);

    if(voltage < 0.0f || percent < 0) {
        lv_label_set_text(s_battery_label, "[Wi-Fi] [BAT] --%");
        return;
    }

    lv_label_set_text_fmt(s_battery_label, "[Wi-Fi] [BAT] %d%%", percent);

    if(s_ble_label != NULL && !lv_obj_has_flag(s_ble_label, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_align_to(s_ble_label, s_battery_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    }
}

static void UiHomeBatteryTimerCb(lv_timer_t *timer)
{
    (void)timer;
    if(s_temp_label != NULL) {
        const float temp = get_current_temperature();
        ESP_LOGI("SHTC3", "Temp: %.1f C", temp);
        UiHomeUpdateTemperature(temp);
    }
    UiHomeUpdateBatteryStatus();
}

static void UiHomeClockTimerCb(lv_timer_t *timer)
{
    (void)timer;
    UiHomeUpdateClockDisplay();
}

static void UiHomeApplyTodoLine(lv_obj_t *label, const char *raw)
{
    lv_obj_set_style_text_font(label, &ui_font_custom, 0);

    if(raw == NULL || raw[0] == '\0') {
        lv_label_set_text(label, "");
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_NONE, 0);
        return;
    }

    const char *display = raw;
    if(strncmp(raw, "[x]", 3) == 0) {
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        display = raw + 3;
    } else if(strncmp(raw, "[ ]", 3) == 0) {
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_NONE, 0);
        display = raw + 3;
    } else {
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_NONE, 0);
    }

    while(*display == ' ') {
        ++display;
    }

    lv_label_set_text(label, display);
}

static void UiHomeStyleCard(lv_obj_t *card)
{
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
}

static lv_obj_t *UiHomeCreateCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    UiHomeStyleCard(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_home_create(void)
{
    s_home_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_home_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(s_home_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_home_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_home_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_home_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_home_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_bar = lv_obj_create(s_home_screen);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_size(status_bar, SCREEN_W, STATUS_H);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_temp_label = lv_label_create(status_bar);
    lv_label_set_text(s_temp_label, "温度: --.-°C");
    lv_obj_set_style_text_font(s_temp_label, &ui_font_custom, 0);
    lv_obj_align(s_temp_label, LV_ALIGN_LEFT_MID, 8, 0);

    s_battery_label = lv_label_create(status_bar);
    lv_label_set_text(s_battery_label, "[Wi-Fi] [BAT] --%");
    lv_obj_align(s_battery_label, LV_ALIGN_RIGHT_MID, -8, 0);

    s_ble_label = lv_label_create(status_bar);
    lv_label_set_text(s_ble_label, "[BLE]");
    lv_obj_set_style_text_font(s_ble_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align_to(s_ble_label, s_battery_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_add_flag(s_ble_label, LV_OBJ_FLAG_HIDDEN);

    const lv_coord_t left_x = BODY_X;
    const lv_coord_t right_x = BODY_X + LEFT_W + GAP;
    const lv_coord_t top_y = BODY_Y;
    const lv_coord_t avatar_y = BODY_Y + TOP_H + GAP;

    lv_obj_t *card_clock = UiHomeCreateCard(s_home_screen, left_x, top_y, LEFT_W, TOP_H);
    lv_obj_set_style_border_width(card_clock, 0, LV_PART_MAIN);
    s_clock_label = lv_label_create(card_clock);
    lv_label_set_text(s_clock_label, "21:51");
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_48, LV_PART_MAIN);
    /* Keep time digits high in the card (offset <= 50) to free vertical space below. */
    const lv_coord_t kClockLabelTopPad = 12;
    lv_obj_align(s_clock_label, LV_ALIGN_TOP_MID, 0, kClockLabelTopPad);

    lv_obj_t *card_calendar = UiHomeCreateCard(s_home_screen, right_x, top_y, RIGHT_W, TOP_H);
    lv_obj_set_style_border_width(card_calendar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(card_calendar, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card_calendar, 10, LV_PART_MAIN);

    s_week_label = lv_label_create(card_calendar);
    lv_label_set_text(s_week_label, "星期三");
    lv_obj_set_style_text_font(s_week_label, &ui_font_custom, LV_PART_MAIN);
    lv_obj_align(s_week_label, LV_ALIGN_TOP_MID, 0, 0);
    s_date_label = lv_label_create(card_calendar);
    lv_label_set_text(s_date_label, "4月25日");
    lv_obj_set_style_text_font(s_date_label, &ui_font_date_large, LV_PART_MAIN);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 0);
    s_year_label = lv_label_create(card_calendar);
    lv_label_set_text(s_year_label, "2026年");
    lv_obj_set_style_text_font(s_year_label, &ui_font_custom, LV_PART_MAIN);
    lv_obj_align(s_year_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    const lv_coord_t bottom_left_w = (BODY_W * 3) / 5;
    const lv_coord_t bottom_right_w = BODY_W - bottom_left_w - GAP;
    const lv_coord_t bottom_right_x = left_x + bottom_left_w + GAP;

    /* To Do: absolute placement; fixed height for 400x300 (~7 lines @ 20px tight). */
    const lv_coord_t kTodoTopY = 140;
    const lv_coord_t kTodoH = 160;
    const lv_coord_t todo_w = bottom_left_w;

    lv_obj_t *card_todo = UiHomeCreateCard(s_home_screen, 0, 0, todo_w, kTodoH);
    lv_obj_align(card_todo, LV_ALIGN_TOP_LEFT, 0, kTodoTopY);
    lv_obj_set_size(card_todo, todo_w, kTodoH);
    lv_obj_set_style_border_width(card_todo, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card_todo, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card_todo, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card_todo, 0, LV_PART_MAIN);
    lv_obj_set_layout(card_todo, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card_todo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_todo, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *todo_title = lv_label_create(card_todo);
    lv_label_set_text(todo_title, "To Do List");
    lv_obj_set_style_text_font(todo_title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(todo_title, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(todo_title, 0, LV_PART_MAIN);

    for(int i = 0; i < UI_HOME_TODO_LINE_COUNT; ++i) {
        s_todo_labels[i] = lv_label_create(card_todo);
        lv_label_set_long_mode(s_todo_labels[i], LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(s_todo_labels[i], lv_pct(100));
        const char *initial = (i == 0) ? "等待蓝牙同步..." : "";
        UiHomeApplyTodoLine(s_todo_labels[i], initial);
    }

    lv_obj_t *card_avatar = UiHomeCreateCard(s_home_screen, bottom_right_x, avatar_y, bottom_right_w, BOTTOM_H);
    lv_obj_t *avatar_label = lv_label_create(card_avatar);
    lv_label_set_text(avatar_label, "[Avatar]");
    lv_obj_set_style_text_font(avatar_label, &ui_font_custom, LV_PART_MAIN);
    lv_obj_center(avatar_label);

    /* Immediate first refresh (avoid waiting for timer). */
    if(s_temp_label != NULL) {
        const float temp = get_current_temperature();
        ESP_LOGI("SHTC3", "Temp: %.1f C", temp);
        UiHomeUpdateTemperature(temp);
    }
    UiHomeUpdateClockDisplay();

    /* Battery monitor init + immediate first refresh (avoid waiting 10s). */
    if(battery_monitor_init() != ESP_OK) {
        ESP_LOGI(TAG_BAT, "battery_monitor_init failed");
        lv_label_set_text(s_battery_label, "[Wi-Fi] [BAT] --%");
    } else {
        UiHomeUpdateBatteryStatus();
    }
    lv_timer_create(UiHomeBatteryTimerCb, 10000, NULL);
    lv_timer_create(UiHomeClockTimerCb, 10000, NULL);

    return s_home_screen;
}

void ui_home_bind_menu_screen(lv_obj_t *menu_screen)
{
    s_menu_screen = menu_screen;
}

bool ui_home_route_to_menu(void)
{
    if(s_home_screen == NULL || s_menu_screen == NULL) {
        return false;
    }
    if(lv_screen_active() != s_home_screen) {
        return false;
    }
    lv_screen_load(s_menu_screen);
    return true;
}

bool ui_home_is_active(void)
{
    return (s_home_screen != NULL) && (lv_screen_active() == s_home_screen);
}

void ui_home_set_todo_lines(const char *lines[UI_HOME_TODO_LINE_COUNT])
{
    if(lines == NULL) {
        return;
    }

    for(int i = 0; i < UI_HOME_TODO_LINE_COUNT; ++i) {
        if(s_todo_labels[i] != NULL) {
            UiHomeApplyTodoLine(s_todo_labels[i], lines[i]);
        }
    }
}

void ui_home_ble_indicator_set_visible(bool visible)
{
    if(s_ble_label == NULL) {
        return;
    }
    if(visible) {
        lv_obj_remove_flag(s_ble_label, LV_OBJ_FLAG_HIDDEN);
        if(s_battery_label != NULL) {
            lv_obj_align_to(s_ble_label, s_battery_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
        }
    } else {
        lv_obj_add_flag(s_ble_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_home_todo_set_first_line_plain(const char *text)
{
    if(s_todo_labels[0] == NULL) {
        return;
    }
    lv_obj_set_style_text_font(s_todo_labels[0], &ui_font_custom, 0);
    lv_obj_set_style_text_decor(s_todo_labels[0], LV_TEXT_DECOR_NONE, 0);
    lv_label_set_text(s_todo_labels[0], text != NULL ? text : "");
}

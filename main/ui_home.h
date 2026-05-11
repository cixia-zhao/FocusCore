#ifndef UI_HOME_H
#define UI_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "lvgl.h"

/** Number of todo rows on home; must match BLE_MEMO_MAX_TODO_LINES in ble_memo.h */
#define UI_HOME_TODO_LINE_COUNT 6

/**
 * @brief Create home screen (400x300 bento layout).
 *
 * @return lv_obj_t* Home screen object.
 */
lv_obj_t *ui_home_create(void);

/**
 * @brief Bind destination menu screen for routing.
 *
 * @param menu_screen Target menu screen object.
 */
void ui_home_bind_menu_screen(lv_obj_t *menu_screen);

/**
 * @brief Route to menu screen if currently on home.
 *
 * @return true if routing happened.
 */
bool ui_home_route_to_menu(void);

/**
 * @brief Check whether active screen is home.
 */
bool ui_home_is_active(void);

/**
 * @brief Update TODO rows on home card (Obsidian-style "[ ]" / "[x]" prefixes).
 *
 * @param lines Exactly UI_HOME_TODO_LINE_COUNT pointers; NULL element treated as empty.
 */
void ui_home_set_todo_lines(const char *lines[UI_HOME_TODO_LINE_COUNT]);

/** Show or hide the status-bar "[BLE]" indicator (default hidden at boot). */
void ui_home_ble_indicator_set_visible(bool visible);

/** Set first todo row text without Obsidian prefix handling (e.g. timeout / status). */
void ui_home_todo_set_first_line_plain(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* UI_HOME_H */

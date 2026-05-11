#ifndef UI_MENU_H
#define UI_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef enum {
    UI_MENU_ITEM_HOME = 0,
    UI_MENU_ITEM_WIFI,
    UI_MENU_ITEM_DOWNLOAD,
    UI_MENU_ITEM_SETTINGS,
    UI_MENU_ITEM_ABOUT,
    UI_MENU_ITEM_COUNT
} ui_menu_item_t;

/**
 * @brief Create the main menu UI.
 *
 * @param parent Parent LVGL object (typically lv_screen_active()).
 * @return lv_obj_t* Created menu object.
 */
lv_obj_t *ui_menu_create(lv_obj_t *parent);

/**
 * @brief Scroll/highlight menu by one step from encoder direction.
 *
 * @param direction >0 means next item, <0 means previous item.
 */
void ui_menu_scroll(int direction);

/**
 * @brief Get current selected menu item index.
 *
 * @return int Current selected item index.
 */
int ui_menu_get_selected_index(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MENU_H */

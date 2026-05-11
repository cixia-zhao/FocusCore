#include "ui_menu.h"

#include <stdio.h>

#define MENU_SCREEN_WIDTH 400
#define MENU_SCREEN_HEIGHT 300
#define ICON_SIZE_NORMAL 64
#define ICON_SIZE_LARGE 96
#define MENU_ICON_COUNT UI_MENU_ITEM_COUNT

#define SLOT_TOP_Y_OFFSET 8
#define SLOT_BOTTOM_Y_OFFSET -8

#define SCALE_NORMAL 256

#define SELECTION_FRAME_WIDTH 132
#define SELECTION_FRAME_HEIGHT 110

static lv_obj_t *s_menu_container = NULL;
static lv_obj_t *s_selection_frame = NULL;
static lv_obj_t *s_menu_slots[3] = {NULL};
static int32_t current_idx = 0;

LV_IMG_DECLARE(ui_icon_note);
LV_IMG_DECLARE(ui_icon_read);
LV_IMG_DECLARE(ui_icon_photo);
LV_IMG_DECLARE(ui_icon_word);
LV_IMG_DECLARE(ui_icon_music);
LV_IMG_DECLARE(ui_icon_note_large);
LV_IMG_DECLARE(ui_icon_read_large);
LV_IMG_DECLARE(ui_icon_photo_large);
LV_IMG_DECLARE(ui_icon_word_large);
LV_IMG_DECLARE(ui_icon_music_large);

static const void *k_menu_icons_normal[MENU_ICON_COUNT] = {
    &ui_icon_note,
    &ui_icon_read,
    &ui_icon_photo,
    &ui_icon_word,
    &ui_icon_music
};

static const void *k_menu_icons_large[MENU_ICON_COUNT] = {
    &ui_icon_note_large,
    &ui_icon_read_large,
    &ui_icon_photo_large,
    &ui_icon_word_large,
    &ui_icon_music_large
};

static int32_t UiMenuNormalizeIndex(int32_t index)
{
    const int32_t count = MENU_ICON_COUNT;
    if(count <= 0) {
        return 0;
    }
    int32_t normalized = index % count;
    if(normalized < 0) {
        normalized += count;
    }
    return normalized;
}

static void UiMenuApplyVisualState(void)
{
    lv_obj_t *screen = lv_screen_active();
    if(screen != NULL) {
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
    }

    if(s_menu_container != NULL) {
        lv_obj_set_style_bg_opa(s_menu_container, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_menu_container, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_menu_container, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_menu_container, 0, LV_PART_MAIN);
    }

    if(s_selection_frame != NULL) {
        lv_obj_set_style_bg_opa(s_selection_frame, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_selection_frame, 5, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_selection_frame, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_radius(s_selection_frame, 6, LV_PART_MAIN);
    }
}

static void UiMenuRefreshWindow(void)
{
    if(s_menu_slots[0] == NULL || s_menu_slots[1] == NULL || s_menu_slots[2] == NULL) {
        return;
    }

    const int32_t top_idx = UiMenuNormalizeIndex(current_idx - 1);
    const int32_t mid_idx = UiMenuNormalizeIndex(current_idx);
    const int32_t bottom_idx = UiMenuNormalizeIndex(current_idx + 1);

    lv_image_set_src(s_menu_slots[0], k_menu_icons_normal[top_idx]);
    lv_image_set_src(s_menu_slots[1], k_menu_icons_large[mid_idx]);
    lv_image_set_src(s_menu_slots[2], k_menu_icons_normal[bottom_idx]);

    lv_image_set_scale(s_menu_slots[0], SCALE_NORMAL);
    lv_image_set_scale(s_menu_slots[1], SCALE_NORMAL);
    lv_image_set_scale(s_menu_slots[2], SCALE_NORMAL);
}

lv_obj_t *ui_menu_create(lv_obj_t *parent)
{
    if(parent == NULL) {
        return NULL;
    }

    current_idx = UiMenuNormalizeIndex(current_idx);

    s_menu_container = lv_obj_create(parent);
    lv_obj_set_size(s_menu_container, MENU_SCREEN_WIDTH, MENU_SCREEN_HEIGHT);
    lv_obj_align(s_menu_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_menu_container, LV_OBJ_FLAG_SCROLLABLE);

    s_menu_slots[0] = lv_image_create(s_menu_container);
    s_menu_slots[1] = lv_image_create(s_menu_container);
    s_menu_slots[2] = lv_image_create(s_menu_container);

    lv_obj_set_size(s_menu_slots[0], ICON_SIZE_NORMAL, ICON_SIZE_NORMAL);
    lv_obj_set_size(s_menu_slots[1], ICON_SIZE_LARGE, ICON_SIZE_LARGE);
    lv_obj_set_size(s_menu_slots[2], ICON_SIZE_NORMAL, ICON_SIZE_NORMAL);

    lv_obj_align(s_menu_slots[0], LV_ALIGN_TOP_MID, 0, SLOT_TOP_Y_OFFSET);
    lv_obj_align(s_menu_slots[1], LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(s_menu_slots[2], LV_ALIGN_BOTTOM_MID, 0, SLOT_BOTTOM_Y_OFFSET);

    s_selection_frame = lv_obj_create(lv_screen_active());
    lv_obj_add_flag(s_selection_frame, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_selection_frame, SELECTION_FRAME_WIDTH, SELECTION_FRAME_HEIGHT);
    lv_obj_add_flag(s_selection_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_selection_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_selection_frame, LV_ALIGN_CENTER, 0, 0);

    UiMenuApplyVisualState();
    UiMenuRefreshWindow();
    lv_obj_move_foreground(s_selection_frame);

    return s_menu_container;
}

void ui_menu_scroll(int direction)
{
    if(s_menu_container == NULL || MENU_ICON_COUNT <= 0) {
        return;
    }

    const int32_t delta = (direction > 0) ? 1 : ((direction < 0) ? -1 : 0);
    if(delta == 0) {
        return;
    }

    current_idx = UiMenuNormalizeIndex(current_idx + delta);
    UiMenuRefreshWindow();
}

int ui_menu_get_selected_index(void)
{
    if(s_menu_container == NULL) {
        return -1;
    }
    return (int)current_idx;
}

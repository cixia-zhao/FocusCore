#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_MEMO_MAX_LEN 256
/** Pipe-separated todo lines in one BLE write (see ble_memo_split_todo_payload). */
#define BLE_MEMO_MAX_TODO_LINES 6

typedef struct {
    uint16_t len; /* Raw payload length (not including any trailing '\0'). */
    char text[BLE_MEMO_MAX_LEN];
} ble_memo_msg_t;

esp_err_t ble_memo_init(QueueHandle_t memo_queue);

/**
 * Start undirected connectable advertising (no-op if already advertising).
 * Safe to call after host sync; checks ble_hs_synced() and ble_gap_adv_active().
 */
esp_err_t ble_memo_start_adv(void);

/**
 * Stop advertising (no-op if not advertising). Clears internal "session wants adv" flag.
 */
esp_err_t ble_memo_stop_adv(void);

/**
 * Split a memo payload on '|' into up to BLE_MEMO_MAX_TODO_LINES NUL-terminated rows.
 * Leading/trailing spaces per segment are preserved for ui_home Obsidian-prefix parsing.
 */
void ble_memo_split_todo_payload(const char *memo, char out_lines[BLE_MEMO_MAX_TODO_LINES][BLE_MEMO_MAX_LEN]);

#ifdef __cplusplus
}
#endif

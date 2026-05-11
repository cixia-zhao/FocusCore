#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t *g_img_bin_data;
extern uint32_t g_img_bin_size;
extern volatile bool g_img_data_ready;

void start_test_download(const char *url);

#ifdef __cplusplus
}
#endif

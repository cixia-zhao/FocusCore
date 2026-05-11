#include "sd_card.h"
#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "SD_CARD";

#define PIN_NUM_CLK 38
#define PIN_NUM_CMD 21
#define PIN_NUM_D0  39

esp_err_t focuscore_sd_init(void)
{
    esp_err_t ret;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = PIN_NUM_CLK;
    slot_config.cmd = PIN_NUM_CMD;
    slot_config.d0  = PIN_NUM_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint64_t capacity_mb = ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024);

    const char *type_str = "SD";
    if (card->is_mmc) {
        type_str = "MMC";
    } else if (card->is_sdio) {
        type_str = "SDIO";
    }

    ESP_LOGI(TAG, "Mounted at /sdcard | Type: %s | Capacity: %llu MB", type_str, capacity_mb);

    return ESP_OK;
}

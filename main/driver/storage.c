#include <esp_event.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <vfs_fat_internal.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include "../event.h"
#include "storage.h"

static const char * TAG = "storage";

bool diskMounted = false;
static sdmmc_slot_config_t slot_config;
static sdmmc_card_t* card;

static esp_err_t scan_mounts(void) {
    esp_err_t err;
    sdmmc_host_t host_config = SDMMC_HOST_DEFAULT();
    host_config.slot = SDMMC_HOST_SLOT_0;
    host_config.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DDR;
    host_config.command_timeout_ms = 50;
    host_config.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 128,
        .allocation_unit_size = 2048,
        .disk_status_check_enable = false
    };

    if (diskMounted) return ESP_OK;

    ESP_LOGI(TAG, "Scanning for SD card, this may hang if no card is present.");

    CHECK_CALLE(esp_vfs_fat_sdmmc_mount("/disk", &host_config, &slot_config, &mount_config, &card), "Could not mount disk");

    sdmmc_card_print_info(stdout, card);

    event_t event;
    event.type = EVENT_TYPE_DISK;
    event_push(&event);
    diskMounted = true;
    return ESP_OK;
}

ESP_EVENT_DEFINE_BASE(SD_EVENT);
enum sd_event {
    SD_EVENT_MOUNT,
    SD_EVENT_UNMOUNT
};

static TickType_t last_scan = 0;

static void IRAM_ATTR check_det(void *args) {
    if (xTaskGetTickCountFromISR() - last_scan < 100) return;
    last_scan = xTaskGetTickCountFromISR();
    if (gpio_get_level(GPIO_NUM_4)) {
        esp_event_isr_post(SD_EVENT, SD_EVENT_MOUNT, NULL, 0, NULL);
    } else {
        esp_event_isr_post(SD_EVENT, SD_EVENT_UNMOUNT, NULL, 0, NULL);
    }
}

static void mount_event(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    switch (event_id) {
        case SD_EVENT_MOUNT: scan_mounts(); return;
        case SD_EVENT_UNMOUNT:
            if (diskMounted) {
                ESP_LOGI(TAG, "Unmounting SD card");
                esp_vfs_fat_sdcard_unmount("/disk", card);
                event_t event;
                event.type = EVENT_TYPE_DISK_EJECT;
                event_push(&event);
                diskMounted = false;
            }
            return;
    }
}

esp_err_t storage_init(void) {
    esp_err_t err;
    // mount root and ROM
    ESP_LOGI(TAG, "Initializing storage module");
    ESP_LOGV(TAG, "Mounting data partition");
    esp_vfs_littlefs_conf_t conf;
    conf.base_path = "";
    conf.partition_label = "data";
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;
    conf.read_only = false;
    CHECK_CALLE(esp_vfs_littlefs_register(&conf), "Failed to mount root filesystem");
    ESP_LOGV(TAG, "Mounting ROM partition");
    conf.base_path = "/rom";
    conf.partition_label = "rom";
    conf.format_if_mount_failed = false;
    conf.read_only = true;
    CHECK_CALLE(esp_vfs_littlefs_register(&conf), "Failed to mount ROM filesystem");
    // initialize SD card driver
    ESP_LOGV(TAG, "Initializing SD cards");
    slot_config.width = 4;
    slot_config.cd = slot_config.gpio_cd = GPIO_NUM_4;
    slot_config.cmd = GPIO_NUM_5;
    slot_config.clk = GPIO_NUM_6;
    slot_config.d0 = GPIO_NUM_7;
    slot_config.d1 = GPIO_NUM_15;
    slot_config.d2 = GPIO_NUM_16;
    slot_config.d3 = GPIO_NUM_17;
    slot_config.wp = slot_config.gpio_wp = GPIO_NUM_NC;
    slot_config.flags = SDMMC_SLOT_FLAG_CD_ACTIVE_LOW;
    esp_log_level_set("sdmmc_req", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
    // initial scan for SD cards
    scan_mounts();
    // install interrupts for SD card detect
    gpio_set_intr_type(GPIO_NUM_4, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(GPIO_NUM_4, check_det, NULL);
    esp_event_handler_register(SD_EVENT, SD_EVENT_MOUNT, mount_event, NULL);
    esp_event_handler_register(SD_EVENT, SD_EVENT_UNMOUNT, mount_event, NULL);
    esp_register_shutdown_handler(storage_deinit);
    return ESP_OK;
}

void storage_deinit(void) {
    ESP_LOGV(TAG, "Deinitializing storage");
    esp_event_handler_unregister(SD_EVENT, SD_EVENT_UNMOUNT, mount_event);
    esp_event_handler_unregister(SD_EVENT, SD_EVENT_MOUNT, mount_event);
    gpio_isr_handler_remove(GPIO_NUM_4);
    if (diskMounted) esp_vfs_fat_sdcard_unmount("/disk", card);
    sdmmc_host_deinit();
    esp_vfs_littlefs_unregister("rom");
    esp_vfs_littlefs_unregister("data");
}

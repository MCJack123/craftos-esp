#include <esp_littlefs.h>
#include <esp_vfs_fat.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <vfs_fat_internal.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include "storage.h"

static const char * TAG = "storage";

/* from vfs_fat_sdmmc.c; Apache 2.0 (2015-2022 Espressif Systems (Shanghai) CO LTD) */

#define CHECK_EXECUTE_RESULT(err, str) do { \
    if ((err) !=ESP_OK) { \
        ESP_LOGE(TAG, str" (0x%x).", err); \
        goto cleanup; \
    } \
    } while(0)

typedef struct vfs_fat_sd_ctx_t {
    BYTE pdrv;                                  //Drive number that is mounted
    esp_vfs_fat_mount_config_t mount_config;    //Mount configuration
    FATFS *fs;                                  //FAT structure pointer that is registered
    sdmmc_card_t *card;                         //Card info
    char *base_path;                            //Path where partition is registered
} vfs_fat_sd_ctx_t;

static vfs_fat_sd_ctx_t *s_ctx[FF_VOLUMES] = {};
/**
 * This `s_saved_ctx_id` is only used by `esp_vfs_fat_sdmmc_unmount`, which is deprecated.
 * This variable together with `esp_vfs_fat_sdmmc_unmount` should be removed in next major version
 */
static uint32_t s_saved_ctx_id = FF_VOLUMES;

static uint32_t s_get_unused_context_id(void)
{
    for (uint32_t i = 0; i < FF_VOLUMES; i++) {
        if (!s_ctx[i]) {
            return i;
        }
    }
    return FF_VOLUMES;
}

static esp_err_t mount_prepare_mem(const char *base_path,
        BYTE *out_pdrv,
        char **out_dup_path,
        sdmmc_card_t** out_card)
{
    esp_err_t err = ESP_OK;
    char* dup_path = NULL;
    sdmmc_card_t* card = NULL;

    // connect SDMMC driver to FATFS
    BYTE pdrv = FF_DRV_NOT_USED;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == FF_DRV_NOT_USED) {
        ESP_LOGD(TAG, "the maximum count of volumes is already mounted");
        return ESP_ERR_NO_MEM;
    }

    // not using ff_memalloc here, as allocation in internal RAM is preferred
    card = (sdmmc_card_t*)malloc(sizeof(sdmmc_card_t));
    if (card == NULL) {
        ESP_LOGD(TAG, "could not locate new sdmmc_card_t");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    dup_path = strdup(base_path);
    if(!dup_path){
        ESP_LOGD(TAG, "could not copy base_path");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    *out_card = card;
    *out_pdrv = pdrv;
    *out_dup_path = dup_path;
    return ESP_OK;
cleanup:
    free(card);
    free(dup_path);
    return err;
}

static esp_err_t s_f_mount(sdmmc_card_t *card, FATFS *fs, const char *drv, uint8_t pdrv, const esp_vfs_fat_mount_config_t *mount_config)
{
    FRESULT res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        ESP_LOGW(TAG, "failed to mount card (%d)", res);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t mount_to_vfs_fat(const esp_vfs_fat_mount_config_t *mount_config, sdmmc_card_t *card, uint8_t pdrv,
                                  const char *base_path, FATFS **out_fs)
{
    FATFS *fs = NULL;
    esp_err_t err;
    ff_diskio_register_sdmmc(pdrv, card);
    ff_sdmmc_set_disk_status_check(pdrv, mount_config->disk_status_check_enable);
    ESP_LOGD(TAG, "using pdrv=%i", pdrv);
    char drv[3] = {(char)('0' + pdrv), ':', 0};

    // connect FATFS to VFS
    err = esp_vfs_fat_register(base_path, drv, mount_config->max_files, &fs);
    *out_fs = fs;
    if (err == ESP_ERR_INVALID_STATE) {
        // it's okay, already registered with VFS
    } else if (err != ESP_OK) {
        ESP_LOGD(TAG, "esp_vfs_fat_register failed 0x(%x)", err);
        goto fail;
    }

    // Try to mount partition
    err = s_f_mount(card, fs, drv, pdrv, mount_config);
    if (err != ESP_OK) {
        goto fail;
    }
    return ESP_OK;

fail:
    if (fs) {
        f_mount(NULL, drv, 0);
    }
    esp_vfs_fat_unregister_path(base_path);
    ff_diskio_unregister(pdrv);
    return err;
}

static bool diskMounted = false;

static esp_err_t scan_mounts(void)
{
    esp_err_t err;
    vfs_fat_sd_ctx_t *ctx = NULL;
    uint32_t ctx_id = FF_VOLUMES;
    FATFS *fs = NULL;
    sdmmc_card_t* card = NULL;
    BYTE pdrv = FF_DRV_NOT_USED;
    char* dup_path = NULL;
    const char* base_path = "/disk";
    const sdmmc_host_t host_config = SDMMC_HOST_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 128,
        .allocation_unit_size = 16384
    };

    if (diskMounted) return ESP_OK;

    ESP_LOGI(TAG, "Scanning for SD card, this may hang if no card is present.");

    err = mount_prepare_mem(base_path, &pdrv, &dup_path, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount_prepare failed");
        return err;
    }

    // probe and initialize card
    err = sdmmc_card_init(&host_config, card);
    CHECK_EXECUTE_RESULT(err, "sdmmc_card_init failed");

    err = mount_to_vfs_fat(&mount_config, card, pdrv, dup_path, &fs);
    CHECK_EXECUTE_RESULT(err, "mount_to_vfs failed");

    //For deprecation backward compatibility
    if (s_saved_ctx_id == FF_VOLUMES) {
        s_saved_ctx_id = 0;
    }

    ctx = calloc(sizeof(vfs_fat_sd_ctx_t), 1);
    if (!ctx) {
        CHECK_EXECUTE_RESULT(ESP_ERR_NO_MEM, "no mem");
    }
    ctx->pdrv = pdrv;
    memcpy(&ctx->mount_config, &mount_config, sizeof(esp_vfs_fat_mount_config_t));
    ctx->card = card;
    ctx->base_path = dup_path;
    ctx->fs = fs;
    ctx_id = s_get_unused_context_id();
    assert(ctx_id != FF_VOLUMES);
    s_ctx[ctx_id] = ctx;

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    diskMounted = true;
    return ESP_OK;
cleanup:
    free(card);
    free(dup_path);
    return err;
}

static esp_err_t unmount_card_core(const char *base_path, sdmmc_card_t *card)
{
    BYTE pdrv = ff_diskio_get_pdrv_card(card);
    if (pdrv == 0xff) {
        return ESP_ERR_INVALID_ARG;
    }

    // unmount
    char drv[3] = {(char)('0' + pdrv), ':', 0};
    f_mount(0, drv, 0);
    // release SD driver
    ff_diskio_unregister(pdrv);

    free(card);

    esp_err_t err = esp_vfs_fat_unregister_path(base_path);
    return err;
}

static esp_err_t esp_vfs_fat_sdmmc_unmount_(void)
{
    esp_err_t err = unmount_card_core(s_ctx[s_saved_ctx_id]->base_path, s_ctx[s_saved_ctx_id]->card);
    free(s_ctx[s_saved_ctx_id]);
    s_ctx[s_saved_ctx_id] = NULL;
    s_saved_ctx_id = FF_VOLUMES;
    return err;
}

/******************************************************************************/

static void IRAM_ATTR check_det(void *args) {
    if (gpio_get_level(GPIO_NUM_5)) {
        scan_mounts();
    } else {
        esp_vfs_fat_sdmmc_unmount_();
        diskMounted = false;
    }
}

esp_err_t storage_init(void) {
    esp_err_t err;
    // mount root and ROM
    ESP_LOGD(TAG, "Initializing storage module");
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
    sdmmc_slot_config_t slot_config;
    slot_config.width = 4;
    slot_config.cd = GPIO_NUM_5;
    slot_config.cmd = GPIO_NUM_6;
    slot_config.clk = GPIO_NUM_7;
    slot_config.d0 = GPIO_NUM_15;
    slot_config.d1 = GPIO_NUM_16;
    slot_config.d2 = GPIO_NUM_17;
    slot_config.d3 = GPIO_NUM_18;
    CHECK_CALLW(sdmmc_host_init(), "Failed to initialize SD host");
    CHECK_CALLW(sdmmc_host_init_slot(0, &slot_config), "Failed to initialize SD slot");
    // initial scan for SD cards
    scan_mounts();
    // install interrupts for SD card detect
    gpio_set_intr_type(GPIO_NUM_5, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(GPIO_NUM_5, check_det, NULL);
    esp_register_shutdown_handler(storage_deinit);
    return ESP_OK;
}

void storage_deinit(void) {
    ESP_LOGV(TAG, "Deinitializing storage");
    esp_vfs_fat_sdmmc_unmount_();
    sdmmc_host_deinit();
    esp_vfs_littlefs_unregister("rom");
    esp_vfs_littlefs_unregister("data");
}

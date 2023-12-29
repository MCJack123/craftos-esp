/**
 * Memory-Mapped Filesystem for ESP32
 * A performance-oriented filesystem driver designed for read-only partitions.
 * 
 * Copyright (c) 2024 JackMacWindows. Licensed under the Apache 2.0 license.
 */

#ifndef MMFS_H
#define MMFS_H
#include <esp_partition.h>

/**
 * Contains configuration variables for MMFS.
 */
typedef struct {
    /* The base path to mount the partition on. */
    const char* base_path;
    /* The label of the partition to mount. Set to NULL to ignore. */
    const char* partition;
    /* The type of partition to find. Set to ESP_PARTITION_TYPE_ANY to ignore. */
    esp_partition_type_t type;
    /* The subtype of partition to find. Set to ESP_PARTITION_SUBTYPE_ANY to ignore. */
    esp_partition_subtype_t subtype;
} mmfs_config_t;

/**
 * Mounts an MMFS partition into the VFS tree.
 * 
 * @param config A configuration structure with info about the partition and mount.
 * @return
 *  ESP_OK on success;
 *  ESP_ERR_NOT_FOUND if the partition selected could not be found;
 *  ESP_ERR_INVALID_STATE if the partition doesn't have a valid MMFS structure;
 *  ESP_ERR_NO_MEM if memory required for the mount couldn't be allocated;
 *  ESP_ERR_NOT_SUPPORTED if the partition lies on an external flash chip.
 */
extern esp_err_t mmfs_vfs_mount(const mmfs_config_t* config);

/**
 * Unmounts a previously mounted MMFS partition.
 * 
 * @param path The base path provided to mmfs_vfs_mount.
 * @return ESP_OK on success; ESP_ERR_NOT_FOUND if the path is not a valid MMFS mount.
 */
extern esp_err_t mmfs_vfs_unmount(const char* path);

#endif

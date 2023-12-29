#ifndef MMFS_H
#define MMFS_H
#include <esp_partition.h>

typedef struct {
    const char* base_path;
    const char* partition;
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
} mmfs_config_t;

extern esp_err_t mmfs_vfs_mount(const mmfs_config_t* config);
extern esp_err_t mmfs_vfs_unmount(const char* path);

#endif

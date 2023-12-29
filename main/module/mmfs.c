/**
 * Memory-Mapped Filesystem for ESP32
 * A performance-oriented filesystem driver designed for read-only partitions.
 * 
 * Copyright (c) 2024 JackMacWindows. Licensed under the Apache 2.0 license.
 */

#include <errno.h>
#include <fcntl.h>
#include <esp_partition.h>
#include <esp_vfs.h>
#include "mmfs.h"

#define MMFS_MAGIC 0x73664D4D // MMfs

struct mmfs_dir_ent {
    const char name[24];
    const unsigned is_dir: 1;
    const unsigned size: 31;
    const uint32_t offset;
} __packed;

struct mmfs_dir {
    const uint32_t magic;
    const uint32_t count;
    const struct mmfs_dir_ent entries[];
} __packed;

struct mmfs_fd {
    const uint8_t* start;
    const uint8_t* ptr;
    const uint8_t* end;
};

struct mmfs_dir_iter {
    uint16_t dd_vfs_idx; /*!< VFS index, not to be used by applications */
    uint16_t dd_rsv;     /*!< field reserved for future extension */
    uint8_t offset;
    const struct mmfs_dir* dir;
    struct dirent ent;
};

struct mmfs_mount {
    char* base_path;
    esp_partition_mmap_handle_t mmap;
    union {
        const struct mmfs_dir* root;
        const void* start;
    };
    struct mmfs_fd fds[MAX_FDS];
    struct mmfs_dir_iter dirs[MAX_FDS/8];
    struct mmfs_mount* next;
};

static const struct mmfs_dir_ent* mmfs_traverse(struct mmfs_mount* mount, const char* pat) {
    // Directory entries are sorted, so we use a binary sort on each level
    static char path[PATH_MAX];
    strcpy(path, pat);
    const struct mmfs_dir_ent* node = NULL;
    const struct mmfs_dir* dir = mount->root;
    for (char* p = strtok(path, "/"); p; p = strtok(NULL, "/")) {
        if (strcmp(p, "") == 0) continue;
        if (node) {
            if (!node->is_dir) {
                errno = ENOTDIR;
                return NULL;
            }
            dir = mount->start + node->offset;
        }
        if (dir->magic != MMFS_MAGIC) {
            errno = EIO;
            return NULL;
        }
        uint32_t l = 0, h = dir->count;
        while (true) {
            if (l >= h) {
                errno = ENOENT;
                return NULL;
            }
            uint32_t m = l + (h - l) / 2;
            int res = strcmp(p, dir->entries[m].name);
            if (res == 0) {
                node = &dir->entries[m];
                break;
            } else if (res > 0) {
                l = m + 1;
            } else {
                h = m;
            }
        }
    }
    return node;
}

static off_t mmfs_lseek(void* p, int fd, off_t size, int mode) {
    struct mmfs_mount* mount = p;
    if (fd < 0 || fd >= MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    struct mmfs_fd* fh = &mount->fds[fd];
    if (!fh->start) {
        errno = EBADF;
        return -1;
    }
    switch (mode) {
        case SEEK_SET:
            fh->ptr = fh->start + size;
            break;
        case SEEK_CUR:
            fh->ptr += size;
            break;
        case SEEK_END:
            fh->ptr = fh->end - size;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    return fh->ptr - fh->start;
}

static ssize_t mmfs_read(void* p, int fd, void* dst, size_t size) {
    struct mmfs_mount* mount = p;
    if (fd < 0 || fd >= MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    struct mmfs_fd* fh = &mount->fds[fd];
    if (!fh->start) {
        errno = EBADF;
        return -1;
    }
    if (fh->ptr >= fh->end) return 0;
    if (fh->ptr + size >= fh->end) size = fh->end - fh->ptr;
    if (size) memcpy(dst, fh->ptr, size);
    fh->ptr += size;
    return size;
}

static ssize_t mmfs_pread(void* p, int fd, void* dst, size_t size, off_t offset) {
    struct mmfs_mount* mount = p;
    if (fd < 0 || fd >= MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    struct mmfs_fd* fh = &mount->fds[fd];
    if (!fh->start) {
        errno = EBADF;
        return -1;
    }
    if (offset < 0 || fh->start + offset >= fh->end) return 0;
    if (fh->ptr + offset + size >= fh->end) size = fh->end - (fh->start + offset);
    if (size) memcpy(dst, fh->start + offset, size);
    return size;
}

static int mmfs_open(void* p, const char* path, int flags, int mode) {
    struct mmfs_mount* mount = p;
    if ((flags & O_ACCMODE) != O_RDONLY) {
        errno = EACCES;
        return -1;
    }
    for (int i = 0; i < MAX_FDS; i++) {
        if (mount->fds[i].start == NULL) {
            const struct mmfs_dir_ent* ent = mmfs_traverse(mount, path);
            if (ent == NULL) return -1;
            if (ent->is_dir) {
                errno = EISDIR;
                return -1;
            }
            mount->fds[i].start = mount->fds[i].ptr = mount->start + ent->offset;
            mount->fds[i].end = mount->start + ent->offset + ent->size;
            return i;
        }
    }
    errno = ENFILE;
    return -1;
}

static int mmfs_close(void* p, int fd) {
    struct mmfs_mount* mount = p;
    if (fd < 0 || fd >= MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    struct mmfs_fd* fh = &mount->fds[fd];
    if (!fh->start) {
        errno = EBADF;
        return -1;
    }
    fh->start = fh->ptr = fh->end = NULL;
    return 0;
}

static int mmfs_stat(void* p, const char* path, struct stat* st) {
    struct mmfs_mount* mount = p;
    st->st_atim.tv_sec = st->st_atim.tv_nsec = 0;
    st->st_ctim.tv_sec = st->st_ctim.tv_nsec = 0;
    st->st_mtim.tv_sec = st->st_mtim.tv_nsec = 0;
    st->st_gid = st->st_uid = 0;
    st->st_blksize = 1;
    st->st_dev = 0;
    st->st_ino = 0;
    st->st_nlink = 0;
    st->st_rdev = 0;
    if (strcmp(path, "/") == 0) {
        st->st_blocks = 0;
        st->st_mode = S_IFDIR | 0555;
        st->st_size = 0;
    } else {
        const struct mmfs_dir_ent* ent = mmfs_traverse(mount, path);
        if (ent == NULL) return -1;
        st->st_blocks = ent->size;
        st->st_mode = (ent->is_dir ? S_IFDIR : S_IFREG) | 0555;
        st->st_size = ent->size;
    }
    return 0;
}

static DIR* mmfs_opendir(void* p, const char* path) {
    struct mmfs_mount* mount = p;
    for (int i = 0; i < MAX_FDS/8; i++) {
        if (mount->dirs[i].dir == NULL) {
            if (strcmp(path, "/") == 0) {
                mount->dirs[i].dir = mount->root;
            } else {
                const struct mmfs_dir_ent* ent = mmfs_traverse(mount, path);
                if (ent == NULL) return NULL;
                if (!ent->is_dir) {
                    errno = ENOTDIR;
                    return NULL;
                }
                mount->dirs[i].dir = mount->start + ent->offset;
            }
            mount->dirs[i].offset = 0;
            return (DIR*)&mount->dirs[i];
        }
    }
    errno = ENFILE;
    return NULL;
}

static struct dirent* mmfs_readdir(void* p, DIR* pdir) {
    (void)p;
    struct mmfs_dir_iter* ent = (struct mmfs_dir_iter*)pdir;
    if (ent->dir->magic != MMFS_MAGIC) {
        errno = EIO;
        return NULL;
    }
    if (ent->offset >= ent->dir->count) return NULL;
    ent->ent.d_ino = 0;
    strcpy(ent->ent.d_name, ent->dir->entries[ent->offset].name);
    ent->ent.d_type = ent->dir->entries[ent->offset].is_dir ? DT_DIR : DT_REG;
    ent->offset++;
    return &ent->ent;
}

static long mmfs_telldir(void* p, DIR* pdir) {
    (void)p;
    struct mmfs_dir_iter* ent = (struct mmfs_dir_iter*)pdir;
    if (ent->dir->magic != MMFS_MAGIC) {
        errno = EIO;
        return -1;
    }
    return ent->offset;
}

static void mmfs_seekdir(void* p, DIR* pdir, long offset) {
    (void)p;
    struct mmfs_dir_iter* ent = (struct mmfs_dir_iter*)pdir;
    if (ent->dir->magic != MMFS_MAGIC) {
        errno = EIO;
        return;
    }
    ent->offset = offset;
}

static int mmfs_closedir(void* p, DIR* pdir) {
    (void)p;
    struct mmfs_dir_iter* ent = (struct mmfs_dir_iter*)pdir;
    if (ent->dir->magic != MMFS_MAGIC) {
        errno = EIO;
        return -1;
    }
    ent->dir = NULL;
    return 0;
}

static int mmfs_access(void* p, const char* path, int amode) {
    struct mmfs_mount* mount = p;
    const struct mmfs_dir_ent* ent = strcmp(path, "/") == 0 ? p : mmfs_traverse(mount, path);
    if (!ent) return -1;
    if (amode & W_OK) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

static esp_vfs_t mmfs_vfs = {
    .flags = ESP_VFS_FLAG_CONTEXT_PTR,
    .lseek_p = mmfs_lseek,
    .read_p = mmfs_read,
    .pread_p = mmfs_pread,
    .open_p = mmfs_open,
    .close_p = mmfs_close,
    .stat_p = mmfs_stat,
    .opendir_p = mmfs_opendir,
    .readdir_p = mmfs_readdir,
    .telldir_p = mmfs_telldir,
    .seekdir_p = mmfs_seekdir,
    .closedir_p = mmfs_closedir,
    .access_p = mmfs_access
};

static struct mmfs_mount* mounts = NULL;

esp_err_t mmfs_vfs_mount(const mmfs_config_t* config) {
    esp_err_t err;
    const esp_partition_t* part = esp_partition_find_first(config->type, config->subtype, config->partition);
    if (!part) return ESP_ERR_NOT_FOUND;
    const void* ptr;
    esp_partition_mmap_handle_t handle;
    err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) return err;
    if (*(uint32_t*)ptr != MMFS_MAGIC) {
        esp_partition_munmap(handle);
        return ESP_ERR_INVALID_STATE;
    }
    struct mmfs_mount* mount = malloc(sizeof(struct mmfs_mount));
    if (mount == NULL) {
        esp_partition_munmap(handle);
        return ESP_ERR_NO_MEM;
    }
    mount->mmap = handle;
    mount->root = ptr;
    mount->base_path = malloc(strlen(config->base_path) + 1);
    if (mount->base_path == NULL) {
        free(mount);
        esp_partition_munmap(handle);
        return ESP_ERR_NO_MEM;
    }
    strcpy(mount->base_path, config->base_path);
    for (int i = 0; i < MAX_FDS; i++)
        mount->fds[i].start = mount->fds[i].ptr = mount->fds[i].end = NULL;
    for (int i = 0; i < MAX_FDS/8; i++)
        mount->dirs[i].dir = NULL;
    mount->next = mounts;
    mounts = mount;
    err = esp_vfs_register(config->base_path, &mmfs_vfs, mount);
    if (err != ESP_OK) {
        free(mount->base_path);
        free(mount);
        esp_partition_munmap(handle);
        return err;
    }
    return ESP_OK;
}

esp_err_t mmfs_vfs_unmount(const char* path) {
    struct mmfs_mount* m = mounts;
    struct mmfs_mount** last = &mounts;
    while (m) {
        if (strcmp(m->base_path, path) == 0) {
            esp_vfs_unregister(path);
            esp_partition_munmap(m->mmap);
            free(m->base_path);
            *last = m->next;
            free(m);
            return ESP_OK;
        }
        last = &m->next;
        m = m->next;
    }
    return ESP_ERR_NOT_FOUND;
}

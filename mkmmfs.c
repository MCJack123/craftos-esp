/**
 * Memory-Mapped Filesystem for ESP32
 * A performance-oriented filesystem driver designed for read-only partitions.
 * 
 * Copyright (c) 2024 JackMacWindows. Licensed under the Apache 2.0 license.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MMFS_MAGIC 0x73664D4D // MMfs
static const uint32_t magic = MMFS_MAGIC;
static const uint8_t zero[1024] = {0};

struct mmfs_dir_ent {
    char name[24];
    unsigned is_dir: 1;
    unsigned size: 31;
    uint32_t offset;
} __attribute__((packed));

struct mmfs_dir {
    uint32_t magic;
    uint32_t count;
    struct mmfs_dir_ent entries[];
} __attribute__((packed));

struct strll {
    char* path;
    char* name;
    struct stat st;
    off_t offset;
    struct strll* next;
};

static void pack_dir(const char* path, FILE* fp) {
    DIR* d = opendir(path);
    if (d) {
        struct strll* list = NULL;
        struct dirent* dir;
        uint32_t count = 0;
        while ((dir = readdir(d))) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
            struct strll* next = malloc(sizeof(struct strll));
            next->name = malloc(strlen(dir->d_name) + 1);
            strcpy(next->name, dir->d_name);
            next->path = malloc(strlen(path) + strlen(dir->d_name) + 2);
            strcpy(next->path, path);
            strcat(next->path, "/");
            strcat(next->path, dir->d_name);
            stat(next->path, &next->st);
            // Insertion sort (w/linked list = O(n))
            struct strll* node = list;
            struct strll* last = NULL;
            while (node && strcmp(dir->d_name, node->name) > 0) {last = node; node = node->next;}
            if (last) last->next = next;
            else list = next;
            next->next = node;
            count++;
        }
        closedir(d);
        fwrite(&magic, 4, 1, fp);
        fwrite(&count, 4, 1, fp);
        struct strll* node = list;
        while (node) {
            struct mmfs_dir_ent ent = {};
            node->offset = ftell(fp) + 28;
            strncpy(ent.name, node->name, 23);
            ent.is_dir = S_ISDIR(node->st.st_mode);
            ent.size = node->st.st_size;
            ent.offset = 0;
            fwrite(&ent, sizeof(ent), 1, fp);
            node = node->next;
        }
        node = list;
        while (node) {
            uint32_t off = ftell(fp);
            fseek(fp, node->offset, SEEK_SET);
            fwrite(&off, 4, 1, fp);
            fseek(fp, off, SEEK_SET);
            if (S_ISDIR(node->st.st_mode)) {
                pack_dir(node->path, fp);
            } else {
                FILE* fpin = fopen(node->path, "rb");
                if (fpin == NULL) {
                    fprintf(stderr, "Could not read file %s: %s\n", node->path, strerror(errno));
                    break;
                }
                static char buf[4096];
                while (!feof(fpin)) {
                    size_t size = fread(buf, 1, 4096, fpin);
                    fwrite(buf, 1, size, fp);
                }
                fclose(fpin);
            }
            free(node->name);
            free(node->path);
            struct strll* next = node->next;
            free(node);
            node = next;
        }
    } else {
        fprintf(stderr, "Could not read directory %s: %s\n", path, strerror(errno));
    }
}

int main(int argc, const char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <directory> <file.bin> [partition size]\n");
        return 1;
    }
    FILE* fp = fopen(argv[2], "wb");
    if (!fp) {
        fprintf(stderr, "Could not open output file: %s\n", strerror(errno));
        return errno;
    }
    pack_dir(argv[1], fp);
    off_t pos = ftell(fp);
    printf("Total data size: %d bytes\n", pos);
    if (argc > 3) {
        unsigned long size;
        if (argv[3][0] == '0' && argv[3][1] == 'x') size = strtoul(argv[3] + 2, NULL, 16);
        else size = strtoul(argv[3], NULL, 10);
        if (size < pos) {
            fprintf(stderr, "Warning: partition is too small for the data!\n");
        } else {
            size_t needed = size - pos;
            while (needed >= 1024) {fwrite(zero, 1024, 1, fp); needed -= 1024;}
            for (; needed; needed--) fputc(0, fp);
        }
    }
    fclose(fp);
    return 0;
}

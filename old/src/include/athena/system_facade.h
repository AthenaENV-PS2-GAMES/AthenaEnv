#ifndef ATHENA_SYSTEM_FACADE_H
#define ATHENA_SYSTEM_FACADE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AthenaDirEntry {
    char name[256];
    uint32_t size;
    bool is_dir;
} AthenaDirEntry;

typedef struct AthenaDirListing {
    AthenaDirEntry *entries;
    int count;
} AthenaDirListing;

typedef struct AthenaFileStat {
    uint32_t size;
    bool is_dir;
} AthenaFileStat;

typedef struct AthenaMemoryStats {
    uint32_t core;
    uint32_t native_stack;
    uint32_t allocs;
    uint32_t used;
} AthenaMemoryStats;

typedef struct AthenaThreadInfo {
    int id;
    const char *title;
    size_t stack_size;
    int status;
} AthenaThreadInfo;

typedef struct AthenaThreadListing {
    AthenaThreadInfo *threads;
    int count;
} AthenaThreadListing;

AthenaDirListing *athena_system_list_dir(const char *path);
void athena_system_dir_listing_free(AthenaDirListing *listing);

int athena_system_stat(const char *path, AthenaFileStat *out);
int athena_system_copy_file(const char *src, const char *dst);
int athena_system_move_file(const char *src, const char *dst);
int athena_system_remove_file(const char *path);
int athena_system_remove_dir(const char *path);

void athena_system_sleep(int seconds);
void athena_system_poweroff(void);
void athena_system_reboot(const char *elf_path, int argc, char **argv);

int athena_system_getcwd(char *buf, size_t size);
int athena_system_chdir(const char *path);
int athena_system_load_elf(const char *path, int argc, char **argv, bool reset_iop);

AthenaThreadListing *athena_system_list_threads(void);
void athena_system_thread_listing_free(AthenaThreadListing *listing);
void athena_system_get_memory_stats(AthenaMemoryStats *stats);

#endif /* ATHENA_SYSTEM_FACADE_H */

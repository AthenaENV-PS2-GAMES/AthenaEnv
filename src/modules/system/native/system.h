#ifndef ATH_NATIVE_SYSTEM_H
#define ATH_NATIVE_SYSTEM_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef struct {
    char name[256];
    uint32_t size;
    int dir;
} AthenaDirectoryEntry;

int athena_system_list_dir_native(const char *path,
    AthenaDirectoryEntry **entries, size_t *count);
void athena_system_free_directory_entries(AthenaDirectoryEntry *entries);

typedef struct {
    int type;
    int free_space;
    int format;
} AthenaMemoryCardInfo;

int athena_system_remove_directory_native(const char *path);
int athena_system_copy_file_native(const char *source, const char *destination);
int athena_system_move_file_native(const char *source, const char *destination);
int athena_system_get_memory_card_info(int port, AthenaMemoryCardInfo *info);
int athena_system_mount_native(const char *mountpoint, const char *blockdev, int mode);
int athena_system_umount_native(const char *device);

void athena_system_delay_native(void);
void athena_system_sleep_native(int32_t milliseconds);
clock_t athena_system_get_ticks_native(void);
double athena_system_get_milliseconds_native(void);
uint32_t athena_system_get_used_memory_native(void);
uint32_t athena_system_get_free_memory_native(void);

#endif /* ATH_NATIVE_SYSTEM_H */

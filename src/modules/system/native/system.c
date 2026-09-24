#include <fcntl.h>
#include <dirent.h>
#include <libmc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include <kernel.h>
#include <athena/memory.h>
#include <athena/module.h>
#include <timer.h>

#include <athena/system.h>

int athena_system_list_dir_native(const char *path,
    AthenaDirectoryEntry **entries, size_t *count) {
    DIR *dir = opendir(path);
    if (!dir) return -1;

    AthenaDirectoryEntry *result = NULL;
    size_t result_count = 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        char entry_path[512];
        struct stat info;
        snprintf(entry_path, sizeof(entry_path), "%s/%s", path, entry->d_name);
        if (stat(entry_path, &info) != 0) continue;

        if (result_count == capacity) {
            size_t new_capacity = capacity == 0 ? 8 : capacity * 2;
            if (new_capacity < capacity ||
                new_capacity > ((size_t)-1 / sizeof(*result))) {
                closedir(dir);
                free(result);
                return -1;
            }
            AthenaDirectoryEntry *resized = realloc(result,
                new_capacity * sizeof(*result));
            if (!resized) {
                closedir(dir);
                free(result);
                return -1;
            }
            result = resized;
            capacity = new_capacity;
        }

        strncpy(result[result_count].name, entry->d_name,
            sizeof(result[result_count].name) - 1);
        result[result_count].name[sizeof(result[result_count].name) - 1] = '\0';
        result[result_count].size = (uint32_t)info.st_size;
        result[result_count].dir = S_ISDIR(info.st_mode);
        result_count++;
    }
    closedir(dir);
    *entries = result;
    *count = result_count;
    return 0;
}

void athena_system_free_directory_entries(AthenaDirectoryEntry *entries) {
    free(entries);
}

int athena_system_remove_directory_native(const char *path) {
    return rmdir(path);
}

int athena_system_copy_file_native(const char *source_path, const char *destination_path) {
    int source = open(source_path, O_RDONLY, 0);
    int destination = open(destination_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (source < 0 || destination < 0) {
        if (source >= 0) close(source);
        if (destination >= 0) close(destination);
        return -1;
    }

    char buffer[4096];
    ssize_t read_size;
    int result = 0;
    while ((read_size = read(source, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < read_size) {
            ssize_t current = write(destination, buffer + written,
                (size_t)(read_size - written));
            if (current <= 0) {
                result = -1;
                break;
            }
            written += current;
        }
        if (result != 0) break;
    }
    if (read_size < 0) result = -1;
    close(source);
    close(destination);
    return result;
}

int athena_system_move_file_native(const char *source, const char *destination) {
    return rename(source, destination);
}

int athena_system_get_memory_card_info(int port, AthenaMemoryCardInfo *info) {
    if (!athena_module_enabled("memcard"))
        return ATHENA_SYSTEM_ERR_NO_MEMCARD;
    int request = mcGetInfo(port, 0, &info->type, &info->free_space, &info->format);
    if (request < 0) return request;

    int result = 0;
    mcSync(0, NULL, &result);
    if (result < -2) return result;
    return 0;
}

int athena_system_mount_native(const char *mountpoint, const char *blockdev, int mode) {
    return fileXioMount(mountpoint, blockdev, mode);
}

int athena_system_umount_native(const char *device) {
    return fileXioUmount(device);
}

void athena_system_delay_native(void) {
    nopdelay();
}

void athena_system_sleep_native(int32_t milliseconds) {
    if (milliseconds > 0) {
        usleep((useconds_t)((int64_t)milliseconds * 1000));
    }
}

clock_t athena_system_get_ticks_native(void) {
    return clock();
}

double athena_system_get_milliseconds_native(void) {
    return ((double)clock() / (double)CLOCKS_PER_SEC) * 1000.0;
}

uint32_t athena_system_get_used_memory_native(void) {
    return (uint32_t)get_used_memory();
}

uint32_t athena_system_get_free_memory_native(void) {
    uint32_t total = GetMemorySize();
    uint32_t used = athena_system_get_used_memory_native();
    return total > used ? total - used : 0;
}

#include <fcntl.h>
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
#include <memory.h>
#include <timer.h>

#include "system.h"

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

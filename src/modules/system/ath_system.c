#include <unistd.h>
#include <malloc.h>
#include <time.h>
#include <kernel.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libmc.h>
#include <libcdvd.h>
#include <timer.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>
#include <io_common.h>
#include <usbhdfsd-common.h>
#include <hdd-ioctl.h>
#include <loadfile.h>
#include <system.h>

#include <ath_env.h>
#include "ath_system.h"
#include <memory.h>
#include <dbgprintf.h>

static bool system_dark_mode;

static JSValue athena_system_get_boot_path(JSContext *ctx, JSValueConst this_val, int magic) {
    return JS_NewString(ctx, boot_path);
}

static JSValue athena_system_list_dir(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc > 1) {
        return JS_ThrowTypeError(ctx, "System.listDir([path]) accepts zero or one argument");
    }

    char path[256];
    if (argc == 0) {
        if (!getcwd(path, sizeof(path))) {
            return JS_ThrowInternalError(ctx, "Unable to determine current directory");
        }
    } else {
        const char *requested = JS_ToCString(ctx, argv[0]);
        if (!requested) return JS_EXCEPTION;
        if (strchr(requested, ':')) {
            snprintf(path, sizeof(path), "%s", requested);
        } else {
            snprintf(path, sizeof(path), "%s%s", boot_path, requested);
        }
        JS_FreeCString(ctx, requested);
    }

    DIR *dir = opendir(path);
    if (!dir) return JS_ThrowInternalError(ctx, "Unable to open directory: %s", path);

    JSValue result = JS_NewArray(ctx);
    struct dirent *entry;
    uint32_t index = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        char entry_path[512];
        struct stat info;
        snprintf(entry_path, sizeof(entry_path), "%s/%s", path, entry->d_name);
        if (stat(entry_path, &info) != 0) continue;

        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, entry->d_name));
        JS_SetPropertyStr(ctx, item, "size", JS_NewUint32(ctx, (uint32_t)info.st_size));
        JS_SetPropertyStr(ctx, item, "dir", JS_NewBool(ctx, S_ISDIR(info.st_mode)));
        JS_SetPropertyUint32(ctx, result, index++, item);
    }
    closedir(dir);
    return result;
}

static int athena_system_path_arg(JSContext *ctx, JSValueConst value, const char **path) {
    *path = JS_ToCString(ctx, value);
    return *path != NULL;
}

static JSValue athena_system_remove_directory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "System.removeDirectory(directory) requires one argument");
    const char *path;
    if (!athena_system_path_arg(ctx, argv[0], &path)) return JS_EXCEPTION;
    int result = rmdir(path);
    JS_FreeCString(ctx, path);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_copy_file(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 2) return JS_ThrowTypeError(ctx, "System.copyFile(source, destination) requires two arguments");
    const char *source_path;
    const char *destination_path;
    if (!athena_system_path_arg(ctx, argv[0], &source_path)) return JS_EXCEPTION;
    if (!athena_system_path_arg(ctx, argv[1], &destination_path)) {
        JS_FreeCString(ctx, source_path);
        return JS_EXCEPTION;
    }

    int source = open(source_path, O_RDONLY, 0);
    int destination = open(destination_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (source < 0 || destination < 0) {
        if (source >= 0) close(source);
        if (destination >= 0) close(destination);
        JS_FreeCString(ctx, source_path);
        JS_FreeCString(ctx, destination_path);
        return JS_ThrowInternalError(ctx, "Unable to open file for copy");
    }

    char buffer[4096];
    ssize_t read_size;
    int result = 0;
    while ((read_size = read(source, buffer, sizeof(buffer))) > 0) {
        if (write(destination, buffer, read_size) != read_size) {
            result = -1;
            break;
        }
    }
    if (read_size < 0) result = -1;
    close(source);
    close(destination);
    JS_FreeCString(ctx, source_path);
    JS_FreeCString(ctx, destination_path);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_move_file(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 2) return JS_ThrowTypeError(ctx, "System.moveFile(source, destination) requires two arguments");
    const char *source;
    const char *destination;
    if (!athena_system_path_arg(ctx, argv[0], &source)) return JS_EXCEPTION;
    if (!athena_system_path_arg(ctx, argv[1], &destination)) {
        JS_FreeCString(ctx, source);
        return JS_EXCEPTION;
    }
    int result = rename(source, destination);
    JS_FreeCString(ctx, source);
    JS_FreeCString(ctx, destination);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_delay(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.delay takes no arguments");
    nopdelay();
    return JS_UNDEFINED;
}

static JSValue athena_system_set_dark_mode(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "System.setDarkMode(enabled) requires one argument");
    system_dark_mode = JS_ToBool(ctx, argv[0]);
    return JS_UNDEFINED;
}

static JSValue athena_system_exit_to_browser(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.exitToBrowser takes no arguments");
    Exit(0);
    return JS_UNDEFINED;
}

static JSValue athena_system_get_mc_info(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    int slot = 0;
    if (argc > 1) return JS_ThrowTypeError(ctx, "System.getMCInfo([slot]) accepts zero or one argument");
    if (argc == 1 && JS_ToInt32(ctx, &slot, argv[0])) return JS_EXCEPTION;

    int type = 0;
    int free_space = 0;
    int format = 0;
    int result = 0;
    mcGetInfo(slot, 0, &type, &free_space, &format);
    mcSync(0, NULL, &result);
    if (result < 0) {
        return JS_ThrowInternalError(ctx, "Unable to read memory-card information");
    }

    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "type", JS_NewInt32(ctx, type));
    JS_SetPropertyStr(ctx, info, "freemem", JS_NewInt32(ctx, free_space));
    JS_SetPropertyStr(ctx, info, "format", JS_NewInt32(ctx, format));
    return info;
}

static JSValue athena_system_load_elf(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc < 1 || argc > 2) return JS_ThrowTypeError(ctx, "System.loadELF(path[, args]) accepts one or two arguments");
    const char *path;
    if (!athena_system_path_arg(ctx, argv[0], &path)) return JS_EXCEPTION;

    int arg_count = 0;
    char **args = NULL;
    if (argc == 2) {
        JSValue length = JS_GetPropertyStr(ctx, argv[1], "length");
        if (JS_ToInt32(ctx, &arg_count, length)) {
            JS_FreeValue(ctx, length);
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, length);
        if (arg_count < 0 ||
            (size_t)arg_count > (((size_t)-1 / sizeof(char *)) - 1)) {
            JS_FreeCString(ctx, path);
            return JS_ThrowTypeError(ctx, "System.loadELF args length must be non-negative and fit in memory");
        }
        args = malloc(sizeof(char *) * (arg_count + 1));
        if (!args) {
            JS_FreeCString(ctx, path);
            return JS_ThrowOutOfMemory(ctx);
        }
        for (int i = 0; i < arg_count; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
            args[i] = (char *)JS_ToCString(ctx, item);
            JS_FreeValue(ctx, item);
            if (!args[i]) {
                for (int j = 0; j < i; j++) JS_FreeCString(ctx, args[j]);
                free(args);
                JS_FreeCString(ctx, path);
                return JS_EXCEPTION;
            }
        }
        args[arg_count] = NULL;
    }

    int result = LoadELFFromFileNoReset(path, arg_count, args);
    if (args) {
        for (int i = 0; i < arg_count; i++) JS_FreeCString(ctx, args[i]);
        free(args);
    }
    JS_FreeCString(ctx, path);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_mount(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc < 2 || argc > 3) return JS_ThrowTypeError(ctx, "System.mount(mountpoint, blockdev[, mode])");
    const char *mountpoint;
    const char *blockdev;
    if (!athena_system_path_arg(ctx, argv[0], &mountpoint)) return JS_EXCEPTION;
    if (!athena_system_path_arg(ctx, argv[1], &blockdev)) {
        JS_FreeCString(ctx, mountpoint);
        return JS_EXCEPTION;
    }
    int mode = 0;
    if (argc == 3 && JS_ToInt32(ctx, &mode, argv[2])) {
        JS_FreeCString(ctx, mountpoint);
        JS_FreeCString(ctx, blockdev);
        return JS_EXCEPTION;
    }
    int result = fileXioMount(mountpoint, blockdev, mode);
    JS_FreeCString(ctx, mountpoint);
    JS_FreeCString(ctx, blockdev);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_umount(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "System.umount(device) requires one argument");
    const char *device;
    if (!athena_system_path_arg(ctx, argv[0], &device)) return JS_EXCEPTION;
    int result = fileXioUmount(device);
    JS_FreeCString(ctx, device);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_devices(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    struct fileXioDevice devices[FILEXIO_MAX_DEVICES];
    int count = fileXioGetDeviceList(devices, FILEXIO_MAX_DEVICES);
    if (count <= 0) return JS_NewArray(ctx);

    JSValue result = JS_NewArray(ctx);
    for (int i = 0; i < count; i++) {
        JSValue device = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, device, "name", JS_NewString(ctx, devices[i].name));
        JS_SetPropertyStr(ctx, device, "desc", JS_NewString(ctx, devices[i].desc));
        JS_SetPropertyUint32(ctx, result, (uint32_t)i, device);
    }
    return result;
}

static JSValue athena_system_get_bdm_info(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "System.getBDMInfo(device) requires one argument");
    const char *device;
    if (!athena_system_path_arg(ctx, argv[0], &device)) return JS_EXCEPTION;
    int fd = fileXioDopen(device);
    if (fd < 0) {
        JS_FreeCString(ctx, device);
        return JS_UNDEFINED;
    }

    char driver[10] = { 0 };
    int result = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driver, sizeof(driver) - 1);
    fileXioDclose(fd);
    if (result < 0) {
        JS_FreeCString(ctx, device);
        return JS_UNDEFINED;
    }

    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "name", JS_NewString(ctx, driver));
    size_t device_length = strlen(device);
    int device_index = -1;
    if (device_length > 4 && device[4] >= '0' && device[4] <= '9') {
        device_index = device[4] - '0';
    }
    JS_SetPropertyStr(ctx, info, "index", JS_NewInt32(ctx, device_index));
    JS_FreeCString(ctx, device);
    return info;
}

static JSValue athena_system_get_cpu_info(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getCPUInfo takes no arguments");
    unsigned int cop0 = GetCop0(15);
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "implementation", JS_NewInt32(ctx, (cop0 >> 8) & 0xff));
    JS_SetPropertyStr(ctx, info, "revision", JS_NewInt32(ctx, cop0 & 0xff));
    JS_SetPropertyStr(ctx, info, "RAMSize", JS_NewUint32(ctx, GetMemorySize()));
    JS_SetPropertyStr(ctx, info, "BUSClock", JS_NewUint32(ctx, kBUSCLK));
    JS_SetPropertyStr(ctx, info, "CPUClock", JS_NewUint32(ctx, kBUSCLK * 2));
    JS_SetPropertyStr(ctx, info, "MachineType", JS_NewUint32(ctx, MachineType()));
    return info;
}

static JSValue athena_system_get_gpu_info(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getGPUInfo takes no arguments");
    volatile uint64_t *gs_csr = (volatile uint64_t *)0x12001000;
    uint16_t revision = (uint16_t)(*gs_csr >> 16);
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "revision", JS_NewInt32(ctx, revision & 0xff));
    JS_SetPropertyStr(ctx, info, "id", JS_NewInt32(ctx, (revision >> 8) & 0xff));
    return info;
}

static JSValue athena_system_get_temperature(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getTemperature takes no arguments");
    unsigned char command[1] = { 0xef };
    unsigned char response[16] = { 0 };
    if (sceCdApplySCmd(0x03, command, sizeof(command), response) == 0 || response[0] == 0) {
        return JS_UNDEFINED;
    }
    uint16_t raw = ((uint16_t)response[1] << 8) | response[2];
    return JS_NewFloat64(ctx, (double)(raw / 128) + (double)(raw % 128) / 10.0);
}

static JSValue athena_system_get_memory_stats(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getMemoryStats takes no arguments");
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "core", JS_NewUint32(ctx, (uint32_t)get_binary_size()));
    JS_SetPropertyStr(ctx, info, "nativeStack", JS_NewUint32(ctx, (uint32_t)get_stack_size()));
    JS_SetPropertyStr(ctx, info, "allocs", JS_NewUint32(ctx, (uint32_t)get_allocs_size()));
    JS_SetPropertyStr(ctx, info, "used", JS_NewUint32(ctx, (uint32_t)get_used_memory()));
    return info;
}

static JSValue athena_system_get_ticks(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getTicks takes no arguments");
    return JS_NewInt64(ctx, (int64_t)clock());
}

static JSValue athena_system_get_ms(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getMilliseconds takes no arguments");
    clock_t c = clock();
    double ms = ((double)c / (double)CLOCKS_PER_SEC) * 1000.0;
    return JS_NewFloat64(ctx, ms);
}

static JSValue athena_system_sleep(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "System.sleep(milliseconds) requires 1 argument");
    }
    int32_t ms = 0;
    if (JS_ToInt32(ctx, &ms, argv[0])) {
        return JS_EXCEPTION;
    }
    if (ms > 0) {
        usleep((useconds_t)((int64_t)ms * 1000));
    }
    return JS_UNDEFINED;
}

static JSValue athena_system_get_used_memory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getUsedMemory takes no arguments");
    return JS_NewUint32(ctx, (uint32_t)get_used_memory());
}

static JSValue athena_system_get_free_memory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.getFreeMemory takes no arguments");
    uint32_t total = GetMemorySize();
    uint32_t used = (uint32_t)get_used_memory();
    uint32_t free_mem = (total > used) ? (total - used) : 0;
    return JS_NewUint32(ctx, free_mem);
}

static JSValue athena_system_gc(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "System.gc takes no arguments");
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static JSValue athena_system_exit(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    dbgprintf("[AthenaCore] System.exit called\n");
    Exit(0);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry system_module_funcs[] = {
    JS_CFUNC_DEF("listDir", 1, athena_system_list_dir),
    JS_CFUNC_DEF("removeDirectory", 1, athena_system_remove_directory),
    JS_CFUNC_DEF("copyFile", 2, athena_system_copy_file),
    JS_CFUNC_DEF("moveFile", 2, athena_system_move_file),
    JS_CFUNC_DEF("rename", 2, athena_system_move_file),
    JS_CFUNC_DEF("delay", 0, athena_system_delay),
    JS_CFUNC_DEF("exitToBrowser", 0, athena_system_exit_to_browser),
    JS_CFUNC_DEF("getMCInfo", 1, athena_system_get_mc_info),
    JS_CFUNC_DEF("loadELF", 2, athena_system_load_elf),
    JS_CFUNC_DEF("mount", 3, athena_system_mount),
    JS_CFUNC_DEF("umount", 1, athena_system_umount),
    JS_CFUNC_DEF("devices", 0, athena_system_devices),
    JS_CFUNC_DEF("getBDMInfo", 1, athena_system_get_bdm_info),
    JS_CFUNC_DEF("getCPUInfo", 0, athena_system_get_cpu_info),
    JS_CFUNC_DEF("getGPUInfo", 0, athena_system_get_gpu_info),
    JS_CFUNC_DEF("getTemperature", 0, athena_system_get_temperature),
    JS_CFUNC_DEF("getMemoryStats", 0, athena_system_get_memory_stats),
    JS_CFUNC_DEF("setDarkMode", 1, athena_system_set_dark_mode),
    JS_CFUNC_DEF("getTicks", 0, athena_system_get_ticks),
    JS_CFUNC_DEF("getMilliseconds", 0, athena_system_get_ms),
    JS_CFUNC_DEF("sleep", 1, athena_system_sleep),
    JS_CFUNC_DEF("getUsedMemory", 0, athena_system_get_used_memory),
    JS_CFUNC_DEF("getFreeMemory", 0, athena_system_get_free_memory),
    JS_CFUNC_DEF("gc", 0, athena_system_gc),
    JS_CFUNC_DEF("exit", 0, athena_system_exit),
    JS_CGETSET_MAGIC_DEF("bootPath", athena_system_get_boot_path, NULL, 0),
    JS_CGETSET_MAGIC_DEF("boot_path", athena_system_get_boot_path, NULL, 0),
};

static int athena_system_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, system_module_funcs, countof(system_module_funcs));
}

JSModuleDef *athena_system_init(JSContext* ctx) {
    return athena_push_module(ctx, athena_system_module_init, system_module_funcs, countof(system_module_funcs), "System");
}

#include <unistd.h>
#include <malloc.h>
#include <time.h>
#include <kernel.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <libcdvd.h>
#include <timer.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>
#include <io_common.h>
#include <usbhdfsd-common.h>
#include <hdd-ioctl.h>
#include <loadfile.h>
#include <athena/system/elf_loader.h>

#include <ath_env.h>
#include <ath_gil.h>
#include "ath_system.h"
#include <athena/memory.h>
#include <athena/debug.h>
#include <athena/system.h>

static int athena_system_require_argc(JSContext *ctx, int argc, int expected, const char *name) {
    if (argc != expected) {
        JS_ThrowTypeError(ctx, "%s expects %d argument%s", name, expected, expected == 1 ? "" : "s");
        return 0;
    }
    return 1;
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
        int written;
        if (strchr(requested, ':')) {
            written = snprintf(path, sizeof(path), "%s", requested);
        } else {
            /* getcwd() on a console returns "mass0:/bin", without the trailing slash. */
            size_t length = strlen(boot_path);
            const char *separator = length && boot_path[length - 1] != '/' && boot_path[length - 1] != ':' ? "/" : "";
            written = snprintf(path, sizeof(path), "%s%s%s", boot_path, separator, requested);
        }
        JS_FreeCString(ctx, requested);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            return JS_ThrowRangeError(ctx, "System.listDir path is too long");
        }
    }

    AthenaDirectoryEntry *entries = NULL;
    size_t entry_count = 0;
    athena_js_gil_unlock();
    int native_result = athena_system_list_dir_native(path, &entries, &entry_count);
    athena_js_gil_lock();
    if (native_result < 0) {
        return JS_ThrowInternalError(ctx, "Unable to open directory: %s", path);
    }

    JSValue result = JS_NewArray(ctx);
    for (size_t i = 0; i < entry_count; i++) {
        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, entries[i].name));
        JS_SetPropertyStr(ctx, item, "size", JS_NewUint32(ctx, entries[i].size));
        JS_SetPropertyStr(ctx, item, "dir", JS_NewBool(ctx, entries[i].dir));
        JS_SetPropertyUint32(ctx, result, (uint32_t)i, item);
    }
    athena_system_free_directory_entries(entries);
    return result;
}

static int athena_system_path_arg(JSContext *ctx, JSValueConst value, char **path) {
    const char *value_string = JS_ToCString(ctx, value);
    if (!value_string) {
        *path = NULL;
        return 0;
    }
    size_t length = strlen(value_string) + 1;
    *path = malloc(length);
    if (*path) {
        memcpy(*path, value_string, length);
    } else {
        JS_ThrowOutOfMemory(ctx);
    }
    JS_FreeCString(ctx, value_string);
    return *path != NULL;
}

static JSValue athena_system_remove_directory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 1, "System.removeDirectory")) return JS_EXCEPTION;
    char *path;
    if (!athena_system_path_arg(ctx, argv[0], &path)) return JS_EXCEPTION;
    athena_js_gil_unlock();
    int result = athena_system_remove_directory_native(path);
    athena_js_gil_lock();
    free(path);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_copy_file(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 2, "System.copyFile")) return JS_EXCEPTION;
    char *source_path;
    char *destination_path;
    if (!athena_system_path_arg(ctx, argv[0], &source_path)) return JS_EXCEPTION;
    if (!athena_system_path_arg(ctx, argv[1], &destination_path)) {
        free(source_path);
        return JS_EXCEPTION;
    }

    athena_js_gil_unlock();
    int result = athena_system_copy_file_native(source_path, destination_path);
    athena_js_gil_lock();
    free(source_path);
    free(destination_path);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_move_file(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 2, "System.moveFile")) return JS_EXCEPTION;
    char *source;
    char *destination;
    if (!athena_system_path_arg(ctx, argv[0], &source)) return JS_EXCEPTION;
    if (!athena_system_path_arg(ctx, argv[1], &destination)) {
        free(source);
        return JS_EXCEPTION;
    }
    athena_js_gil_unlock();
    int result = athena_system_move_file_native(source, destination);
    athena_js_gil_lock();
    free(source);
    free(destination);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_delay(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.delay")) return JS_EXCEPTION;
    athena_js_gil_unlock();
    athena_system_delay_native();
    athena_js_gil_lock();
    return JS_UNDEFINED;
}

static JSValue athena_system_set_dark_mode(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 1, "System.setDarkMode")) return JS_EXCEPTION;
    dark_mode = JS_ToBool(ctx, argv[0]);
    return JS_UNDEFINED;
}

static JSValue athena_system_exit_to_browser(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.exitToBrowser")) return JS_EXCEPTION;
    Exit(0);
    return JS_UNDEFINED;
}

static JSValue athena_system_get_mc_info(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    int32_t port = 0;
    if (argc > 1) return JS_ThrowTypeError(ctx, "System.getMCInfo accepts zero or one argument");
    if (argc == 1 && JS_ToInt32(ctx, &port, argv[0])) return JS_EXCEPTION;
    if (port < 0 || port > 1) {
        return JS_ThrowRangeError(ctx, "System.getMCInfo port must be 0 or 1");
    }

    AthenaMemoryCardInfo memory_card;
    athena_js_gil_unlock();
    int result = athena_system_get_memory_card_info(port, &memory_card);
    athena_js_gil_lock();
    if (result == ATHENA_SYSTEM_ERR_NO_MEMCARD) {
        return JS_ThrowInternalError(ctx,
            "System.getMCInfo: memory card support (memcard module) is not in this build");
    }
    if (result < 0) {
        return JS_ThrowInternalError(ctx,
            "Unable to read memory-card information: %d", result);
    }

    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "type", JS_NewInt32(ctx, memory_card.type));
    JS_SetPropertyStr(ctx, info, "freemem", JS_NewInt32(ctx, memory_card.free_space));
    JS_SetPropertyStr(ctx, info, "format", JS_NewInt32(ctx, memory_card.format));
    return info;
}

static JSValue athena_system_load_elf(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc < 1 || argc > 2) return JS_ThrowTypeError(ctx, "System.loadELF(path[, args]) accepts one or two arguments");
    char *path;
    if (!athena_system_path_arg(ctx, argv[0], &path)) return JS_EXCEPTION;

    int32_t arg_count = 0;
    char **args = NULL;
    if (argc == 2) {
        if (!JS_IsArray(ctx, argv[1])) {
            free(path);
            return JS_ThrowTypeError(ctx, "System.loadELF args must be an array");
        }
        JSValue length = JS_GetPropertyStr(ctx, argv[1], "length");
        if (JS_ToInt32(ctx, &arg_count, length)) {
            JS_FreeValue(ctx, length);
            free(path);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, length);
        if (arg_count < 0 ||
            (size_t)arg_count > (((size_t)-1 / sizeof(char *)) - 1)) {
            free(path);
            return JS_ThrowTypeError(ctx, "System.loadELF args length must be non-negative and fit in memory");
        }
        args = malloc(sizeof(char *) * (arg_count + 1));
        if (!args) {
            free(path);
            return JS_ThrowOutOfMemory(ctx);
        }
        for (int32_t i = 0; i < arg_count; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
            const char *item_string = JS_ToCString(ctx, item);
            args[i] = NULL;
            if (item_string) {
                size_t item_length = strlen(item_string) + 1;
                args[i] = malloc(item_length);
                if (args[i]) memcpy(args[i], item_string, item_length);
                JS_FreeCString(ctx, item_string);
            }
            JS_FreeValue(ctx, item);
            if (!args[i]) {
                for (int32_t j = 0; j < i; j++) free(args[j]);
                free(args);
                free(path);
                return JS_EXCEPTION;
            }
        }
        args[arg_count] = NULL;
    }

    athena_js_gil_unlock();
    int result = LoadELFFromFileNoReset(path, arg_count, args);
    athena_js_gil_lock();
    if (args) {
        for (int32_t i = 0; i < arg_count; i++) free(args[i]);
        free(args);
    }
    free(path);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_mount(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (argc < 2 || argc > 3) return JS_ThrowTypeError(ctx, "System.mount(mountpoint, blockdev[, mode])");
    char *mountpoint;
    char *blockdev;
    if (!athena_system_path_arg(ctx, argv[0], &mountpoint)) return JS_EXCEPTION;
    if (!athena_system_path_arg(ctx, argv[1], &blockdev)) {
        free(mountpoint);
        return JS_EXCEPTION;
    }
    int32_t mode = 0;
    if (argc == 3 && JS_ToInt32(ctx, &mode, argv[2])) {
        free(mountpoint);
        free(blockdev);
        return JS_EXCEPTION;
    }
    athena_js_gil_unlock();
    int result = athena_system_mount_native(mountpoint, blockdev, mode);
    athena_js_gil_lock();
    free(mountpoint);
    free(blockdev);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_umount(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 1, "System.umount")) return JS_EXCEPTION;
    char *device;
    if (!athena_system_path_arg(ctx, argv[0], &device)) return JS_EXCEPTION;
    athena_js_gil_unlock();
    int result = athena_system_umount_native(device);
    athena_js_gil_lock();
    free(device);
    return JS_NewInt32(ctx, result);
}

static JSValue athena_system_devices(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    struct fileXioDevice devices[FILEXIO_MAX_DEVICES];
    athena_js_gil_unlock();
    int count = fileXioGetDeviceList(devices, FILEXIO_MAX_DEVICES);
    athena_js_gil_lock();
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
    if (!athena_system_require_argc(ctx, argc, 1, "System.getBDMInfo")) return JS_EXCEPTION;
    char *device;
    if (!athena_system_path_arg(ctx, argv[0], &device)) return JS_EXCEPTION;
    athena_js_gil_unlock();
    int fd = fileXioDopen(device);
    if (fd < 0) {
        athena_js_gil_lock();
        free(device);
        return JS_UNDEFINED;
    }

    char driver[10] = { 0 };
    int result = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driver, sizeof(driver) - 1);
    fileXioDclose(fd);
    athena_js_gil_lock();
    if (result < 0) {
        free(device);
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
    free(device);
    return info;
}

static JSValue athena_system_get_cpu_info(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getCPUInfo")) return JS_EXCEPTION;
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
    if (!athena_system_require_argc(ctx, argc, 0, "System.getGPUInfo")) return JS_EXCEPTION;
    volatile uint64_t *gs_csr = (volatile uint64_t *)0x12001000;
    uint16_t revision = (uint16_t)(*gs_csr >> 16);
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "revision", JS_NewInt32(ctx, revision & 0xff));
    JS_SetPropertyStr(ctx, info, "id", JS_NewInt32(ctx, (revision >> 8) & 0xff));
    return info;
}

static JSValue athena_system_get_temperature(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getTemperature")) return JS_EXCEPTION;
    unsigned char command[1] = { 0xef };
    unsigned char response[16] = { 0 };
    athena_js_gil_unlock();
    int command_result = sceCdApplySCmd(0x03, command, sizeof(command), response);
    athena_js_gil_lock();
    if (command_result == 0 || response[0] == 0) {
        return JS_UNDEFINED;
    }
    uint16_t raw = ((uint16_t)response[1] << 8) | response[2];
    return JS_NewFloat64(ctx, (double)(raw / 128) + (double)(raw % 128) / 10.0);
}

static JSValue athena_system_get_memory_stats(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getMemoryStats")) return JS_EXCEPTION;
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "core", JS_NewUint32(ctx, (uint32_t)get_binary_size()));
    JS_SetPropertyStr(ctx, info, "nativeStack", JS_NewUint32(ctx, (uint32_t)get_stack_size()));
    JS_SetPropertyStr(ctx, info, "allocs", JS_NewUint32(ctx, (uint32_t)get_allocs_size()));
    JS_SetPropertyStr(ctx, info, "used", JS_NewUint32(ctx, (uint32_t)get_used_memory()));
    {
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(JS_GetRuntime(ctx), &usage);
        /*
         * QuickJS adds an estimated 8-byte header (MALLOC_OVERHEAD) to each
         * live block; drop it so jsHeap is measured like allocs and the two
         * can be subtracted.
         */
        JS_SetPropertyStr(ctx, info, "jsHeap",
            JS_NewInt64(ctx, usage.malloc_size - 8 * usage.malloc_count));
        JS_SetPropertyStr(ctx, info, "jsLimit", JS_NewInt64(ctx, usage.malloc_limit));
        JS_SetPropertyStr(ctx, info, "jsObjects", JS_NewInt64(ctx, usage.obj_count));
    }
    return info;
}

static JSValue athena_system_get_ticks(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getTicks")) return JS_EXCEPTION;
    return JS_NewInt64(ctx, (int64_t)athena_system_get_ticks_native());
}

static JSValue athena_system_get_ms(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getMilliseconds")) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, athena_system_get_milliseconds_native());
}

static JSValue athena_system_sleep(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 1, "System.sleep")) return JS_EXCEPTION;
    int32_t ms = 0;
    if (JS_ToInt32(ctx, &ms, argv[0])) {
        return JS_EXCEPTION;
    }
    athena_js_gil_unlock();
    athena_system_sleep_native(ms);
    athena_js_gil_lock();
    return JS_UNDEFINED;
}

static JSValue athena_system_get_used_memory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getUsedMemory")) return JS_EXCEPTION;
    return JS_NewUint32(ctx, athena_system_get_used_memory_native());
}

static JSValue athena_system_get_free_memory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.getFreeMemory")) return JS_EXCEPTION;
    return JS_NewUint32(ctx, athena_system_get_free_memory_native());
}

static JSValue athena_system_gc(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.gc")) return JS_EXCEPTION;
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static JSValue athena_system_exit(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    if (!athena_system_require_argc(ctx, argc, 0, "System.exit")) return JS_EXCEPTION;
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
    JS_PROP_STRING_DEF("bootPath", boot_path, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("boot_path", boot_path, JS_PROP_ENUMERABLE),
};

static int athena_system_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, system_module_funcs, countof(system_module_funcs));
}

JSModuleDef *athena_system_init(JSContext* ctx) {
    return athena_push_module(ctx, athena_system_module_init, system_module_funcs, countof(system_module_funcs), "System");
}

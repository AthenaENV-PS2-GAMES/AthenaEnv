#include <stdarg.h>
#include <stdio.h>

#include <audsrv.h>

#include <athena/debug.h>
#include <athena/iop_manager.h>
#include <athena/sound.h>

#include "sound_internal.h"

iopman_define_module(libsd);
iopman_define_module(audsrv);

static module_entry *audsrv_entry;
static bool audsrv_ready;
/* audsrv_init() was called for the driver now resident on the IOP. */
static bool audsrv_attempted;
static uint32_t audsrv_generation;

/*
 * audsrv's init/end callbacks in the IOP manager. They run whenever the
 * driver is loaded (first use, `audsrv = true` in athena.ini at boot,
 * IOP.loadModule) and before an IOP reset, so the EE side follows the IOP.
 */
static int sound_audsrv_started(void *module) {
    int result;

    (void)module;
    /*
     * The EE side of audsrv has no guard of its own: a second audsrv_init()
     * binds the RPC again and starts another callback thread. A failure is
     * not retried until the driver is loaded again.
     */
    if (audsrv_attempted)
        return audsrv_ready ? 0 : -1;
    audsrv_attempted = true;
    result = audsrv_init();
    if (result != AUDSRV_ERR_NOERROR) {
        dbgprintf("[Sound] audsrv_init failed: %d\n", result);
        return result;
    }
    audsrv_ready = true;
    audsrv_generation++;
    /* The driver's _start already ran audsrv_adpcm_init(): voices are off. */
    sound_sfx_audsrv_started();
    sound_stream_audsrv_started();
    dbgprintf("[Sound] audsrv ready\n");
    return 0;
}

static int sound_audsrv_stopping(void *module) {
    (void)module;
    audsrv_attempted = false;
    if (!audsrv_ready)
        return 0;
    sound_stream_halt();
    audsrv_ready = false;
    audsrv_quit();
    return 0;
}

/*
 * Boot hook: only registers the drivers, so `audsrv = true` in athena.ini and
 * IOP.getModule("audsrv") keep working. Nothing is loaded until it is used.
 */
void athena_sound_register_iop(void) {
    static const char *const no_dependencies[] = { NULL };
    static const char *const after_libsd[] = { "libsd", NULL };
    module_entry *entry;

    if (!iopman_ensure_module_buffer("libsd", libsd, no_dependencies, NULL, NULL))
        return;
    entry = iopman_ensure_module_buffer("audsrv", audsrv, after_libsd,
        sound_audsrv_started, sound_audsrv_stopping);
    if (!entry)
        return;
    /* Registered by another module without hooks: the EE side is ours. */
    if (!entry->init && !entry->end) {
        entry->init = sound_audsrv_started;
        entry->end = sound_audsrv_stopping;
    }
    if (entry->init != sound_audsrv_started)
        dbgprintf("[Sound] audsrv was registered by another module\n");
    audsrv_entry = entry;
}

void athena_sound_module_shutdown(void) {
    if (!audsrv_ready)
        return;
    sound_stream_halt();
    audsrv_adpcm_init();
}

/*
 * Loads audsrv on the IOP once. The IOP manager skips modules already
 * started, so this never runs SifExecModuleBuffer twice for the same driver.
 */
int athena_sound_ensure(void) {
    int status;

    if (audsrv_ready)
        return ATHENA_SOUND_OK;
    if (!audsrv_entry) {
        dbgprintf("[Sound] audsrv is not registered\n");
        return ATHENA_SOUND_ERR_IOP;
    }
    if (!audsrv_entry->started) {
        status = iopman_load_module(audsrv_entry, 0, NULL);
        if (status != MODULE_STATUS_LOADED) {
            dbgprintf("[Sound] failed to load audsrv (%d)\n", status);
            return ATHENA_SOUND_ERR_IOP;
        }
    }
    /* Loaded through another module's hooks: ours did not run. */
    if (audsrv_entry->init != sound_audsrv_started)
        sound_audsrv_started(audsrv_entry);
    /* Started, but audsrv_init failed: not retried until the next reset. */
    return audsrv_ready ? ATHENA_SOUND_OK : ATHENA_SOUND_ERR_IOP;
}

bool athena_sound_ready(void) {
    return audsrv_ready;
}

uint32_t sound_iop_generation(void) {
    return audsrv_generation;
}

const char *athena_sound_result_string(int result) {
    switch (result) {
    case ATHENA_SOUND_OK: return "ok";
    case ATHENA_SOUND_ERR_ARGS: return "invalid argument";
    case ATHENA_SOUND_ERR_IOP: return "audsrv could not be started on the IOP";
    case ATHENA_SOUND_ERR_OPEN: return "cannot open file";
    case ATHENA_SOUND_ERR_READ: return "cannot read file";
    case ATHENA_SOUND_ERR_FORMAT: return "unsupported audio format";
    case ATHENA_SOUND_ERR_MEMORY: return "out of memory";
    case ATHENA_SOUND_ERR_SPU_MEMORY: return "not enough SPU2/IOP memory for the sample";
    case ATHENA_SOUND_ERR_CORRUPT: return "corrupt ADPCM data";
    case ATHENA_SOUND_ERR_THREAD: return "cannot start the streaming thread";
    default: return "unknown error";
    }
}

const char *athena_sound_result_code(int result) {
    switch (result) {
    case ATHENA_SOUND_ERR_ARGS: return "INVALID_ARGUMENT";
    case ATHENA_SOUND_ERR_IOP: return "IOP";
    case ATHENA_SOUND_ERR_OPEN: return "NOT_FOUND";
    case ATHENA_SOUND_ERR_READ: return "IO";
    case ATHENA_SOUND_ERR_FORMAT: return "BAD_FORMAT";
    case ATHENA_SOUND_ERR_MEMORY: return "NO_MEMORY";
    case ATHENA_SOUND_ERR_SPU_MEMORY: return "SPU_MEMORY";
    case ATHENA_SOUND_ERR_CORRUPT: return "CORRUPT";
    case ATHENA_SOUND_ERR_THREAD: return "THREAD";
    default: return "UNKNOWN";
    }
}

/* Written and read on the script thread only (open/load paths). */
static char sound_detail[160];

void sound_set_detail(const char *fmt, ...) {
    va_list args;

    if (!fmt) {
        sound_detail[0] = '\0';
        return;
    }
    va_start(args, fmt);
    vsnprintf(sound_detail, sizeof(sound_detail), fmt, args);
    va_end(args);
}

const char *athena_sound_error_detail(void) {
    return sound_detail;
}

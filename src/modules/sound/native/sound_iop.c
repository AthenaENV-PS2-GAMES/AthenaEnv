#include <stdio.h>

#include <audsrv.h>

#include <athena/debug.h>
#include <athena/iop_manager.h>
#include <athena/sound.h>

#include "sound_internal.h"

iopman_define_module(libsd);
iopman_define_module(audsrv);

static bool audsrv_ready;
static uint32_t audsrv_generation;

/*
 * audsrv's init/end callbacks in the IOP manager. They run whenever the
 * driver is loaded (first use, `audsrv = true` in athena.ini at boot,
 * IOP.loadModule) and before an IOP reset, so the EE side follows the IOP.
 */
static int sound_audsrv_started(void *module) {
    int result;

    (void)module;
    result = audsrv_init();
    if (result != AUDSRV_ERR_NOERROR) {
        dbgprintf("[Sound] audsrv_init failed: %d\n", result);
        return result;
    }
    audsrv_ready = true;
    audsrv_generation++;
    /* Stops every voice and drops the samples of a previous session. */
    audsrv_adpcm_init();
    sound_sfx_audsrv_started();
    sound_stream_audsrv_started();
    dbgprintf("[Sound] audsrv ready\n");
    return 0;
}

static int sound_audsrv_stopping(void *module) {
    (void)module;
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
    if (entry && entry->init != sound_audsrv_started)
        dbgprintf("[Sound] audsrv was registered by another module\n");
}

void athena_sound_module_shutdown(void) {
    if (!audsrv_ready)
        return;
    sound_stream_halt();
    audsrv_adpcm_init();
}

int athena_sound_ensure(void) {
    module_entry *entry;
    int status;

    if (audsrv_ready)
        return ATHENA_SOUND_OK;
    entry = iopman_search_module("audsrv");
    if (!entry) {
        dbgprintf("[Sound] audsrv is not registered\n");
        return ATHENA_SOUND_ERR_IOP;
    }
    if (!entry->started) {
        status = iopman_load_module(entry, 0, NULL);
        if (status != MODULE_STATUS_LOADED) {
            dbgprintf("[Sound] failed to load audsrv (%d)\n", status);
            return ATHENA_SOUND_ERR_IOP;
        }
    }
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
    case ATHENA_SOUND_ERR_STALE: return "sound effect was unloaded by an IOP reset; load it again";
    case ATHENA_SOUND_ERR_THREAD: return "cannot start the streaming thread";
    default: return "unknown error";
    }
}

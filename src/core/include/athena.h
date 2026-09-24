#ifndef ATHENA_H
#define ATHENA_H

/*
 * Umbrella header for C applications built on AthenaEnv (RUNTIME=native).
 * Module APIs are included on their own, e.g. <athena/graphics.h>,
 * <athena/gamepad.h>;
 * <athena/config.h> tells which modules this build contains
 * (ATHENA_MODULE_<ID>).
 */

#include <athena/config.h>
#include <athena/boot.h>
#include <athena/module.h>
#include <athena/memory.h>
#include <athena/iop_manager.h>
#include <athena/debug.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented by the application when linking against the native runtime. */
int athena_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* ATHENA_H */

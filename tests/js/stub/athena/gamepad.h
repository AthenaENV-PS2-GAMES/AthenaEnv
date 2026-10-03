/* Host stand-in for <athena/gamepad.h>: a pad whose buttons scripts set (runner.c). */
#ifndef ATHENA_GAMEPAD_H
#define ATHENA_GAMEPAD_H

#include <stdint.h>

typedef int AthenaGamepadResult;

AthenaGamepadResult athena_gamepad_core_init(void);
uint16_t athena_gamepad_core_peek(int port);

#endif

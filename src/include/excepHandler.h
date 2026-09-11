/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void installExceptionHandlers(void);
void iopException(int cause, int badvaddr, int status, int epc, u32 *regs, int repc, char *name);
void athena_display_crash_screen(const char *title, const char *err_msg, bool dark_mode);

#ifdef __cplusplus
}
#endif

#endif

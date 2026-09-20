#ifndef ATHENA_KEYBOARD_H
#define ATHENA_KEYBOARD_H

#ifdef ATHENA_KEYBOARD

#include <stdint.h>

int athena_keyboard_open(void);
int athena_keyboard_read(char *key);
int athena_keyboard_read_raw(uint8_t *state, uint8_t *key);
int athena_keyboard_set_read_mode(uint32_t mode);
int athena_keyboard_set_repeat_rate(unsigned int msec);
int athena_keyboard_set_blocking_mode(unsigned int mode);
int athena_keyboard_set_keymap(const uint8_t *keymap, const uint8_t *shiftkeymap, const uint8_t *keycap);
int athena_keyboard_reset_keymap(void);
int athena_keyboard_deinit(void);

#endif /* ATHENA_KEYBOARD */

#endif /* ATHENA_KEYBOARD_H */

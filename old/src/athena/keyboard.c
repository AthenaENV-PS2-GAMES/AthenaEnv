#ifdef ATHENA_KEYBOARD

#include <string.h>

#include <athena/keyboard.h>
#include <libkbd.h>

int athena_keyboard_open(void)
{
    return PS2KbdInit();
}

int athena_keyboard_read(char *key)
{
    return PS2KbdRead(key);
}

int athena_keyboard_read_raw(uint8_t *state, uint8_t *key)
{
    PS2KbdRawKey raw;
    int ret = PS2KbdReadRaw(&raw);

    if (ret > 0) {
        *state = raw.state;
        *key = raw.key;
    }

    return ret;
}

int athena_keyboard_set_read_mode(uint32_t mode)
{
    return PS2KbdSetReadmode(mode);
}

int athena_keyboard_set_repeat_rate(unsigned int msec)
{
    return PS2KbdSetRepeatRate(msec);
}

int athena_keyboard_set_blocking_mode(unsigned int mode)
{
    return PS2KbdSetBlockingMode(mode);
}

int athena_keyboard_set_keymap(const uint8_t *keymap, const uint8_t *shiftkeymap, const uint8_t *keycap)
{
    PS2KbdKeyMap map;

    memcpy(map.keymap, keymap, PS2KBD_KEYMAP_SIZE);
    memcpy(map.shiftkeymap, shiftkeymap, PS2KBD_KEYMAP_SIZE);

    if (keycap)
        memcpy(map.keycap, keycap, PS2KBD_KEYMAP_SIZE);
    else
        memset(map.keycap, 0, PS2KBD_KEYMAP_SIZE);

    return PS2KbdSetKeymap(&map);
}

int athena_keyboard_reset_keymap(void)
{
    return PS2KbdResetKeymap();
}

int athena_keyboard_deinit(void)
{
    return PS2KbdClose();
}

#endif /* ATHENA_KEYBOARD */

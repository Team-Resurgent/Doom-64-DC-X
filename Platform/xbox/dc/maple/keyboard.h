/* dc/maple/keyboard.h -- Xbox shim: KallistiOS v2.2.1's maple keyboard types.
 *
 * Doom 64 reads a Dreamcast keyboard when one is on the bus
 * (src/i_main.c I_GetControllerData). The Xbox bus here reports none --
 * maple_enum_type(n, MAPLE_FUNC_KEYBOARD) is always NULL -- so these are the
 * types and key codes that code compiles against, with KOS's names and
 * values (KOS licence, Platform/xbox/LICENSE.KOS). */
#ifndef XBOX_DC_MAPLE_KEYBOARD_H
#define XBOX_DC_MAPLE_KEYBOARD_H

#include <kos/cdefs.h>
#include <dc/maple.h>

__BEGIN_DECLS

#define KBD_MOD_LCTRL       BIT(0)
#define KBD_MOD_LSHIFT      BIT(1)
#define KBD_MOD_LALT        BIT(2)
#define KBD_MOD_S1          BIT(3)
#define KBD_MOD_RCTRL       BIT(4)
#define KBD_MOD_RSHIFT      BIT(5)
#define KBD_MOD_RALT        BIT(6)
#define KBD_MOD_S2          BIT(7)

typedef union kbd_mods {
    struct {
        uint8_t lctrl   : 1;
        uint8_t lshift  : 1;
        uint8_t lalt    : 1;
        uint8_t s1      : 1;
        uint8_t rctrl   : 1;
        uint8_t rshift  : 1;
        uint8_t ralt    : 1;
        uint8_t s2      : 1;
    };
    uint8_t raw;
} kbd_mods_t;

typedef union kbd_leds {
    uint8_t raw;
} kbd_leds_t;

typedef enum kbd_key {
    KBD_KEY_NONE         = 0x00,
    KBD_KEY_ERROR        = 0x01,
    KBD_KEY_A            = 0x04,
    KBD_KEY_D            = 0x07,
    KBD_KEY_F            = 0x09,
    KBD_KEY_S            = 0x16,
    KBD_KEY_W            = 0x1a,
    KBD_KEY_ENTER        = 0x28,
    KBD_KEY_ESCAPE       = 0x29,
    KBD_KEY_BACKSPACE    = 0x2a,
    KBD_KEY_TAB          = 0x2b,
    KBD_KEY_SPACE        = 0x2c,
    KBD_KEY_COMMA        = 0x36,
    KBD_KEY_PERIOD       = 0x37,
    KBD_KEY_PGUP         = 0x4b,
    KBD_KEY_PGDOWN       = 0x4e,
    KBD_KEY_RIGHT        = 0x4f,
    KBD_KEY_LEFT         = 0x50,
    KBD_KEY_DOWN         = 0x51,
    KBD_KEY_UP           = 0x52,
    KBD_KEY_PAD_MINUS    = 0x56,
    KBD_KEY_PAD_PLUS     = 0x57,
    KBD_KEY_PAD_ENTER    = 0x58,
} kbd_key_t;

#define KBD_MAX_PRESSED_KEYS 6
#define MAX_PRESSED_KEYS KBD_MAX_PRESSED_KEYS

typedef struct {
    kbd_mods_t modifiers;
    kbd_leds_t leds;
    uint8_t keys[KBD_MAX_PRESSED_KEYS];
} kbd_cond_t;

typedef struct kbd_state {
    kbd_cond_t cond;
    uint8_t matrix[256];
    int shift_keys;
    int region;
} kbd_state_t;

__END_DECLS

#endif /* XBOX_DC_MAPLE_KEYBOARD_H */

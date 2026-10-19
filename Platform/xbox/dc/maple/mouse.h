/* dc/maple/mouse.h -- Xbox shim: KallistiOS's maple mouse types.
 *
 * As with the keyboard (dc/maple/keyboard.h), the Xbox bus reports no mouse;
 * these are what src/i_main.c compiles against, with KOS's names and values
 * (KOS licence, Platform/xbox/LICENSE.KOS). */
#ifndef XBOX_DC_MAPLE_MOUSE_H
#define XBOX_DC_MAPLE_MOUSE_H

#include <kos/cdefs.h>
#include <dc/maple.h>

__BEGIN_DECLS

#define MOUSE_RIGHTBUTTON   BIT(1)
#define MOUSE_LEFTBUTTON    BIT(2)
#define MOUSE_SIDEBUTTON    BIT(3)

#define MOUSE_DELTA_CENTER  0x200

typedef struct mouse_state {
    uint32_t buttons;
    int dx;
    int dy;
    int dz;
} mouse_state_t;

__END_DECLS

#endif /* XBOX_DC_MAPLE_MOUSE_H */

/* kos/dbgio.h -- Xbox shim: KallistiOS's debug-console switch. Off = the
 * debug log (kos/dbglog.h) writes nothing (Platform/xbox/kos_log_xbox.c). */
#ifndef XBOX_KOS_DBGIO_H
#define XBOX_KOS_DBGIO_H

#include <kos/cdefs.h>

__BEGIN_DECLS

void dbgio_enable(void);
void dbgio_disable(void);

/* KOS's debug console: Doom 64 prints its progress and errors with
 * dbgio_printf, to "serial" or the framebuffer ("fb"). Both are the debug
 * channel here (kos_log_xbox.c); an I_Error's message is also drawn on the
 * screen by the game itself. */
int dbgio_dev_select(const char *name);
int dbgio_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

__END_DECLS

#endif /* XBOX_KOS_DBGIO_H */

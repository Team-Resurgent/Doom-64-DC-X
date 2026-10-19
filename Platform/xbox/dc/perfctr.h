/* dc/perfctr.h -- Xbox shim: the SH-4 performance counter's timer mode,
 * which KOS uses as a nanosecond clock (Platform/xbox/kos_sys_xbox.c). */
#ifndef XBOX_DC_PERFCTR_H
#define XBOX_DC_PERFCTR_H

#include <kos/cdefs.h>

__BEGIN_DECLS

uint64_t perf_cntr_timer_ns(void);

__END_DECLS

#endif /* XBOX_DC_PERFCTR_H */

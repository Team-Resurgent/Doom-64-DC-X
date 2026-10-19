/* dc/vector.h -- Xbox shim: KallistiOS's matrix, vector and point types
 * (KOS dc/vector.h, KOS licence, Platform/xbox/LICENSE.KOS). */
#ifndef XBOX_DC_VECTOR_H
#define XBOX_DC_VECTOR_H

#include <kos/cdefs.h>

__BEGIN_DECLS

typedef __attribute__((aligned(8))) float matrix_t[4][4];

typedef struct vectorstr {
    float x, y, z, w;
} vector_t;

typedef vector_t point_t;

__END_DECLS

#endif /* XBOX_DC_VECTOR_H */

/* dc/matrix.h -- Xbox shim: the SH-4's matrix unit (KallistiOS dc/matrix.h).
 *
 * The SH-4 keeps one 4x4 matrix in its back floating-point bank (XMTRX);
 * mat_load and mat_apply set it, ftrv transforms a vector by it. Doom 64
 * composes its viewport, projection and view matrices into it once a frame
 * (src/r_main.c) and transforms every vertex with mat_trans_single3_nodivw.
 * Here XMTRX is one matrix in memory (Platform/xbox/kos_matrix_xbox.c):
 * only the game's own thread uses it, as on the Dreamcast. KOS's layout: matrix_t is [column][row], so element [j][i] multiplies
 * input component j into output component i -- what ftrv computes from the
 * matrix fmov'd in by mat_load. */
#ifndef XBOX_DC_MATRIX_H
#define XBOX_DC_MATRIX_H

#include <kos/cdefs.h>
#include <dc/vector.h>

__BEGIN_DECLS

typedef struct xbox_xmtrx {
    float m[4][4];
} xbox_xmtrx_t;

extern xbox_xmtrx_t xbox_xmtrx_reg;

void mat_store(matrix_t *out);
void mat_load(const matrix_t *src);
void mat_identity(void);
void mat_apply(const matrix_t *src);
void mat_multiply(matrix_t *dst, const matrix_t *src);
void mat_transform(const vector_t *invecs, vector_t *outvecs, int veccnt, int vecskip);

/* ftrv: (x, y, z, w) by XMTRX */
#define XBOX_FTRV(x, y, z, w_in, ox, oy, oz, ow) do { \
        const xbox_xmtrx_t *__m = &xbox_xmtrx_reg; \
        float __x = (x), __y = (y), __z = (z), __w = (w_in); \
        ox = __m->m[0][0] * __x + __m->m[1][0] * __y + __m->m[2][0] * __z + __m->m[3][0] * __w; \
        oy = __m->m[0][1] * __x + __m->m[1][1] * __y + __m->m[2][1] * __z + __m->m[3][1] * __w; \
        oz = __m->m[0][2] * __x + __m->m[1][2] * __y + __m->m[2][2] * __z + __m->m[3][2] * __w; \
        ow = __m->m[0][3] * __x + __m->m[1][3] * __y + __m->m[2][3] * __z + __m->m[3][3] * __w; \
    } while (0)

#define mat_trans_single3_nodivw(x, y, z, w) do { \
        float __ox, __oy, __oz, __ow; \
        XBOX_FTRV(x, y, z, 1.0f, __ox, __oy, __oz, __ow); \
        x = __ox; y = __oy; z = __oz; w = __ow; \
    } while (0)

#define mat_trans_single3_nodiv(x, y, z) do { \
        float __ox, __oy, __oz, __ow; \
        XBOX_FTRV(x, y, z, 1.0f, __ox, __oy, __oz, __ow); \
        (void)__ow; \
        x = __ox; y = __oy; z = __oz; \
    } while (0)

#define mat_trans_single(x, y, z) do { \
        float __ox, __oy, __oz, __ow; \
        XBOX_FTRV(x, y, z, 1.0f, __ox, __oy, __oz, __ow); \
        __ow = 1.0f / __ow; \
        x = __ox * __ow; y = __oy * __ow; z = __ow; \
    } while (0)

#define mat_trans_single3(x, y, z) do { \
        float __ox, __oy, __oz, __ow; \
        XBOX_FTRV(x, y, z, 1.0f, __ox, __oy, __oz, __ow); \
        __ow = 1.0f / __ow; \
        x = __ox * __ow; y = __oy * __ow; z = __oz * __ow; \
    } while (0)

#define mat_trans_single4(x, y, z, w) do { \
        float __ox, __oy, __oz, __ow; \
        XBOX_FTRV(x, y, z, w, __ox, __oy, __oz, __ow); \
        __ow = 1.0f / __ow; \
        x = __ox * __ow; y = __oy * __ow; z = __oz * __ow; w = __ow; \
    } while (0)

#define mat_trans_nodiv(x, y, z, w) do { \
        float __ox, __oy, __oz, __ow; \
        XBOX_FTRV(x, y, z, w, __ox, __oy, __oz, __ow); \
        x = __ox; y = __oy; z = __oz; w = __ow; \
    } while (0)

__END_DECLS

#endif /* XBOX_DC_MATRIX_H */

/* dc/fmath.h -- Xbox shim: KallistiOS's SH-4 fast maths (dc/fmath.h and
 * dc/fmath_base.h, KOS licence), as the SH-4 computes it.
 *
 *   fipr       a 4-component dot product
 *   fsin/fcos  fsca: radians * 10430.37835, truncated (ftrc), low 16 bits =
 *              a binary angle -- so the result is the sine of the QUANTISED
 *              angle, as on the Dreamcast (sh4zam_x86.h shz_sincosf)
 *   fisin...   the same from an integer binary angle
 *   fsincos    degrees * 182.04444443 (65536 / 360), same quantisation
 *   fsqrt      fsqrt; frsqrt = 1 / sqrt (fsrra) */
#ifndef XBOX_DC_FMATH_H
#define XBOX_DC_FMATH_H

#include <kos/cdefs.h>
#include <math.h>
#include "../sh4zam_x86.h"

__BEGIN_DECLS

/* KOS dc/fmath_base.h */
#ifndef F_PI
#define F_PI 3.1415926f
#endif

static inline float fipr(float x, float y, float z, float w, float a, float b, float c, float d) {
    return x * a + y * b + z * c + w * d;
}

static inline float fipr_magnitude_sqr(float x, float y, float z, float w) {
    return x * x + y * y + z * z + w * w;
}

static inline float fsin(float r) {
    return shz_sincosf(r).sin;
}

static inline float fcos(float r) {
    return shz_sincosf(r).cos;
}

static inline float ftan(float r) {
    shz_sincos_t sc = shz_sincosf(r);
    return sc.sin / sc.cos;
}

static inline float fisin(int d) {
    return shz_sincosu16((uint16_t) d).sin;
}

static inline float ficos(int d) {
    return shz_sincosu16((uint16_t) d).cos;
}

static inline float fitan(int d) {
    shz_sincos_t sc = shz_sincosu16((uint16_t) d);
    return sc.sin / sc.cos;
}

static inline float fsqrt(float f) {
    return sqrtf(f);
}

static inline float frsqrt(float f) {
    return 1.0f / sqrtf(f);
}

static inline void fsincos(float f, float *s, float *c) {
    shz_sincos_t sc = shz_sincosu16((uint16_t) shz_ftrc_(f * 182.04444443f));
    *s = sc.sin;
    *c = sc.cos;
}

static inline void fsincosr(float f, float *s, float *c) {
    shz_sincos_t sc = shz_sincosf(f);
    *s = sc.sin;
    *c = sc.cos;
}

/* KOS dc/fmath.h: the PVR's bump-map parameters as an offset colour --
 * K1 (alpha) = 255 - h, K2 (red) = h sin t, K3 (green) = h cos t, Q (blue)
 * = q as a fraction of a turn (the PVR's own formula, which pvr_xbox.c's
 * bump pass evaluates) */
static inline uint32_t pvr_pack_bump(float h, float t, float q) {
    uint8_t hp = (uint8_t)(h * 255.0f);
    uint8_t k1 = (uint8_t)~hp;
    uint8_t k2 = (uint8_t)(hp * fsin(t));
    uint8_t k3 = (uint8_t)(hp * fcos(t));
    uint8_t qp = (uint8_t)((q / (2 * F_PI)) * 255.0f);

    return ((uint32_t)k1 << 24) | ((uint32_t)k2 << 16) | ((uint32_t)k3 << 8) | qp;
}

__END_DECLS

#endif /* XBOX_DC_FMATH_H */

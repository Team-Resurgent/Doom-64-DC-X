/* dc/vec3f.h -- Xbox shim: KallistiOS's three-component vector helpers
 * (KOS dc/vec3f.h, KOS licence): its fipr / fsrra arithmetic in plain C. */
#ifndef XBOX_DC_VEC3F_H
#define XBOX_DC_VEC3F_H

#include <kos/cdefs.h>
#include <math.h>

__BEGIN_DECLS

typedef struct vec3f {
    float x, y, z;
} vec3f_t;

static inline float vec_dot(vec3f_t a, vec3f_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float vec_length(vec3f_t a)
{
    return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

static inline float vec_distance(vec3f_t a, vec3f_t b)
{
    vec3f_t d = { a.x - b.x, a.y - b.y, a.z - b.z };
    return vec_length(d);
}

/* fsrra of 0 is +inf on the SH-4 and the product NaN; a zero vector stays
 * zero here, which is what every caller in the port wants */
static inline vec3f_t vec_normalize(vec3f_t a)
{
    float l = a.x * a.x + a.y * a.y + a.z * a.z;
    vec3f_t r;

    l = (l > 0.0f) ? 1.0f / sqrtf(l) : 0.0f;
    r.x = a.x * l;
    r.y = a.y * l;
    r.z = a.z * l;
    return r;
}

static inline vec3f_t vec_sub_normalize(vec3f_t a, vec3f_t b)
{
    vec3f_t d = { a.x - b.x, a.y - b.y, a.z - b.z };
    return vec_normalize(d);
}

#define vec3f_dot(x1, y1, z1, x2, y2, z2, w) \
    w = vec_dot((vec3f_t){ x1, y1, z1 }, (vec3f_t){ x2, y2, z2 })

#define vec3f_length(x, y, z, w) \
    w = vec_length((vec3f_t){ x, y, z })

#define vec3f_distance(x1, y1, z1, x2, y2, z2, w) \
    w = vec_distance((vec3f_t){ x1, y1, z1 }, (vec3f_t){ x2, y2, z2 })

#define vec3f_normalize(__x, __y, __z) do { \
    vec3f_t __v = vec_normalize((vec3f_t){ __x, __y, __z }); \
    __x = __v.x; __y = __v.y; __z = __v.z; \
} while (0)

#define vec3f_sub_normalize(x1, y1, z1, x2, y2, z2, x3, y3, z3) do { \
    vec3f_t __v = vec_sub_normalize((vec3f_t){ x1, y1, z1 }, (vec3f_t){ x2, y2, z2 }); \
    x3 = __v.x; y3 = __v.y; z3 = __v.z; \
} while (0)

__END_DECLS

#endif /* XBOX_DC_VEC3F_H */

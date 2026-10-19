/* kos_doom_xbox.c -- the KallistiOS services Doom 64 uses beyond the ones
 * it shares with the other Xbox ports:
 *
 *   the SH-4 matrix unit (dc/matrix.h): XMTRX, mat_load / mat_apply ...
 *   perf_cntr_timer_ns, the performance counter's nanosecond clock
 *   fs_load, a whole file into a malloc'd buffer
 *   dbgio_dev_select / dbgio_printf, the debug console
 *   arch_valid_address / arch_valid_text_address, arch_stk_trace
 *   vmufb_print_string_into, the VMU screen's text (there is no VMU screen)
 *   array_fast_cpy / single_fast_cpy, the port's SH-4 assembly copies
 *     (array_fast_copy.S), in C
 */
#include "xbox_xtl.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos/dbglog.h>
#include <kos/dbgio.h>
#include <kos/fs.h>
#include <arch/arch.h>
#include <arch/timer.h>
#include <dc/matrix.h>
#include <dc/perfctr.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <dc/vmu_fb.h>

/* ========================================================= matrix unit
 * KOS kernel/arch/dreamcast/math/matrix.s: matrix_t is [column][row], and
 * mat_apply(src) leaves XMTRX * src (each of src's columns transformed by
 * XMTRX), so vertices are transformed by the last matrix applied first. */

xbox_xmtrx_t xbox_xmtrx_reg = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };

void mat_store(matrix_t *out)
{
    memcpy(out, xbox_xmtrx_reg.m, sizeof(matrix_t));
}

void mat_load(const matrix_t *src)
{
    memcpy(xbox_xmtrx_reg.m, src, sizeof(matrix_t));
}

void mat_identity(void)
{
    int i, j;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            xbox_xmtrx_reg.m[i][j] = (i == j) ? 1.0f : 0.0f;
}

void mat_apply(const matrix_t *src)
{
    float r[4][4];
    int i, j, k;

    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
        {
            float s = 0.0f;

            for (k = 0; k < 4; k++)
                s += xbox_xmtrx_reg.m[k][i] * (*src)[j][k];
            r[j][i] = s;
        }
    memcpy(xbox_xmtrx_reg.m, r, sizeof(r));
}

void mat_multiply(matrix_t *dst, const matrix_t *src)
{
    matrix_t r;
    int i, j, k;

    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
        {
            float s = 0.0f;

            for (k = 0; k < 4; k++)
                s += (*dst)[k][i] * (*src)[j][k];
            r[j][i] = s;
        }
    memcpy(dst, r, sizeof(r));
}

void mat_transform(const vector_t *invecs, vector_t *outvecs, int veccnt, int vecskip)
{
    const uint8_t *in = (const uint8_t *)invecs;
    uint8_t *out = (uint8_t *)outvecs;
    int i;

    for (i = 0; i < veccnt; i++)
    {
        const vector_t *v = (const vector_t *)in;
        vector_t *o = (vector_t *)out;
        float x, y, z, w;

        XBOX_FTRV(v->x, v->y, v->z, 1.0f, x, y, z, w);
        w = 1.0f / w;
        o->x = x * w;
        o->y = y * w;
        o->z = w;
        in += sizeof(vector_t) + vecskip;
        out += sizeof(vector_t) + vecskip;
    }
}

/* ================================================================ timers */

uint64_t perf_cntr_timer_ns(void)
{
    return timer_ns_gettime64();
}

/* ============================================================ fs_load */

ssize_t fs_load(const char *src, void **out_ptr)
{
    file_t f;
    ssize_t total, got = 0;
    uint8_t *buf;

    *out_ptr = NULL;
    f = fs_open(src, O_RDONLY);
    if (f == FILEHND_INVALID)
        return -1;
    total = fs_total(f);
    if (total < 0)
    {
        fs_close(f);
        return -1;
    }
    /* KOS allocates the file's size; one byte more so a text file (the
     * controller mapping) can be handed on as a string */
    buf = (uint8_t *)malloc((size_t)total + 1);
    if (buf == NULL)
    {
        fs_close(f);
        return -1;
    }
    while (got < total)
    {
        ssize_t n = fs_read(f, buf + got, (size_t)(total - got));

        if (n <= 0)
            break;
        got += n;
    }
    fs_close(f);
    if (got != total)
    {
        free(buf);
        return -1;
    }
    buf[total] = 0;
    *out_ptr = buf;
    return total;
}

/* ====================================================== debug console */

int dbgio_dev_select(const char *name)
{
    (void)name;
    return 0;
}

int dbgio_printf(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    dbglog(DBG_INFO, "%s", line);
    return n;
}

/* ===================================================== address checks */

/* The XBE's image: its header, at 0x10000, gives the image's size at
 * 0x10C (the base it is loaded at is 0x104). */
static uintptr_t image_end(void)
{
    static uintptr_t end;

    if (end == 0)
    {
        const uint32_t *hdr = (const uint32_t *)0x00010000u;
        uint32_t base = hdr[0x104 / 4], size = hdr[0x10C / 4];

        end = (base == 0x00010000u && size > 0 && size < 0x04000000u) ? base + size : 0x04000000u;
    }
    return end;
}

int arch_valid_text_address(uintptr_t ptr)
{
    return ptr >= 0x00010000u && ptr < image_end();
}

int arch_valid_address(uintptr_t ptr)
{
    /* the title's memory: the low 64 MB (128 on a development kit), and
     * the kernel's mirror of it the contiguous allocations live in */
    return (ptr >= 0x00010000u && ptr < 0x08000000u) ||
           (ptr >= 0x80000000u && ptr < 0x88000000u);
}

void arch_stk_trace_at(uint32_t fp, size_t n)
{
    (void)fp;
    (void)n;
}

void arch_stk_trace(int n)
{
    dbglog(DBG_ERROR, "arch_stk_trace: from %p\n", __builtin_return_address(0));
    (void)n;
}

/* ===================================================== VMU screen text */

void vmufb_print_string_into(vmufb_t *fb, const vmufb_font_t *font,
                             unsigned int x, unsigned int y,
                             unsigned int w, unsigned int h,
                             unsigned int line_spacing, const char *str)
{
    /* the VMU screen exists only to be shown on a VMU, and the Xbox has
     * none (vmufb_present); nothing reads the text back */
    (void)fb;
    (void)font;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)line_spacing;
    (void)str;
}

void vmufb_print_string(vmufb_t *fb, const vmufb_font_t *font, const char *str)
{
    vmufb_print_string_into(fb, font, 0, 0, VMU_SCREEN_WIDTH, VMU_SCREEN_HEIGHT, 0, str);
}

/* ============================================ the port's SH-4 copies
 * array_fast_copy.S: 32-byte blocks with paired-single moves, to memory or
 * to the store queues; the TA's queue address goes to the emulated TA. */

static void copy32(void *dst, const void *src)
{
    if ((uintptr_t)dst == (uintptr_t)SQ_MASK_DEST(PVR_TA_INPUT))
        pvr_prim(src, 32);
    else
        memcpy(dst, src, 32);
}

void array_fast_cpy(void **dst, const void **src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        copy32(dst[i], src[i]);
}

void single_fast_cpy(void *dst, const void *src)
{
    copy32(dst, src);
}

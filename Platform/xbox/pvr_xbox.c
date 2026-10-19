/* pvr_xbox.c -- KallistiOS's PowerVR API on the Xbox's NV2A (Direct3D 8).
 *
 * Doom 64's Dreamcast renderer (src/r_*.c, src/i_main.c, src/m_main.c ...)
 * draws through KOS's raw PVR API in KOS's "hybrid" mode: the opaque list
 * goes straight to the tile accelerator -- headers and vertices through the
 * store queues (sq_fast_cpy) and the direct-render slots (pvr_dr_*) -- while
 * the punch-through and translucent lists are collected in RAM, in buffers
 * the game hands KOS with pvr_set_vertbuf and writes either through
 * pvr_list_prim or in place (pvr_vertbuf_tail / pvr_vertbuf_written), and are
 * sent to the TA when the scene finishes. Vertices are in screen space: x/y,
 * z = 1/w, u/v, base and offset colour. Textures live in PVR memory. This
 * file is that API, so the game draws unchanged. (It began as the Super
 * Smash Bros. port's, SSB-X, by way of Star-Fox-64-X; the rules below are
 * the PVR's.)
 *
 * Texture memory. An 8 MB host buffer at a 16 MB boundary, so KOS's header
 * encoding of an address -- (addr & (8 MB - 1)) >> 3 -- names its offset.
 * pvr_txr_load / pvr_txr_load_ex write it exactly as KOS writes VRAM
 * (twiddled the same way), and bump a write generation for every 4 KB page
 * they touch; so do pvr_mem_malloc and pvr_mem_free, because the game also
 * writes freshly allocated texture memory directly (its bump maps,
 * src/bc5_decoder.c). A Direct3D texture is made from that memory the first
 * time a header names it, and made again when any page under it has been
 * written since: untwiddled (16-bit, 4/8-bit paletted, VQ, bump) into the
 * NV2A's swizzled layout, or for a non-twiddled strided texture, the
 * render-to-texture picture that address holds.
 *
 * Palettes. 1024 entries, as on the PVR, captured into a Direct3D palette
 * when a header is decoded. (Doom 64 loads its four banks once, at boot.)
 *
 * Depth. The game's z is 1/w, compared GREATER (larger = nearer), as
 * floats; the HUD sits at z 5 to 9, the screen wipe at 5. A fixed-point
 * depth buffer cannot hold that range, so the depth buffer is the NV2A's
 * 24-bit FLOAT one (D3DFMT_F24S8) and each vertex's depth is z / 16384
 * (clamped to 1): monotonic, so the comparisons map one to one, and affine
 * in 1/w, so the depth the rasteriser interpolates across a triangle is the
 * PVR's exactly. rhw is z itself, which is what the PVR uses for
 * perspective-correct texturing. The background plane (pvr_set_zclip) is
 * the depth the frame is cleared to.
 *
 * Lists. Opaque, then punch-through (alpha-tested at the PT reference the
 * game writes, 10), then translucent, as the PVR renders a tile. Neither of
 * the first two blends, whatever a header's blend fields say: a
 * punch-through pixel past the alpha test is written whole, one that fails
 * is not written at all. Doom draws a bump-mapped switch's colour there,
 * over its own bump result (src/r_phase3.c R_RenderSwitch), and the colour
 * replaces it -- multiplied over it, as a blend would, the switch came out
 * a dark square on Ultra. So a punch-through polygon is compared GEQUAL
 * where its header says GREATER: on its own bump result's depth a GREATER
 * would draw nothing (switches with no texture). And a grate's edges,
 * blended by their filtered alpha, would let through the bare bump light of
 * the room beyond: a white fringe round every hole.
 * The translucent list is autosorted on the PVR (pvr_init's
 * autosort_disabled = 0), per pixel, far to near; here it is sorted per
 * triangle, far to near by each triangle's farthest point, ties in submission
 * order (a stable radix sort). (By its nearest point, a big translucent
 * floor -- the top layer of water -- reaching towards the eye would draw
 * after a sprite standing over it, and over it, in triangles; per pixel the
 * floor under a sprite is the farther.) No order of whole triangles is
 * right for every pixel, though, so what is solid in the list is not
 * sorted at all: a translucent-list triangle that blends by its alpha (or
 * not at all), writes depth and has an opaque colour at every vertex -- the
 * base layer of water, a sprite at full alpha -- is drawn first, in the
 * order the game sent it, its texels of alpha 1 only, writing depth; only
 * its other texels join the sort, writing none. So the water's top layer
 * lands on its base wherever the base is split into small triangles (as it
 * is round the player, for dynamic lights) and a sprite hanging below a
 * floor is cut at the floor's edge, both per pixel, as on the PVR.
 * Every translucent polygon is compared
 * GEQUAL, which is the only compare autosort allows: Doom draws a surface's
 * colour over its own bump-map result at the very same depth. Those colour
 * passes (DESTCOLOR, ZERO: the texture times the bump light the opaque list
 * left there) are world surfaces, opaque in effect, and a per-triangle sort
 * would put a big floor's after the sprites standing on it -- the sprites
 * would blend over the bare light. On the PVR each pixel sorts them first,
 * being farthest; here they are drawn before the rest of the list, in the
 * order the game sent them.
 *
 * Bump mapping (Doom 64's normal maps, src/r_lights.c). The PVR's bump
 * texture holds, per texel, the elevation S (high byte, 0..255 = 0..90
 * degrees) and rotation R (low byte, a turn) of the surface normal; the
 * offset colour carries K1 (alpha), K2 (red), K3 (green) and the light's
 * rotation Q (blue), and the texel's intensity is
 *     I = K1 + K2 sin S + K3 cos S cos(R - Q),
 * which is K1 + N.L for N = (cos S cos R, cos S sin R, sin S) and
 * L = (K3 cos Q, K3 sin Q, K2). So a bump texture becomes a normal map
 * (X8R8G8B8, N * 0.5 + 0.5), L rides in the diffuse colour and -L in the
 * specular, K1 in the texture factor, and four texture stages evaluate it
 * with the PVR's signed result: the dot product's negative part (N.-L, into
 * the temporary register), its positive part, + K1, - the negative part.
 * The game's surface colour then multiplies it (DESTCOLOR, ZERO), as on the
 * Dreamcast.
 *
 * Fog. PVR_FOG_TABLE: the 128-entry table (pvr_fog_table_*, two densities a
 * entry) indexed by 1/w scaled by the fog density register, in the PVR's
 * pseudo-logarithmic steps; it is looked up for every vertex of a fogged
 * batch when the scene is drawn -- the table, its colour and its scale as
 * they stand then, which is when the PVR reads them -- and drawn as
 * Direct3D vertex fog (the factor in the specular alpha). PVR_FOG_VERTEX:
 * the offset colour's alpha is the density, the PVR_FOG_VERTEX_COLOR
 * register the colour.
 *
 * Sprites (the HUD, the font, the menus' backdrops): a sprite header and
 * 64-byte vertices of four corners, D's depth and texture coordinates the
 * PVR's own, completed from the parallelogram A B C. Drawn as two
 * triangles in the header's colours.
 *
 * Secondary accumulation buffer (mode2 bits 24 and 25): a polygon whose
 * DST_SELECT is set blends into a second screen-sized render target instead
 * of the frame; one whose SRC_SELECT is set takes that buffer's pixel as its
 * source colour. Doom builds its bump-mapped weapon there and lays it over
 * the frame (src/r_phase3.c R_RenderPSprites). Both are real render
 * targets here.
 *
 * Coordinates. The PVR's pixel centres are at +0.5; Direct3D 8's are at the
 * integer, so every vertex moves half a pixel up and left. */

#include "xbox_xtl.h"
#include <xgraphics.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <kos/dbglog.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <dc/sq.h>

#include "pvr_texdecode.h"

void xbox_maple_rumble_pump(void);
void xbox_maple_poll(void);

/* ======================================================== configuration */

/* The game's screen: the PVR's 640x480, which every vertex the game
 * submits is in. The display can be larger (pvr_init picks the mode):
 * screen-space vertices are then scaled into the 4:3 picture's place on
 * it, sDispArea, and the rest -- a 16:9 screen's side bars -- stays black. */
#define SCR_W 640
#define SCR_H 480

/* z -> Direct3D depth */
#define DEPTH_SCALE (1.0f / 16384.0f)

#define VRAM_SIZE   (8u * 1024u * 1024u)
#define VRAM_PAGE   4096u
#define VRAM_PAGES  (VRAM_SIZE / VRAM_PAGE)

#define MAX_VERTS   (144 * 1024)    /* triangle-list vertices per scene */
#define MAX_STATES  16384
#define MAX_BATCHES 32768
#define SORT_CHUNK  (8 * 1024)      /* translucent triangles copied out at once */
#define TEX_SLOTS   4096            /* texture cache (open addressing) */
#define FREE_DELAY  3               /* frames before a released GPU object goes */

typedef struct XbVert
{
    float x, y, z, rhw;
    D3DCOLOR diffuse;
    D3DCOLOR specular;
    float u, v;
} XbVert;
#define XB_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)

/* Everything a batch needs, captured when its header is decoded. Kept POD
 * and memset before filling, so two states compare with memcmp. */
typedef struct XbState
{
    uint8_t list;
    uint8_t blend_src, blend_dst;
    uint8_t blend;                  /* blending at all (not in OP or PT) */
    uint8_t src_sel, dst_sel;
    uint8_t depth_cmp, depth_write;
    uint8_t cull;
    uint8_t use_alpha;
    uint8_t textured;
    uint8_t env;
    uint8_t ignore_tex_alpha;
    uint8_t filter;
    uint8_t addr_u, addr_v;
    uint8_t offset;
    uint8_t gouraud;
    uint8_t linear;                 /* texture takes texel coordinates */
    uint8_t fog;                    /* PVR_FOG_TABLE, PVR_FOG_VERTEX or PVR_FOG_DISABLE */
    uint8_t bump;                   /* a bump-map polygon (see the top) */
    uint8_t bump_k1;                /* its K1: the vertices', one state per value */
    uint8_t sprite;
    uint8_t mulpass;                /* a TR colour pass over a bump result (see the top) */
    uint32_t fog_color;             /* the vertex fog colour register, as it stood */
    float tex_w, tex_h;             /* the header's size, for linear UVs */
    IDirect3DTexture8 *tex;
    D3DPalette *pal;
} XbState;

typedef struct XbBatch
{
    uint32_t state;
    uint32_t first;                 /* first vertex */
    uint32_t count;                 /* vertices, a multiple of 3 */
} XbBatch;

typedef struct XbTri               /* a translucent triangle awaiting the sort */
{
    uint32_t key;                   /* its farthest vertex's z, as ordered bits */
    uint32_t state;
    uint32_t first;                 /* its three vertices */
    uint32_t solid;                 /* opaque texels drawn before the sort (see the top) */
} XbTri;

/* What the TA keeps while it reads one stream of parameters: the header in
 * force, the strip so far, a sprite vertex's first half. The direct stream
 * (the open list) has one, each DMA buffer gets one when it is read. */
typedef struct TaCtx
{
    int list;
    uint32_t state;                 /* 0xFFFFFFFF: no header yet */
    int uv16;
    int sprite;
    uint32_t sprite_argb, sprite_oargb;
    int half;                       /* a sprite vertex's first 32 bytes are held */
    uint32_t held[8];
    pvr_vertex_t strip[2];          /* the two vertices before the next */
    int strip_len;
} TaCtx;

/* ============================================================ globals */

static IDirect3D8 *sD3D;
static IDirect3DDevice8 *sDev;
static IDirect3DSurface8 *sBackBuf, *sDepth;
static int sInited;
static uint32_t sFrame;

/* the display (pvr_init): its size, the 4:3 picture's rectangle on it,
 * and the game-pixel -> display-pixel scale and offset that puts it
 * there. 640x480 modes are 1, 0, 0 and the picture is the whole screen. */
static uint32_t sDispW = SCR_W, sDispH = SCR_H;
static D3DRECT sDispArea = { 0, 0, SCR_W, SCR_H };
static float sDispScaleX = 1.0f, sDispScaleY = 1.0f, sDispOffX = 0.0f, sDispOffY = 0.0f;
static int sDispBars;               /* the picture is not the whole screen */
static int sDisp16x9;               /* the screen is 16:9 (720p, or anamorphic 480) */
static IDirect3DSurface8 *sTargetNow;  /* the render target set last (set_target) */
static int sAutosort = 1;              /* pvr_init: the TR list autosorted */

/* this scene's: the display's, or 1, 0, 0 for a scene drawn to a texture,
 * which is in game pixels */
static float sXfScaleX = 1.0f, sXfScaleY = 1.0f, sXfOffX = 0.0f, sXfOffY = 0.0f;

/* texture memory */
static uint8_t *sVram;              /* 8 MB, 16 MB-aligned */
static uint32_t sPageGen[VRAM_PAGES];
static uint32_t sGenCounter = 1;
static CRITICAL_SECTION sVramLock;

/* the PVR's registers and palette RAM that the backend models */
static uint32_t sPal[1024];
static uint32_t sPalBankGen[64];    /* per 16-entry bank */
static int sPalFmt = PVR_PAL_ARGB1555;
static uint32_t sPtAlphaRef = 0xFF; /* KOS's reset value; the game writes 10 */
static uint32_t sTxrStride;         /* PVR_TEXTURE_MODULO, in pixels */
static D3DCOLOR sBgColor = 0xFF000000;
static float sZClip = 0.0001f;      /* KOS's default */
static uint32_t sFogVertexColor;    /* PVR_FOG_VERTEX_COLOR */
static uint32_t sFogTableColor;     /* PVR_FOG_TABLE_COLOR */
static uint32_t sFogDensity;        /* PVR_FOG_DENSITY: the table's 1/w scale */
static float sFogScale;             /* ... as a float */
static uint32_t sFogTable[128];     /* PVR_FOG_TABLE_BASE: hi byte this entry, lo the next */
static uint32_t sObjectClip, sScalerCfg;   /* kept, not drawn */
static int sVideoEnabled = 1;       /* vid_set_enabled */

/* the scene being collected */
static XbVert *sVerts;
static uint32_t sVertCount;
static XbState *sStates;
static uint32_t sStateCount;
static XbBatch *sBatches[3];        /* OP, PT and the TR colour-over-bump batches */
static uint32_t sBatchCount[3];
static XbTri *sTris, *sTrisTmp;
static uint32_t sTriCount;
static TaCtx sDirect;               /* the open list's stream */
static int sCurList = -1;
static int sSceneOpen;
static uint32_t sDropped;
static int sUsesSecondary;

/* the DMA vertex buffers (pvr_set_vertbuf), per list */
#define LISTS 5
static uint8_t *sVbBase[LISTS];
static size_t sVbSize[LISTS], sVbPtr[LISTS];

/* render to texture */
static void *sRttAddr;              /* the texture-RAM address the picture is "at" */
static IDirect3DTexture8 *sRttTex;
static IDirect3DSurface8 *sRttSurf;
static uint32_t sRttW, sRttH;
static int sSceneToTexture;

/* the secondary accumulation buffer */
static IDirect3DTexture8 *sSecTex;
static IDirect3DSurface8 *sSecSurf;

/* ====================================================== deferred release
 * The GPU may still be reading a texture or palette for up to two frames
 * after its last draw is queued; released objects wait FREE_DELAY frames. */

typedef struct Deferred
{
    D3DResource *obj;
    D3DPalette *pal;
    uint32_t frame;
} Deferred;
static Deferred *sDeferred;
static uint32_t sDeferredCount, sDeferredMax;

static void defer_release_tex(IDirect3DTexture8 *t)
{
    if (t == NULL)
        return;
    if (sDeferredCount == sDeferredMax)
    {
        uint32_t n = sDeferredMax ? sDeferredMax * 2 : 1024;
        Deferred *d = (Deferred *)realloc(sDeferred, n * sizeof(*d));
        if (d == NULL)
            return;             /* leaked rather than freed under the GPU */
        sDeferred = d;
        sDeferredMax = n;
    }
    sDeferred[sDeferredCount].obj = (D3DResource *)t;
    sDeferred[sDeferredCount].pal = NULL;
    sDeferred[sDeferredCount].frame = sFrame;
    sDeferredCount++;
}

/* Palettes go back to a pool rather than being released: a bank can be
 * rewritten many times a frame and creating one costs a contiguous alloc. */
static D3DPalette *sPalPool32[256], *sPalPool256[64];
static int sPalPool32Count, sPalPool256Count;

typedef struct PalDeferred
{
    D3DPalette *pal;
    int big;
    uint32_t frame;
} PalDeferred;
static PalDeferred sPalDeferred[2048];
static uint32_t sPalDeferredCount;

static void defer_release_pal(D3DPalette *p, int big)
{
    if (p == NULL)
        return;
    if (sPalDeferredCount < sizeof(sPalDeferred) / sizeof(sPalDeferred[0]))
    {
        sPalDeferred[sPalDeferredCount].pal = p;
        sPalDeferred[sPalDeferredCount].big = big;
        sPalDeferred[sPalDeferredCount].frame = sFrame;
        sPalDeferredCount++;
    }
    else
    {
        IDirect3DPalette8_Release(p);       /* table full: the oldest are long done */
    }
}

static void run_deferred(void)
{
    uint32_t i, kept = 0;

    for (i = 0; i < sDeferredCount; i++)
    {
        if (sFrame - sDeferred[i].frame >= FREE_DELAY)
            D3DResource_Release(sDeferred[i].obj);
        else
            sDeferred[kept++] = sDeferred[i];
    }
    sDeferredCount = kept;

    kept = 0;
    for (i = 0; i < sPalDeferredCount; i++)
    {
        PalDeferred *d = &sPalDeferred[i];

        if (sFrame - d->frame >= FREE_DELAY)
        {
            if (d->big && sPalPool256Count < 64)
                sPalPool256[sPalPool256Count++] = d->pal;
            else if (!d->big && sPalPool32Count < 256)
                sPalPool32[sPalPool32Count++] = d->pal;
            else
                IDirect3DPalette8_Release(d->pal);
        }
        else
        {
            sPalDeferred[kept++] = *d;
        }
    }
    sPalDeferredCount = kept;
}

/* ===================================================== texture memory */

/* A first-fit allocator over the 8 MB, 32-byte aligned as KOS's is. Its
 * bookkeeping lives outside the memory it manages: the game may write any
 * byte of what it is given. */
typedef struct VBlock
{
    uint32_t off, size;
    int used;
} VBlock;
static VBlock *sBlocks;
static uint32_t sBlockCount, sBlockMax;

static void vram_init_alloc(void)
{
    sBlockMax = 1024;
    sBlocks = (VBlock *)malloc(sBlockMax * sizeof(VBlock));
    sBlocks[0].off = 0;
    sBlocks[0].size = VRAM_SIZE;
    sBlocks[0].used = 0;
    sBlockCount = 1;
}

static int vram_grow(void)
{
    uint32_t n = sBlockMax * 2;
    VBlock *b = (VBlock *)realloc(sBlocks, n * sizeof(VBlock));

    if (b == NULL)
        return -1;
    sBlocks = b;
    sBlockMax = n;
    return 0;
}

static void vram_dirty(uint32_t off, uint32_t n)
{
    uint32_t p, last;

    if (n == 0 || off >= VRAM_SIZE)
        return;
    if (off + n > VRAM_SIZE)
        n = VRAM_SIZE - off;
    last = (off + n - 1) / VRAM_PAGE;
    sGenCounter++;
    for (p = off / VRAM_PAGE; p <= last; p++)
        sPageGen[p] = sGenCounter;
}

static uint32_t vram_gen(uint32_t off, uint32_t n)
{
    uint32_t p, last, g = 0;

    if (n == 0 || off >= VRAM_SIZE)
        return 0;
    if (off + n > VRAM_SIZE)
        n = VRAM_SIZE - off;
    last = (off + n - 1) / VRAM_PAGE;
    for (p = off / VRAM_PAGE; p <= last; p++)
        if (sPageGen[p] > g)
            g = sPageGen[p];
    return g;
}

static int vram_owns(const void *p)
{
    return sVram != NULL && (const uint8_t *)p >= sVram && (const uint8_t *)p < sVram + VRAM_SIZE;
}

pvr_ptr_t pvr_mem_malloc(size_t size)
{
    uint32_t i, want;
    pvr_ptr_t rv = NULL;

    if (!sInited || size == 0)
        return NULL;
    want = (uint32_t)((size + 31) & ~(size_t)31);
    EnterCriticalSection(&sVramLock);
    for (i = 0; i < sBlockCount; i++)
    {
        VBlock *b = &sBlocks[i];

        if (b->used || b->size < want)
            continue;
        if (b->size > want)
        {
            if (sBlockCount == sBlockMax && vram_grow() < 0)
                break;
            b = &sBlocks[i];
            memmove(&sBlocks[i + 2], &sBlocks[i + 1], (sBlockCount - i - 1) * sizeof(VBlock));
            sBlocks[i + 1].off = b->off + want;
            sBlocks[i + 1].size = b->size - want;
            sBlocks[i + 1].used = 0;
            sBlockCount++;
            b->size = want;
        }
        b->used = 1;
        /* the game writes new texture memory directly as well as through
         * pvr_txr_load (its bump maps): whatever was cached from these
         * pages before is stale from here */
        vram_dirty(b->off, b->size);
        rv = sVram + b->off;
        break;
    }
    LeaveCriticalSection(&sVramLock);
    return rv;
}

static void rtt_forget(const void *p);

void pvr_mem_free(pvr_ptr_t chunk)
{
    uint32_t off, i;

    if (!vram_owns(chunk))
        return;
    off = (uint32_t)((uint8_t *)chunk - sVram);
    EnterCriticalSection(&sVramLock);
    for (i = 0; i < sBlockCount; i++)
    {
        if (sBlocks[i].off == off && sBlocks[i].used)
        {
            sBlocks[i].used = 0;
            /* the memory no longer holds what it held */
            vram_dirty(off, sBlocks[i].size);
            if (i + 1 < sBlockCount && !sBlocks[i + 1].used)
            {
                sBlocks[i].size += sBlocks[i + 1].size;
                memmove(&sBlocks[i + 1], &sBlocks[i + 2], (sBlockCount - i - 2) * sizeof(VBlock));
                sBlockCount--;
            }
            if (i > 0 && !sBlocks[i - 1].used)
            {
                sBlocks[i - 1].size += sBlocks[i].size;
                memmove(&sBlocks[i], &sBlocks[i + 1], (sBlockCount - i - 1) * sizeof(VBlock));
                sBlockCount--;
            }
            break;
        }
    }
    LeaveCriticalSection(&sVramLock);
    rtt_forget(chunk);
}

size_t pvr_mem_available(void)
{
    size_t total = 0;
    uint32_t i;

    if (!sInited)
        return 0;
    EnterCriticalSection(&sVramLock);
    for (i = 0; i < sBlockCount; i++)
        if (!sBlocks[i].used)
            total += sBlocks[i].size;
    LeaveCriticalSection(&sVramLock);
    return total;
}

void pvr_mem_reset(void)
{
    EnterCriticalSection(&sVramLock);
    sBlocks[0].off = 0;
    sBlocks[0].size = VRAM_SIZE;
    sBlocks[0].used = 0;
    sBlockCount = 1;
    vram_dirty(0, VRAM_SIZE);
    LeaveCriticalSection(&sVramLock);
}

/* sq_set32 on texture memory (Platform/xbox/kos_sys_xbox.c tells us) */
void pvr_xbox_vram_written(const void *p, size_t n)
{
    if (vram_owns(p))
    {
        EnterCriticalSection(&sVramLock);
        vram_dirty((uint32_t)((const uint8_t *)p - sVram), (uint32_t)n);
        LeaveCriticalSection(&sVramLock);
    }
}

void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count)
{
    if (!vram_owns(dst))
        return;
    if ((uint8_t *)dst + count > sVram + VRAM_SIZE)
        count = (size_t)(sVram + VRAM_SIZE - (uint8_t *)dst);
    memcpy(dst, src, count);
    EnterCriticalSection(&sVramLock);
    vram_dirty((uint32_t)((uint8_t *)dst - sVram), (uint32_t)count);
    LeaveCriticalSection(&sVramLock);
}

/* KOS's twiddle (kernel/arch/dreamcast/hardware/pvr/pvr_texture.c) */
#define TWIDTAB(x) ( ((x)&1)|(((x)&2)<<1)|(((x)&4)<<2)|(((x)&8)<<3)|(((x)&16)<<4)| \
                     (((x)&32)<<5)|(((x)&64)<<6)|(((x)&128)<<7)|(((x)&256)<<8)|(((x)&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )

/* pvr_txr_load_ex as KOS does it: it always twiddles, whatever the flags
 * say. */
void pvr_txr_load_ex(const void *src, pvr_ptr_t dst,
                                uint32_t w, uint32_t h, uint32_t flags)
{
    uint32_t x, y, yout, min, mask, bytes = 0;
    int invert = (flags & PVR_TXRLOAD_INVERT_Y) ? 1 : 0;

    if (!vram_owns(dst) || w == 0 || h == 0)
        return;
    min = w < h ? w : h;
    mask = min - 1;
    switch (flags & PVR_TXRLOAD_FMT_MASK)
    {
        case PVR_TXRLOAD_4BPP:
        {
            const uint8_t *pixels = (const uint8_t *)src;
            uint16_t *vtex = (uint16_t *)dst;

            for (y = 0; y < h; y += 2)
            {
                yout = invert ? (h - 1) - y : y;
                for (x = 0; x < w; x += 2)
                    vtex[TWIDOUT((x & mask) / 2, (yout & mask) / 2) +
                         (x / min + yout / min) * min * min / 4] =
                        (pixels[(x + y * w) >> 1] & 15) | ((pixels[(x + (y + 1) * w) >> 1] & 15) << 4) |
                        ((pixels[(x + y * w) >> 1] >> 4) << 8) | ((pixels[(x + (y + 1) * w) >> 1] >> 4) << 12);
            }
            bytes = w * h / 2;
            break;
        }
        case PVR_TXRLOAD_8BPP:
        {
            const uint8_t *pixels = (const uint8_t *)src;
            uint16_t *vtex = (uint16_t *)dst;

            for (y = 0; y < h; y += 2)
            {
                yout = invert ? (h - 1) - y : y;
                for (x = 0; x < w; x++)
                    vtex[TWIDOUT((yout & mask) / 2, x & mask) +
                         (x / min + yout / min) * min * min / 2] =
                        pixels[y * w + x] | (pixels[(y + 1) * w + x] << 8);
            }
            bytes = w * h;
            break;
        }
        case PVR_TXRLOAD_16BPP:
        default:
        {
            const uint16_t *pixels = (const uint16_t *)src;
            uint16_t *vtex = (uint16_t *)dst;

            for (y = 0; y < h; y++)
            {
                yout = invert ? (h - 1) - y : y;
                for (x = 0; x < w; x++)
                    vtex[TWIDOUT(x & mask, yout & mask) + (x / min + yout / min) * min * min] =
                        pixels[y * w + x];
            }
            bytes = w * h * 2;
            break;
        }
    }
    EnterCriticalSection(&sVramLock);
    vram_dirty((uint32_t)((uint8_t *)dst - sVram), bytes);
    LeaveCriticalSection(&sVramLock);
}

void pvr_txr_set_stride(size_t texture_width)
{
    sTxrStride = (uint32_t)texture_width;
}

size_t pvr_txr_get_stride(void)
{
    return sTxrStride;
}

/* =============================================================== palettes */

void pvr_set_pal_format(pvr_palfmt_t fmt)
{
    int b;

    if (sPalFmt != (int)fmt)
        for (b = 0; b < 64; b++)
            sPalBankGen[b]++;
    sPalFmt = (int)fmt;
}

void pvr_set_pal_entry(uint32_t idx, uint32_t value)
{
    idx &= 1023;
    if (sPal[idx] != value)
    {
        sPal[idx] = value;
        sPalBankGen[idx >> 4]++;
    }
}

static D3DCOLOR pal_color(uint32_t v)
{
    uint32_t a, r, g, b;

    switch (sPalFmt)
    {
        case PVR_PAL_RGB565:
            r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
            return D3DCOLOR_ARGB(255, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
        case PVR_PAL_ARGB4444:
            return D3DCOLOR_ARGB(((v >> 12) & 15) * 17, ((v >> 8) & 15) * 17,
                                 ((v >> 4) & 15) * 17, (v & 15) * 17);
        case PVR_PAL_ARGB8888:
            return (D3DCOLOR)v;
        case PVR_PAL_ARGB1555:
        default:
            a = (v >> 15) & 1; r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
            return D3DCOLOR_ARGB(a ? 255 : 0, (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
    }
}

/* The palette for a bank as it stands now. One Direct3D palette per
 * (bank, generation): a batch keeps the one it was given, however the bank
 * changes after it. `first` = first entry, `n` = 16 or 256. */
typedef struct PalCache
{
    D3DPalette *pal;
    uint32_t gen;               /* the bank generations folded together */
    int fmt;
} PalCache;
static PalCache sPalCache16[64];    /* PAL4: bank of 16 */
static PalCache sPalCache256[4];    /* PAL8: bank of 256 */

static D3DPalette *palette_for(uint32_t first, uint32_t n)
{
    PalCache *pc = (n == 16) ? &sPalCache16[first >> 4] : &sPalCache256[first >> 8];
    uint32_t gen = 0, i;
    D3DCOLOR *c;
    D3DPalette *p;

    for (i = 0; i < n / 16; i++)
        gen = gen * 31u + sPalBankGen[(first >> 4) + i];
    if (pc->pal != NULL && pc->gen == gen && pc->fmt == sPalFmt)
        return pc->pal;
    /* the old one may be in this frame's batches: it is let go later */
    defer_release_pal(pc->pal, n == 256);
    pc->pal = NULL;
    if (n == 256 && sPalPool256Count > 0)
        p = sPalPool256[--sPalPool256Count];
    else if (n == 16 && sPalPool32Count > 0)
        p = sPalPool32[--sPalPool32Count];
    else if (FAILED(IDirect3DDevice8_CreatePalette(sDev, n == 256 ? D3DPALETTE_256 : D3DPALETTE_32, &p)))
        return NULL;
    if (FAILED(IDirect3DPalette8_Lock(p, &c, 0)))
    {
        IDirect3DPalette8_Release(p);
        return NULL;
    }
    for (i = 0; i < n; i++)
        c[i] = pal_color(sPal[(first + i) & 1023]);
    if (n == 16)
        for (i = 16; i < 32; i++)
            c[i] = 0;
    IDirect3DPalette8_Unlock(p);
    pc->pal = p;
    pc->gen = gen;
    pc->fmt = sPalFmt;
    return p;
}

/* ================================================================ textures */

typedef struct TexEntry
{
    uint32_t key_off;               /* texture-RAM offset */
    uint32_t key_fmt;               /* mode3's format bits | size bits << 16 */
    IDirect3DTexture8 *tex;
    uint32_t gen;                   /* page generation it was made from */
    uint32_t bytes;
    uint32_t d3d_bytes;             /* what the Direct3D copy takes */
    uint32_t last_used;
    uint8_t used;
    uint8_t paletted;               /* 4 or 8, else 0 */
    uint8_t bump;                   /* a bump texture, made a normal map */
} TexEntry;
static TexEntry sTex[TEX_SLOTS];
static uint32_t sBadTexFmt;

/* What the Direct3D copies of texture memory may take. Texture memory is
 * 8 MB and a copy is at most twice its source (a bump texture's normal map,
 * P8 from 4-bit), so a scene's working set is well inside this. What it
 * guards against is textures named once and never again -- every level
 * frees and reloads texture memory, the sprite cache turns over -- piling
 * up until the idle sweep reaches them. */
#define TEX_BUDGET (12u * 1024u * 1024u)
static uint32_t sTexBytes;

static uint8_t *sScratch;           /* linear texels, the largest a texture can be */
static size_t sScratchSize;

static void *scratch(size_t n)
{
    if (n > sScratchSize)
    {
        void *p = realloc(sScratch, n);

        if (p == NULL)
            return NULL;
        sScratch = (uint8_t *)p;
        sScratchSize = n;
    }
    return sScratch;
}

/* The normal map texel for a bump texel: S (high byte) is the elevation,
 * 0..255 = 0..90 degrees, R (low byte) the rotation, 0..255 = a turn;
 * N = (cos S cos R, cos S sin R, sin S), stored N * 0.5 + 0.5 as Direct3D's
 * DOT3 reads it. One table for all 65536 texels, made the first time. */
static uint32_t *sBumpLut;

static uint32_t bump_texel(uint32_t t)
{
    if (sBumpLut == NULL)
    {
        float sinS[256], cosS[256], sinR[256], cosR[256];
        uint32_t i;

        sBumpLut = (uint32_t *)malloc(65536 * sizeof(uint32_t));
        if (sBumpLut == NULL)
            return 0xFF8080FFu;
        for (i = 0; i < 256; i++)
        {
            float s = (float)i / 255.0f * 1.57079633f, r = (float)i / 255.0f * 6.28318531f;

            sinS[i] = sinf(s);
            cosS[i] = cosf(s);
            sinR[i] = sinf(r);
            cosR[i] = cosf(r);
        }
        for (i = 0; i < 65536; i++)
        {
            uint32_t si = i >> 8, ri = i & 255;
            float nx = cosS[si] * cosR[ri], ny = cosS[si] * sinR[ri], nz = sinS[si];
            int x = (int)(nx * 127.5f + 128.0f), y = (int)(ny * 127.5f + 128.0f),
                z = (int)(nz * 127.5f + 128.0f);

            x = x < 0 ? 0 : x > 255 ? 255 : x;
            y = y < 0 ? 0 : y > 255 ? 255 : y;
            z = z < 0 ? 0 : z > 255 ? 255 : z;
            sBumpLut[i] = 0xFF000000u | ((uint32_t)x << 16) | ((uint32_t)y << 8) | (uint32_t)z;
        }
    }
    return sBumpLut[t & 0xFFFF];
}

/* Make the Direct3D texture for a header's texture: texture memory to
 * linear texels (pvr_texdecode.h), then into the NV2A's swizzled layout.
 * NULL if the format is one the port does not use (YUV, VQ paletted or
 * mipmapped) or memory ran out. */
static IDirect3DTexture8 *tex_build(uint32_t off, uint32_t w, uint32_t h, uint32_t m3, uint8_t *paletted)
{
    int pixfmt = (int)((m3 >> 27) & 7);
    IDirect3DTexture8 *t = NULL;
    D3DFORMAT fmt;
    D3DLOCKED_RECT lr;
    uint8_t *lin;
    int bpp;

    *paletted = 0;
    switch (pixfmt)
    {
        case PVR_PIXEL_MODE_ARGB1555: fmt = D3DFMT_A1R5G5B5; break;
        case PVR_PIXEL_MODE_RGB565:   fmt = D3DFMT_R5G6B5;   break;
        case PVR_PIXEL_MODE_ARGB4444: fmt = D3DFMT_A4R4G4B4; break;
        case PVR_PIXEL_MODE_PAL_4BPP: fmt = D3DFMT_P8; *paletted = 4; break;
        case PVR_PIXEL_MODE_PAL_8BPP: fmt = D3DFMT_P8; *paletted = 8; break;
        case PVR_PIXEL_MODE_BUMP:     fmt = D3DFMT_X8R8G8B8; break;
        default: fmt = D3DFMT_UNKNOWN; break;
    }
    /* a bump texture's 16-bit texels are decoded into the first half of
     * the scratch and widened into the normal map in place, from the end */
    lin = (uint8_t *)scratch((size_t)w * h * (pixfmt == PVR_PIXEL_MODE_BUMP ? 4 : 2));
    bpp = (lin != NULL && fmt != D3DFMT_UNKNOWN)
          ? pvr_td_decode(sVram + off, w, h, m3, sTxrStride, VRAM_SIZE - off, lin) : 0;
    if (bpp == 0)
    {
        if (sBadTexFmt++ < 4)
            dbglog(DBG_ERROR, "pvr: texture at %06lx (mode3 %08lx, %lux%lu) is not drawn\n",
                   (unsigned long)off, (unsigned long)m3, (unsigned long)w, (unsigned long)h);
        return NULL;
    }
    if (pixfmt == PVR_PIXEL_MODE_BUMP)
    {
        const uint16_t *t16 = (const uint16_t *)lin;
        uint32_t *t32 = (uint32_t *)lin;
        uint32_t i = w * h;

        while (i-- > 0)
            t32[i] = bump_texel(t16[i]);
        bpp = 4;
    }
    if (FAILED(IDirect3DDevice8_CreateTexture(sDev, w, h, 1, 0, fmt, D3DPOOL_DEFAULT, &t)))
        return NULL;
    if (FAILED(IDirect3DTexture8_LockRect(t, 0, &lr, NULL, 0)))
    {
        IDirect3DTexture8_Release(t);
        return NULL;
    }
    XGSwizzleRect(lin, w * bpp, NULL, lr.pBits, w, h, NULL, bpp);
    IDirect3DTexture8_UnlockRect(t, 0);
    return t;
}

static uint32_t tex_hash(uint32_t off, uint32_t f)
{
    uint32_t h = off * 2654435761u ^ f * 2246822519u;

    return (h ^ (h >> 15)) & (TEX_SLOTS - 1);
}

/* The texture a header names, current. */
static IDirect3DTexture8 *tex_lookup(uint32_t m2, uint32_t m3, uint8_t *paletted, uint8_t *bump)
{
    /* the address field spans 16 MB; the 8 MB of texture memory is all
     * there is to read, and the decoder's bounds are taken within it */
    uint32_t off = ((m3 & 0x1FFFFF) << 3) & (VRAM_SIZE - 1);
    uint32_t w = 8u << ((m2 >> 3) & 7), h = 8u << (m2 & 7);
    uint32_t fbits = ((m3 >> 25) & 0x7F) | ((m2 & 0x3F) << 16);
    int pixfmt = (int)((m3 >> 27) & 7);
    uint32_t bytes = pvr_td_bytes(w, h, m3);
    uint32_t slot = tex_hash(off, fbits), i, gen;
    TexEntry *e = NULL;

    /* a strided texture's rows depend on the stride register */
    if (((m3 >> 26) & 1) && ((m3 >> 25) & 1))
        fbits ^= sTxrStride << 23;
    EnterCriticalSection(&sVramLock);
    gen = vram_gen(off, bytes);
    LeaveCriticalSection(&sVramLock);

    for (i = 0; i < TEX_SLOTS; i++)
    {
        TexEntry *c = &sTex[(slot + i) & (TEX_SLOTS - 1)];

        if (!c->used)
        {
            if (e == NULL)
                e = c;
            break;
        }
        if (c->key_off == off && c->key_fmt == fbits)
        {
            e = c;
            break;
        }
    }
    if (e == NULL)
        return NULL;            /* table full; the next frame's sweep frees room */
    if (e->used && e->tex != NULL && e->gen == gen)
    {
        e->last_used = sFrame;
        *paletted = e->paletted;
        *bump = e->bump;
        return e->tex;
    }
    if (e->used && e->tex != NULL)
    {
        defer_release_tex(e->tex);
        sTexBytes -= e->d3d_bytes;
    }
    e->used = 1;
    e->key_off = off;
    e->key_fmt = fbits;
    e->gen = gen;
    e->bytes = bytes;
    e->last_used = sFrame;
    e->bump = pixfmt == PVR_PIXEL_MODE_BUMP;
    e->tex = tex_build(off, w, h, m3, &e->paletted);
    e->d3d_bytes = e->tex != NULL ? w * h * (e->paletted ? 1u : e->bump ? 4u : 2u) : 0;
    sTexBytes += e->d3d_bytes;
    *paletted = e->paletted;
    *bump = e->bump;
    return e->tex;
}

static void tex_evict(TexEntry *e)
{
    defer_release_tex(e->tex);
    sTexBytes -= e->d3d_bytes;
    e->tex = NULL;
    e->d3d_bytes = 0;
    /* the slot stays claimed (a tombstone keeps the probe chains that run
     * through it intact); a later lookup of this key rebuilds into it */
    e->gen = 0xFFFFFFFF;
}

/* Rebuild the table from its live entries, dropping the tombstones. Keys
 * come and go for good -- every level frees and reallocates texture memory,
 * and the sprite cache turns over -- so without this the tombstones would
 * fill the table. */
static void tex_compact(void)
{
    static TexEntry live[TEX_SLOTS];
    uint32_t i, n = 0;

    for (i = 0; i < TEX_SLOTS; i++)
        if (sTex[i].used && sTex[i].tex != NULL)
            live[n++] = sTex[i];
    memset(sTex, 0, sizeof(sTex));
    for (i = 0; i < n; i++)
    {
        uint32_t slot = tex_hash(live[i].key_off, live[i].key_fmt), j;

        for (j = 0; j < TEX_SLOTS; j++)
        {
            TexEntry *c = &sTex[(slot + j) & (TEX_SLOTS - 1)];

            if (!c->used)
            {
                *c = live[i];
                break;
            }
        }
    }
}

/* Once a frame: textures no header has named for ten seconds go, so a
 * scene's textures do not outlive it on the GPU; past the budget, the ones
 * longest unused go first, down to the ones this frame needs; and when the
 * table is three-quarters claimed, the tombstones are swept out. */
static void tex_sweep(void)
{
    static const uint32_t ages[] = { 600, 300, 120, 60, 30, 10, 2 };
    uint32_t i, a, claimed = 0;

    for (a = 0; a < sizeof(ages) / sizeof(ages[0]); a++)
    {
        if (a > 0 && sTexBytes <= TEX_BUDGET)
            break;
        if (a == 0 && (sFrame & 127) != 0 && sTexBytes <= TEX_BUDGET)
            continue;
        for (i = 0; i < TEX_SLOTS; i++)
        {
            TexEntry *e = &sTex[i];

            if (e->used && e->tex != NULL && sFrame - e->last_used > ages[a])
                tex_evict(e);
        }
    }
    for (i = 0; i < TEX_SLOTS; i++)
        claimed += sTex[i].used;
    if (claimed > TEX_SLOTS * 3 / 4)
        tex_compact();
}

/* ======================================================= render to texture */

static void rtt_forget(const void *p)
{
    if (p != NULL && p == sRttAddr)
        sRttAddr = NULL;
}

static int rtt_target(uint32_t w, uint32_t h)
{
    if (sRttTex != NULL && (sRttW != w || sRttH != h))
    {
        if (sRttSurf)
            IDirect3DSurface8_Release(sRttSurf);
        defer_release_tex(sRttTex);
        sRttTex = NULL;
        sRttSurf = NULL;
    }
    if (sRttTex == NULL)
    {
        if (FAILED(IDirect3DDevice8_CreateTexture(sDev, w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_LIN_X8R8G8B8,
                                       D3DPOOL_DEFAULT, &sRttTex)))
            return -1;
        IDirect3DTexture8_GetSurfaceLevel(sRttTex, 0, &sRttSurf);
        sRttW = w;
        sRttH = h;
    }
    return 0;
}

int pvr_scene_begin_rtt(pvr_ptr_t txr, uint32_t render_w,
                                   uint32_t render_h, uint32_t stride_px)
{
    if (!txr || render_w == 0 || render_h == 0 || stride_px < render_w || (stride_px & 3))
        return -1;
    if (!sInited || rtt_target(render_w, render_h) < 0)
        return -1;
    pvr_scene_begin();
    sSceneToTexture = 1;
    sRttAddr = txr;
    /* the texture is in game pixels, whatever the display's mode */
    sXfScaleX = sXfScaleY = 1.0f;
    sXfOffX = sXfOffY = 0.0f;
    return 0;
}

/* ===================================================== the secondary buffer */

static int secondary_target(void)
{
    if (sSecTex == NULL)
    {
        /* the display's size: it is drawn and sampled in display pixels */
        if (FAILED(IDirect3DDevice8_CreateTexture(sDev, sDispW, sDispH, 1, D3DUSAGE_RENDERTARGET,
                                       D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &sSecTex)))
            return -1;
        IDirect3DTexture8_GetSurfaceLevel(sSecTex, 0, &sSecSurf);
    }
    return 0;
}

/* ========================================================= header decode */

static const D3DCMPFUNC kCmp[8] = {
    D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL,
    D3DCMP_GREATER, D3DCMP_NOTEQUAL, D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS,
};
/* PVR blend codes 2 and 3 name "the other colour": the destination's for the
 * source factor, the source's for the destination factor. */
static const D3DBLEND kSrcBlend[8] = {
    D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLEND_DESTCOLOR, D3DBLEND_INVDESTCOLOR,
    D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLEND_DESTALPHA, D3DBLEND_INVDESTALPHA,
};
static const D3DBLEND kDstBlend[8] = {
    D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLEND_SRCCOLOR, D3DBLEND_INVSRCCOLOR,
    D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLEND_DESTALPHA, D3DBLEND_INVDESTALPHA,
};

static uint32_t sBadColorFmt;

/* The state for `s`: the stream's current one when it is the same, else a
 * new entry. 0xFFFFFFFF when the scene has no room left. */
static uint32_t state_add(const XbState *s, uint32_t cur)
{
    if (cur != 0xFFFFFFFF && memcmp(&sStates[cur], s, sizeof(*s)) == 0)
        return cur;
    if (sStateCount == MAX_STATES)
    {
        sDropped++;
        return 0xFFFFFFFF;
    }
    sStates[sStateCount] = *s;
    return sStateCount++;
}

static void decode_header(TaCtx *c, const uint32_t *w)
{
    uint32_t cmd = w[0], m1 = w[1], m2 = w[2], m3 = w[3];
    XbState s;

    memset(&s, 0, sizeof(s));
    c->sprite = (cmd >> 29) == PVR_HDR_SPRITE;
    c->uv16 = c->sprite ? 1 : (int)(cmd & 1);   /* a sprite's uv are always 16-bit */
    c->half = 0;
    c->strip_len = 0;
    if (c->sprite)
    {
        c->sprite_argb = w[4];
        c->sprite_oargb = w[5];
    }

    s.list = (uint8_t)c->list;
    s.sprite = (uint8_t)c->sprite;
    s.depth_cmp = (uint8_t)((m1 >> 29) & 7);
    /* a sprite is a screen rectangle: drawn whichever way it winds */
    s.cull = c->sprite ? (uint8_t)PVR_CULLING_NONE : (uint8_t)((m1 >> 27) & 3);
    s.depth_write = (uint8_t)!((m1 >> 26) & 1);
    s.src_sel = (uint8_t)((m2 >> 25) & 1);
    s.dst_sel = (uint8_t)((m2 >> 24) & 1);
    s.use_alpha = (uint8_t)((m2 >> 20) & 1);
    s.gouraud = c->sprite ? 0 : (uint8_t)((cmd >> 1) & 1);
    /* the opaque and punch-through lists do not blend on the PVR (see the top) */
    if (c->list == PVR_LIST_TR_POLY || c->list == PVR_LIST_TR_MOD)
    {
        s.blend_src = (uint8_t)((m2 >> 29) & 7);
        s.blend_dst = (uint8_t)((m2 >> 26) & 7);
        s.blend = !(s.blend_src == PVR_BLEND_ONE && s.blend_dst == PVR_BLEND_ZERO);
        s.mulpass = s.blend_src == PVR_BLEND_DESTCOLOR && s.blend_dst == PVR_BLEND_ZERO &&
                    !s.src_sel && !s.dst_sel;
    }
    s.fog = (uint8_t)((m2 >> 22) & 3);
    if (s.fog == PVR_FOG_VERTEX)
        s.fog_color = sFogVertexColor;
    else if (s.fog == PVR_FOG_TABLE || s.fog == PVR_FOG_TABLE2)
        s.fog = PVR_FOG_TABLE;
    else
        s.fog = PVR_FOG_DISABLE;
    if (((cmd >> 4) & 3) != PVR_CLRFMT_ARGBPACKED && !c->sprite && sBadColorFmt++ < 4)
        dbglog(DBG_ERROR, "pvr: colour format %u is drawn as packed ARGB\n",
               (unsigned)((cmd >> 4) & 3));

    if (s.src_sel)
    {
        /* the secondary buffer is the source colour: sampled pixel for pixel */
        if (secondary_target() == 0)
        {
            s.textured = 1;
            s.tex = sSecTex;
            s.linear = 1;
            s.tex_w = 1.0f;
            s.tex_h = 1.0f;
            s.env = PVR_TXRENV_REPLACE;
            s.filter = PVR_FILTER_NEAREST;
            s.addr_u = s.addr_v = D3DTADDRESS_CLAMP;
        }
        sUsesSecondary = 1;
    }
    else if ((cmd & 8) && (m1 & (1u << 25)))
    {
        uint32_t off = ((m3 & 0x1FFFFF) << 3) & (VRAM_SIZE - 1);
        uint8_t paletted = 0, bump = 0;

        s.textured = 1;
        s.env = (uint8_t)((m2 >> 6) & 3);
        s.ignore_tex_alpha = (uint8_t)((m2 >> 19) & 1);
        s.filter = (uint8_t)((m2 >> 13) & 3);
        s.offset = (uint8_t)((cmd >> 2) & 1);
        s.tex_w = (float)(8u << ((m2 >> 3) & 7));
        s.tex_h = (float)(8u << (m2 & 7));
        if (sRttAddr != NULL && off == (uint32_t)((uint8_t *)sRttAddr - sVram) &&
            ((m3 >> 26) & 1) && sRttTex != NULL)
        {
            /* the render-to-texture picture: a linear texture, texel UVs */
            s.tex = sRttTex;
            s.linear = 1;
        }
        else
        {
            s.tex = tex_lookup(m2, m3, &paletted, &bump);
            if (paletted == 4)
                s.pal = palette_for(((m3 >> 21) & 63) * 16, 16);
            else if (paletted == 8)
                s.pal = palette_for(((m3 >> 25) & 3) * 256, 256);
        }
        if (s.tex == NULL)
        {
            s.textured = 0;     /* drawn in its vertex colours, as a fallback */
        }
        else if (bump)
        {
            /* the offset colour is the light, not a colour; no fog on the
             * intensity pass (the colour pass after it is fogged) */
            s.bump = 1;
            s.bump_k1 = 128;    /* Doom's walls' (src/r_lights.c); set by the vertices */
            s.offset = 0;
            s.fog = PVR_FOG_DISABLE;
            s.fog_color = 0;
        }
        if (s.linear)
        {
            s.addr_u = s.addr_v = D3DTADDRESS_CLAMP;
        }
        else
        {
            uint32_t clamp = (m2 >> 15) & 3, flip = (m2 >> 17) & 3;

            s.addr_u = (clamp & 2) ? D3DTADDRESS_CLAMP : (flip & 2) ? D3DTADDRESS_MIRROR : D3DTADDRESS_WRAP;
            s.addr_v = (clamp & 1) ? D3DTADDRESS_CLAMP : (flip & 1) ? D3DTADDRESS_MIRROR : D3DTADDRESS_WRAP;
        }
    }

    if (s.dst_sel)
    {
        sUsesSecondary = 1;
        if (secondary_target() < 0)
            s.dst_sel = 0;
    }
    c->state = state_add(&s, c->state);
}

/* ========================================================== the vertices */

/* cos and sin of the bump light's rotation Q, a byte of a turn */
static float sQcos[256], sQsin[256];

static inline uint32_t enc_unit(float x)
{
    int v = (int)(x * 127.5f + 128.0f);

    return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static inline void make_vert(XbVert *o, const pvr_vertex_t *v, const XbState *s, uint32_t uv16)
{
    float z = v->z;
    float x = v->x * sXfScaleX + sXfOffX, y = v->y * sXfScaleY + sXfOffY;

    o->x = x - 0.5f;
    o->y = y - 0.5f;
    o->z = z * DEPTH_SCALE;
    if (!(o->z > 0.0f))
        o->z = 0.0f;            /* also catches NaN */
    else if (o->z > 1.0f)
        o->z = 1.0f;
    o->rhw = (z > 1e-30f) ? z : 1e-30f;
    o->diffuse = v->argb;
    o->specular = v->oargb;
    if (s->bump)
    {
        /* L = (K3 cos Q, K3 sin Q, K2) in the diffuse colour, -L in the
         * specular, both as DOT3 reads them; the vertex alpha stays */
        uint32_t oc = v->oargb;
        float k2 = (float)((oc >> 16) & 255) * (1.0f / 255.0f);
        float k3 = (float)((oc >> 8) & 255) * (1.0f / 255.0f);
        float lx = k3 * sQcos[oc & 255], ly = k3 * sQsin[oc & 255], lz = k2;

        o->diffuse = (v->argb & 0xFF000000u) | (enc_unit(lx) << 16) | (enc_unit(ly) << 8) | enc_unit(lz);
        o->specular = 0xFF000000u | (enc_unit(-lx) << 16) | (enc_unit(-ly) << 8) | enc_unit(-lz);
    }
    else if (s->fog == PVR_FOG_VERTEX)
    {
        /* PVR vertex fog takes its density from the offset colour's alpha,
         * 255 = all fog; Direct3D takes its fog factor from the specular
         * alpha with the opposite sense, 255 = no fog */
        o->specular = (v->oargb & 0x00FFFFFFu) | ((255u - (v->oargb >> 24)) << 24);
    }
    if (s->src_sel)
    {
        o->u = x;
        o->v = y;
        return;
    }
    if (uv16)
    {
        union { float f; uint32_t i; } a, b;

        a.i = v->argb0 & 0xFFFF0000u;   /* PVR_PACK_16BIT_UV: u high, v low */
        b.i = v->argb0 << 16;
        o->u = a.f;
        o->v = b.f;
    }
    else
    {
        o->u = v->u;
        o->v = v->v;
    }
    if (s->linear)
    {
        o->u *= s->tex_w;
        o->v *= s->tex_h;
    }
}

/* A translucent triangle's sort key: its farthest z (least 1/w) as bits that order as
 * the floats do (positive floats do; nothing in front of the eye has a
 * negative or NaN 1/w, which sorts first, as farthest) */
static inline uint32_t tri_key(float k)
{
    union { float f; uint32_t i; } u;

    if (!(k > 0.0f))
        return 0;
    u.f = k;
    return u.i;
}

static void emit_tri(TaCtx *c, const pvr_vertex_t *a, const pvr_vertex_t *b, const pvr_vertex_t *cc)
{
    const XbState *s;
    XbVert *o;
    int li;

    if (c->state == 0xFFFFFFFF)
        return;
    if (sVertCount + 3 > MAX_VERTS)
    {
        sDropped++;
        return;
    }
    s = &sStates[c->state];
    o = &sVerts[sVertCount];
    make_vert(&o[0], a, s, (uint32_t)c->uv16);
    make_vert(&o[1], b, s, (uint32_t)c->uv16);
    make_vert(&o[2], cc, s, (uint32_t)c->uv16);
    if (!s->gouraud)
    {
        /* flat: the strip's last vertex colours the triangle (its specular
         * as make_vert converted it, fog factor included) */
        o[0].diffuse = o[1].diffuse = o[2].diffuse;
        o[0].specular = o[1].specular = o[2].specular;
    }

    if ((s->list == PVR_LIST_TR_POLY || s->list == PVR_LIST_TR_MOD) && !s->mulpass)
    {
        XbTri *t;
        float k = a->z;

        if (b->z < k)
            k = b->z;
        if (cc->z < k)
            k = cc->z;
        t = &sTris[sTriCount];
        t->key = tri_key(k);
        t->state = c->state;
        t->first = sVertCount;
        t->solid = s->list == PVR_LIST_TR_POLY && s->depth_write && !s->bump && !s->src_sel &&
                   !s->dst_sel && (!s->blend || (s->blend_src == PVR_BLEND_SRCALPHA &&
                                                 s->blend_dst == PVR_BLEND_INVSRCALPHA)) &&
                   (o[0].diffuse & o[1].diffuse & o[2].diffuse) >> 24 == 255;
        sTriCount++;
        sVertCount += 3;
        return;
    }

    li = s->mulpass ? 2 : (s->list == PVR_LIST_PT_POLY) ? 1 : 0;
    if (sBatchCount[li] > 0)
    {
        XbBatch *last = &sBatches[li][sBatchCount[li] - 1];

        if (last->state == c->state && last->first + last->count == sVertCount)
        {
            last->count += 3;
            sVertCount += 3;
            return;
        }
    }
    if (sBatchCount[li] == MAX_BATCHES)
    {
        sDropped++;
        return;
    }
    sBatches[li][sBatchCount[li]].state = c->state;
    sBatches[li][sBatchCount[li]].first = sVertCount;
    sBatches[li][sBatchCount[li]].count = 3;
    sBatchCount[li]++;
    sVertCount += 3;
}

/* A bump polygon's K1 rides in its vertices' offset alpha; it is the
 * texture factor here, one per state, so a vertex with another K1 moves its
 * stream to a state of its own (Doom gives a surface one K1 throughout). */
static void bump_k1_follow(TaCtx *c, const pvr_vertex_t *v)
{
    uint8_t k1 = (uint8_t)(v->oargb >> 24);
    XbState s;

    if (c->state == 0xFFFFFFFF || !sStates[c->state].bump || sStates[c->state].bump_k1 == k1)
        return;
    s = sStates[c->state];
    s.bump_k1 = k1;
    c->state = state_add(&s, c->state);
}

static void vertex_in(TaCtx *c, const pvr_vertex_t *v)
{
    if (c->state == 0xFFFFFFFF)
        return;
    bump_k1_follow(c, v);
    if (c->strip_len < 2)
    {
        c->strip[c->strip_len++] = *v;
    }
    else
    {
        /* a strip of n vertices is n - 2 triangles; every second one is
         * wound the other way round, so its first two swap */
        if (c->strip_len & 1)
            emit_tri(c, &c->strip[1], &c->strip[0], v);
        else
            emit_tri(c, &c->strip[0], &c->strip[1], v);
        c->strip[0] = c->strip[1];
        c->strip[1] = *v;
        c->strip_len++;
    }
    if (v->flags & 0x10000000u)     /* end of strip */
        c->strip_len = 0;
}

/* A 64-byte sprite vertex: corners A B C given, D's x and y given, its z
 * and texture coordinates the PVR's, from the parallelogram A B C (D = A +
 * C - B); the colours are the header's. Two triangles, A B C and A C D. */
static void sprite_in(TaCtx *c, const uint32_t *w)
{
    union { uint32_t i; float f; } u;
    pvr_vertex_t a, b, cc, d;
    float ua, va, ub, vb, uc, vc;

#define SPR_F(n) (u.i = w[n], u.f)
#define UV_U(p) (u.i = (p) & 0xFFFF0000u, u.f)
#define UV_V(p) (u.i = (p) << 16, u.f)
    if (c->state == 0xFFFFFFFF)
        return;
    memset(&a, 0, sizeof(a));
    a.flags = PVR_CMD_VERTEX;
    a.argb = c->sprite_argb;
    a.oargb = c->sprite_oargb;
    b = cc = d = a;
    a.x = SPR_F(1);  a.y = SPR_F(2);  a.z = SPR_F(3);
    b.x = SPR_F(4);  b.y = SPR_F(5);  b.z = SPR_F(6);
    cc.x = SPR_F(7); cc.y = SPR_F(8); cc.z = SPR_F(9);
    d.x = SPR_F(10); d.y = SPR_F(11);
    d.z = a.z + cc.z - b.z;
    a.argb0 = w[13];
    b.argb0 = w[14];
    cc.argb0 = w[15];
    ua = UV_U(w[13]); va = UV_V(w[13]);
    ub = UV_U(w[14]); vb = UV_V(w[14]);
    uc = UV_U(w[15]); vc = UV_V(w[15]);
    d.argb0 = PVR_PACK_16BIT_UV(ua + uc - ub, va + vc - vb);
#undef SPR_F
#undef UV_U
#undef UV_V
    emit_tri(c, &a, &b, &cc);
    emit_tri(c, &a, &cc, &d);
}

/* The TA's reading of a parameter stream: 32-byte words, a header, then
 * vertices (64 bytes each after a sprite header). */
static void ta_stream(TaCtx *c, const void *data, size_t size)
{
    const uint32_t *w = (const uint32_t *)data;
    size_t n;

    for (n = 0; n + 32 <= size; n += 32, w += 8)
    {
        if (c->half)
        {
            uint32_t v[16];

            /* a sprite vertex's second half: whatever its first word is */
            memcpy(v, c->held, 32);
            memcpy(v + 8, w, 32);
            c->half = 0;
            sprite_in(c, v);
            continue;
        }
        switch (w[0] >> 29)
        {
            case PVR_HDR_POLY:
            case PVR_HDR_SPRITE:
                decode_header(c, w);
                break;
            case 7:                 /* vertex */
                if (c->sprite)
                {
                    memcpy(c->held, w, 32);
                    c->half = 1;
                }
                else
                {
                    vertex_in(c, (const pvr_vertex_t *)w);
                }
                break;
            case PVR_HDR_EOL:       /* end of list: the next needs a header */
                c->state = 0xFFFFFFFF;
                c->strip_len = 0;
                break;
            default:                /* user clip, object list set: nothing to draw */
                c->strip_len = 0;
                break;
        }
    }
}

static void ta_reset(TaCtx *c, int list)
{
    memset(c, 0, sizeof(*c));
    c->list = list;
    c->state = 0xFFFFFFFF;
}

int pvr_prim(const void *data, size_t size)
{
    if (!sSceneOpen || sCurList < 0)
        return -1;
    ta_stream(&sDirect, data, size);
    return 0;
}

/* ================================================== the DMA vertex buffers */

void *pvr_set_vertbuf(pvr_list_t list, void *buffer, size_t len)
{
    void *old;

    if ((int)list < 0 || (int)list >= LISTS)
        return NULL;
    old = sVbBase[list];
    sVbBase[list] = (uint8_t *)buffer;
    /* KOS halves the buffer to double-buffer it; this one is read before
     * the next frame starts, so the game gets the whole of it */
    sVbSize[list] = buffer ? len : 0;
    sVbPtr[list] = 0;
    return old;
}

void *pvr_vertbuf_tail(pvr_list_t list)
{
    if ((int)list < 0 || (int)list >= LISTS || sVbBase[list] == NULL)
        return NULL;
    return sVbBase[list] + sVbPtr[list];
}

void pvr_vertbuf_written(pvr_list_t list, size_t amt)
{
    if ((int)list < 0 || (int)list >= LISTS || sVbBase[list] == NULL)
        return;
    sVbPtr[list] += amt;
    if (sVbPtr[list] > sVbSize[list])
    {
        static int told;

        if (told++ < 4)
            dbglog(DBG_ERROR, "pvr: list %d's vertex buffer overran\n", (int)list);
        sVbPtr[list] = sVbSize[list];
    }
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size)
{
    if ((int)list < 0 || (int)list >= LISTS)
        return -1;
    if (sVbBase[list] == NULL)
    {
        /* a list with no buffer is the open one, drawn directly */
        if ((int)list == sCurList)
            return pvr_prim(data, size);
        return -1;
    }
    if (sVbPtr[list] + size > sVbSize[list])
    {
        static int told;

        if (told++ < 4)
            dbglog(DBG_ERROR, "pvr: list %d's vertex buffer is full\n", (int)list);
        return -1;
    }
    memcpy(sVbBase[list] + sVbPtr[list], data, size);
    sVbPtr[list] += size;
    return 0;
}

int pvr_list_flush(pvr_list_t list)
{
    (void)list;
    return 0;
}

/* ===================================================================== fog */

/* The fog density register: an 8-bit mantissa (1.7 fixed point) over a
 * signed 8-bit exponent -- the scale the PVR multiplies 1/w by before it
 * indexes the table. */
static void fog_density_set(uint32_t v)
{
    float m = (float)((v >> 8) & 0xFF) / 128.0f;

    sFogDensity = v;
    sFogScale = ldexpf(m, (int)(int8_t)(v & 0xFF));
}

/* The table's density at 1/w = z, 0..255: the scaled 1/w clamped to
 * [1, 256), its exponent the entry's high three bits, the top four bits of
 * its mantissa the low four, the rest of the mantissa the step from this
 * entry's density to the next one's. */
static inline uint32_t fog_lookup(float z)
{
    union { float f; uint32_t i; } u;
    uint32_t e, idx, ent, hi, lo, frac;

    u.f = z * sFogScale;
    if (!(u.f > 1.0f))
        u.f = 1.0f;
    else if (u.f > 255.99f)
        u.f = 255.99f;
    e = ((u.i >> 23) & 0xFF) - 127;
    idx = (e << 4) | ((u.i >> 19) & 15);
    frac = (u.i >> 11) & 255;
    ent = sFogTable[idx & 127];
    hi = (ent >> 8) & 255;
    lo = ent & 255;
    return (hi * (256 - frac) + lo * frac + 128) >> 8;
}

/* table fog for `n` vertices: the factor into the specular alpha */
static void fog_apply(XbVert *v, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        v[i].specular = (v[i].specular & 0x00FFFFFFu) | ((255u - fog_lookup(v[i].rhw)) << 24);
}

/* ============================================================= the scene */

/* The picture's place on the screen: the 4:3 picture, as large as the
 * screen's height allows, centred. */
static void update_picture(void)
{
    float picW;
    D3DRECT area;
    int bars;

    sDispScaleY = (float)sDispH / (float)SCR_H;
    /* a 16:9 screen shows the 4:3 picture three quarters of its width */
    picW = sDisp16x9 ? (float)sDispW * 0.75f : (float)sDispW;
    sDispScaleX = picW / (float)SCR_W;
    sDispOffX = ((float)sDispW - (float)SCR_W * sDispScaleX) * 0.5f;
    sDispOffY = 0.0f;
    area.x1 = (LONG)(((float)sDispW - picW) * 0.5f + 0.5f);
    area.y1 = 0;
    area.x2 = (LONG)sDispW - area.x1;
    area.y2 = (LONG)sDispH;
    bars = (area.x1 != 0);
    if (area.x1 != sDispArea.x1 || bars != sDispBars)
    {
        /* the scissors follow the picture: re-applied at the next target */
        sDispArea = area;
        sDispBars = bars;
        if (sDev != NULL)
            IDirect3DDevice8_SetScissors(sDev, 0, FALSE, NULL);
        sTargetNow = NULL;
    }
}

void pvr_scene_begin(void)
{
    int i;

    if (!sInited)
        return;
    sSceneOpen = 1;
    sSceneToTexture = 0;
    update_picture();
    sXfScaleX = sDispScaleX;
    sXfScaleY = sDispScaleY;
    sXfOffX = sDispOffX;
    sXfOffY = sDispOffY;
    sVertCount = 0;
    sStateCount = 0;
    sBatchCount[0] = sBatchCount[1] = sBatchCount[2] = 0;
    sTriCount = 0;
    ta_reset(&sDirect, -1);
    sCurList = -1;
    sUsesSecondary = 0;
    for (i = 0; i < LISTS; i++)
        sVbPtr[i] = 0;
}

int pvr_list_begin(pvr_list_t list)
{
    if (!sSceneOpen)
        return -1;
    sCurList = (int)list;
    ta_reset(&sDirect, (int)list);  /* a header in the new list is needed first */
    return 0;
}

int pvr_list_finish(void)
{
    sCurList = -1;
    ta_reset(&sDirect, -1);
    return 0;
}

/* --- drawing --- */

static const XbState *sApplied;
static int sAppliedBump = -1;

static void stage_tex(DWORD st, const XbState *s)
{
    IDirect3DDevice8 *d = sDev;
    DWORD f = (s->filter == PVR_FILTER_NEAREST) ? D3DTEXF_POINT : D3DTEXF_LINEAR;

    IDirect3DDevice8_SetTexture(d, st, (D3DBaseTexture *)s->tex);
    IDirect3DDevice8_SetTextureStageState(d, st, D3DTSS_MINFILTER, f);
    IDirect3DDevice8_SetTextureStageState(d, st, D3DTSS_MAGFILTER, f);
    IDirect3DDevice8_SetTextureStageState(d, st, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice8_SetTextureStageState(d, st, D3DTSS_ADDRESSU, s->addr_u);
    IDirect3DDevice8_SetTextureStageState(d, st, D3DTSS_ADDRESSV, s->addr_v);
}

/* The bump pass (see the top of the file): I = K1 + N.L, signed, in four
 * stages -- N.-L into TEMP, N.L, + K1 (the texture factor), - TEMP. */
static void apply_bump(const XbState *s)
{
    IDirect3DDevice8 *d = sDev;
    uint32_t k1 = s->bump_k1;

    stage_tex(0, s);
    stage_tex(1, s);
    IDirect3DDevice8_SetRenderState(d, D3DRS_TEXTUREFACTOR, 0xFF000000u | (k1 << 16) | (k1 << 8) | k1);

    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLORARG2, D3DTA_SPECULAR);
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_RESULTARG, D3DTA_TEMP);

    IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
    IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_RESULTARG, D3DTA_CURRENT);

    IDirect3DDevice8_SetTexture(d, 2, NULL);
    IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_COLOROP, D3DTOP_ADD);
    IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_COLORARG1, D3DTA_CURRENT);
    IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_COLORARG2, D3DTA_TFACTOR);
    IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_RESULTARG, D3DTA_CURRENT);

    IDirect3DDevice8_SetTexture(d, 3, NULL);
    IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_COLOROP, D3DTOP_SUBTRACT);
    IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_COLORARG1, D3DTA_CURRENT);
    IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_COLORARG2, D3DTA_TEMP);
    IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    /* the header's "use alpha" off: every pixel is opaque */
    IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_ALPHAARG1, s->use_alpha ? D3DTA_CURRENT : D3DTA_TFACTOR);
    IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_RESULTARG, D3DTA_CURRENT);
}

/* how a solid translucent triangle (see the top) is being drawn: 0 not one
 * or not split, 1 its opaque texels before the sort, 2 the rest in it */
static int sSolidPass, sAppliedSolid;

static void apply_state(const XbState *s)
{
    IDirect3DDevice8 *d = sDev;

    if (sApplied != NULL && sAppliedSolid == sSolidPass && memcmp(sApplied, s, sizeof(*s)) == 0)
        return;
    sApplied = s;
    sAppliedSolid = sSolidPass;

    /* Autosort allows a translucent polygon one depth compare, "greater or
     * equal"; Doom's surface colour lands on its own bump result at the
     * same depth, and a GREATER there would draw nothing. */
    if (s->list == PVR_LIST_TR_POLY && sAutosort)
        IDirect3DDevice8_SetRenderState(d, D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
    else if (s->list == PVR_LIST_PT_POLY && (s->depth_cmp & 7) == PVR_DEPTHCMP_GREATER)
        IDirect3DDevice8_SetRenderState(d, D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
    else
        IDirect3DDevice8_SetRenderState(d, D3DRS_ZFUNC, kCmp[s->depth_cmp & 7]);
    IDirect3DDevice8_SetRenderState(d, D3DRS_ZWRITEENABLE, s->depth_write ? TRUE : FALSE);
    IDirect3DDevice8_SetRenderState(d, D3DRS_CULLMODE, s->cull == PVR_CULLING_CCW ? D3DCULL_CCW
                                      : s->cull == PVR_CULLING_CW ? D3DCULL_CW : D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHABLENDENABLE, s->blend ? TRUE : FALSE);
    if (s->blend)
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_SRCBLEND, kSrcBlend[s->blend_src & 7]);
        IDirect3DDevice8_SetRenderState(d, D3DRS_DESTBLEND, kDstBlend[s->blend_dst & 7]);
    }
    if (s->list == PVR_LIST_PT_POLY)
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHATESTENABLE, TRUE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHAREF, sPtAlphaRef & 0xFF);
    }
    else if (s->list == PVR_LIST_TR_POLY && s->depth_write && s->blend &&
             s->blend_src == PVR_BLEND_SRCALPHA && s->blend_dst == PVR_BLEND_INVSRCALPHA)
    {
        /* A translucent polygon that writes depth writes it only where it
         * covers something: the clear texels of a sprite would otherwise
         * punch depth holes in whatever is drawn behind them later.
         * GREATER than 0 drops only alpha 0 -- which, blended by its own
         * alpha, draws nothing anyway. (Under any other blend a clear texel
         * still changes the pixel: Doom's weapon is multiplied into the
         * secondary buffer, alpha and all, and only that says where the
         * weapon is not.) */
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHATESTENABLE, TRUE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHAREF, 0);
    }
    else
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHATESTENABLE, FALSE);
    }
    if (sSolidPass)
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHATESTENABLE, TRUE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHAFUNC, sSolidPass == 1 ? D3DCMP_GREATEREQUAL : D3DCMP_LESS);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHAREF, 255);
        if (sSolidPass == 2)
            IDirect3DDevice8_SetRenderState(d, D3DRS_ZWRITEENABLE, FALSE);
    }

    /* fog: the factor comes in the specular alpha (make_vert, fog_apply);
     * FOGTABLEMODE NONE is what makes Direct3D take it from there for
     * pre-transformed vertices */
    if (s->fog == PVR_FOG_VERTEX || s->fog == PVR_FOG_TABLE)
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_FOGENABLE, TRUE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_FOGCOLOR,
                                        (D3DCOLOR)(s->fog == PVR_FOG_TABLE ? sFogTableColor : s->fog_color));
    }
    else
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_FOGENABLE, FALSE);
    }

    if (s->bump)
    {
        IDirect3DDevice8_SetRenderState(d, D3DRS_SPECULARENABLE, FALSE);
        apply_bump(s);
        sAppliedBump = 1;
        return;
    }
    if (sAppliedBump != 0)
    {
        /* back from the bump pass's four stages to the usual two */
        IDirect3DDevice8_SetRenderState(d, D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_RESULTARG, D3DTA_CURRENT);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_RESULTARG, D3DTA_CURRENT);
        IDirect3DDevice8_SetTexture(d, 1, NULL);
        IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        IDirect3DDevice8_SetTextureStageState(d, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_COLOROP, D3DTOP_DISABLE);
        IDirect3DDevice8_SetTextureStageState(d, 3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        sAppliedBump = 0;
    }
    IDirect3DDevice8_SetRenderState(d, D3DRS_SPECULARENABLE, (s->textured && s->offset) ? TRUE : FALSE);

    if (s->textured)
    {
        stage_tex(0, s);
        if (s->pal)
            IDirect3DDevice8_SetPalette(d, 0, s->pal);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        /* The PVR's texture environments:
         *   REPLACE        c = Ct           a = At
         *   MODULATE       c = Cf * Ct      a = At
         *   DECAL          c = Ct*At + Cf*(1 - At)   a = Af
         *   MODULATEALPHA  c = Cf * Ct      a = Af * At
         * with At = 1 when the header ignores the texture's alpha. */
        switch (s->env)
        {
            case PVR_TXRENV_REPLACE:
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                if (s->ignore_tex_alpha)
                    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
                break;
            case PVR_TXRENV_MODULATE:
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                if (s->ignore_tex_alpha)
                    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
                break;
            case PVR_TXRENV_DECAL:
                if (s->ignore_tex_alpha)
                    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                else
                    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA);
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                break;
            case PVR_TXRENV_MODULATEALPHA:
            default:
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP,
                                        s->ignore_tex_alpha ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE);
                break;
        }
    }
    else
    {
        IDirect3DDevice8_SetTexture(d, 0, NULL);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    }

    /* the header's "use alpha" off: every pixel is opaque */
    if (!s->use_alpha)
    {
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    }
    else
    {
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }
}

static void set_target(IDirect3DSurface8 *rt)
{
    if (rt != sTargetNow)
    {
        IDirect3DDevice8_SetRenderTarget(sDev, rt, sDepth);
        sTargetNow = rt;
        sApplied = NULL;
        /* With side bars, nothing is drawn outside the picture: the PVR
         * clips at its 640x480 edges, and what a game draws past them
         * would land in the bars. A texture target is in game pixels and
         * has none. */
        if (sDispBars)
        {
            if (rt == sRttSurf)
                IDirect3DDevice8_SetScissors(sDev, 0, FALSE, NULL);
            else
                IDirect3DDevice8_SetScissors(sDev, 1, FALSE, &sDispArea);
        }
    }
}

static void target_for(const XbState *s)
{
    if (s->dst_sel)
        set_target(sSecSurf);
    else
        set_target(sSceneToTexture ? sRttSurf : sBackBuf);
}

static void draw_batches(int li)
{
    uint32_t i;

    for (i = 0; i < sBatchCount[li]; i++)
    {
        const XbBatch *b = &sBatches[li][i];
        const XbState *s = &sStates[b->state];

        target_for(s);
        apply_state(s);
        if (s->fog == PVR_FOG_TABLE)
            fog_apply(&sVerts[b->first], b->count);
        IDirect3DDevice8_DrawPrimitiveUP(sDev, D3DPT_TRIANGLELIST, b->count / 3, &sVerts[b->first], sizeof(XbVert));
    }
}

/* far to near: a stable LSD radix sort on the keys, ties in submission
 * order; a byte every triangle shares is skipped */
static void sort_tris(void)
{
    static uint32_t count[256];
    XbTri *src = sTris, *dst = sTrisTmp, *t;
    uint32_t shift, i, n = sTriCount;

    for (shift = 0; shift < 32; shift += 8)
    {
        uint32_t sum = 0;

        memset(count, 0, sizeof(count));
        for (i = 0; i < n; i++)
            count[(src[i].key >> shift) & 255]++;
        if (count[(src[0].key >> shift) & 255] == n)
            continue;
        for (i = 0; i < 256; i++)
        {
            uint32_t c = count[i];

            count[i] = sum;
            sum += c;
        }
        for (i = 0; i < n; i++)
            dst[count[(src[i].key >> shift) & 255]++] = src[i];
        t = src;
        src = dst;
        dst = t;
    }
    if (src != sTris)
        memcpy(sTris, src, n * sizeof(XbTri));
}

static XbVert *sSorted;

/* The translucent list, after the opaque and punch-through ones as the PVR
 * renders a tile: sorted far to near when autosort is on, as the game's
 * pvr_init asks, else in the order it was submitted. Copied out in chunks
 * in that order, table fog applied on the way, and drawn in runs of one
 * state. */
static void draw_tri_runs(uint32_t base, uint32_t chunk)
{
    uint32_t start = 0, n;

    while (start < chunk)
    {
        const XbTri *t = &sTris[base + start];
        uint32_t st = t->state, solid = t->solid;

        n = 1;
        while (start + n < chunk && sTris[base + start + n].state == st && sTris[base + start + n].solid == solid)
            n++;
        if (sSolidPass != 1)
            sSolidPass = (sAutosort && solid) ? 2 : 0;
        target_for(&sStates[st]);
        apply_state(&sStates[st]);
        IDirect3DDevice8_DrawPrimitiveUP(sDev, D3DPT_TRIANGLELIST, n, &sSorted[start * 3], sizeof(XbVert));
        start += n;
    }
}

static void draw_translucent(void)
{
    uint32_t base, i, chunk;

    if (sTriCount == 0)
        return;
    if (sAutosort)
    {
        /* the solid texels first, in the order sent, writing depth */
        sSolidPass = 1;
        for (base = 0; base < sTriCount; )
        {
            uint32_t m = 0;

            while (base < sTriCount && m < SORT_CHUNK)
            {
                const XbTri *t = &sTris[base++];

                if (!t->solid)
                    continue;
                memcpy(&sSorted[m * 3], &sVerts[t->first], 3 * sizeof(XbVert));
                if (sStates[t->state].fog == PVR_FOG_TABLE)
                    fog_apply(&sSorted[m * 3], 3);
                sTrisTmp[m++] = *t;
            }
            /* draw_tri_runs reads its triangles from sTris: lend it these */
            if (m > 0)
            {
                XbTri *keep = sTris;

                sTris = sTrisTmp;
                draw_tri_runs(0, m);
                sTris = keep;
            }
        }
        sSolidPass = 0;
        if (sTriCount > 1)
            sort_tris();
    }
    for (base = 0; base < sTriCount; base += chunk)
    {
        chunk = sTriCount - base;
        if (chunk > SORT_CHUNK)
            chunk = SORT_CHUNK;
        for (i = 0; i < chunk; i++)
        {
            const XbTri *t = &sTris[base + i];

            memcpy(&sSorted[i * 3], &sVerts[t->first], 3 * sizeof(XbVert));
            if (sStates[t->state].fog == PVR_FOG_TABLE)
                fog_apply(&sSorted[i * 3], 3);
        }
        draw_tri_runs(base, chunk);
    }
    sSolidPass = 0;
}

int pvr_scene_finish(void)
{
    IDirect3DSurface8 *target;
    float zc;
    int li;

    if (!sSceneOpen)
        return -1;
    sSceneOpen = 0;

    /* the DMA lists, read as the TA reads them once the direct list is
     * done: each from its own buffer, with a stream of its own */
    for (li = 0; li < LISTS; li++)
    {
        if (sVbBase[li] != NULL && sVbPtr[li] > 0)
        {
            TaCtx c;

            ta_reset(&c, li);
            ta_stream(&c, sVbBase[li], sVbPtr[li]);
        }
    }

    target = sSceneToTexture ? sRttSurf : sBackBuf;
    sTargetNow = NULL;
    sApplied = NULL;
    IDirect3DDevice8_BeginScene(sDev);
    if (sUsesSecondary && sSecSurf != NULL)
    {
        /* the secondary accumulation buffer starts each scene empty */
        set_target(sSecSurf);
        IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
    }
    set_target(target);
    /* the background plane: the scene's colour at the Z clip depth */
    zc = sZClip * DEPTH_SCALE;
    if (!(zc >= 0.0f))
        zc = 0.0f;
    if (zc > 1.0f)
        zc = 1.0f;
    if (sDispBars && !sSceneToTexture)
    {
        /* the whole screen black (Clear ignores the scissors), then the
         * picture in the scene's colour */
        IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                    0xFF000000, zc, 0);
        IDirect3DDevice8_Clear(sDev, 1, &sDispArea, D3DCLEAR_TARGET, sBgColor, zc, 0);
    }
    else
        IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                    sBgColor, zc, 0);

    draw_batches(0);            /* opaque */
    draw_batches(1);            /* punch-through */
    draw_batches(2);            /* translucent: the colour over the bump light */
    draw_translucent();         /* translucent: the rest, far to near */

    set_target(sBackBuf);
    IDirect3DDevice8_SetTexture(sDev, 0, NULL);
    IDirect3DDevice8_SetTexture(sDev, 1, NULL);
    if (!sVideoEnabled && !sSceneToTexture)
    {
        /* vid_set_enabled(0): the Dreamcast's video output is off, the
         * screen black, while the game keeps rendering */
        IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET, 0xFF000000, 0.0f, 0);
    }
    IDirect3DDevice8_EndScene(sDev);

    if (!sSceneToTexture)
    {
        /* KOS flips at the vertical blank; interval one does the same */
        IDirect3DDevice8_Present(sDev, NULL, NULL, NULL, NULL);
    }
    sSceneToTexture = 0;

    if (sDropped)
    {
        static uint32_t told;

        if (told++ < 8)
            dbglog(DBG_WARNING, "pvr: scene over budget, %u primitives dropped\n",
                   (unsigned)sDropped);
        sDropped = 0;
    }

    sFrame++;
    run_deferred();
    tex_sweep();
    xbox_maple_poll();
    xbox_maple_rumble_pump();
    return 0;
}

int pvr_wait_ready(void)
{
    /* The scene is drawn and presented in pvr_scene_finish, which waits for
     * the vertical blank; there is never a previous scene still in flight
     * for the next one to wait on. */
    return 0;
}

int pvr_check_ready(void)
{
    return 0;
}

int pvr_wait_render_done(void)
{
    /* Texture memory here is CPU memory the GPU never reads (it reads the
     * Direct3D textures made from it), so a VRAM write cannot race a
     * render. */
    return 0;
}

/* ============================================================= init */

int pvr_init(const pvr_init_params_t *params)
{
    D3DPRESENT_PARAMETERS pp;
    uint8_t *base;
    HRESULT hr;
    int i;

    /* bin sizes and vertex buffers are the TA's; the sort mode is kept */
    sAutosort = (params == NULL) || !params->autosort_disabled;
    if (sInited)
        return 0;

    InitializeCriticalSection(&sVramLock);

    /* texture memory: reserve 24 MB of address space, commit the 8 MB at
     * its 16 MB boundary, so the low 24 bits of a texture's address are
     * its offset (KOS's header encoding of an address keeps 24). */
    base = (uint8_t *)VirtualAlloc(NULL, 3 * VRAM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    if (base == NULL)
    {
        dbglog(DBG_CRITICAL, "pvr: no address space for texture memory\n");
        return -1;
    }
    sVram = (uint8_t *)(((uintptr_t)base + 2 * VRAM_SIZE - 1) & ~(uintptr_t)(2 * VRAM_SIZE - 1));
    if (VirtualAlloc(sVram, VRAM_SIZE, MEM_COMMIT, PAGE_READWRITE) == NULL)
    {
        dbglog(DBG_CRITICAL, "pvr: no memory for the 8 MB of texture memory\n");
        return -1;
    }
    vram_init_alloc();

    sVerts = (XbVert *)malloc(MAX_VERTS * sizeof(XbVert));
    sSorted = (XbVert *)malloc(SORT_CHUNK * 3 * sizeof(XbVert));
    sTris = (XbTri *)malloc((MAX_VERTS / 3) * sizeof(XbTri));
    sTrisTmp = (XbTri *)malloc((MAX_VERTS / 3) * sizeof(XbTri));
    sStates = (XbState *)malloc(MAX_STATES * sizeof(XbState));
    sBatches[0] = (XbBatch *)malloc(MAX_BATCHES * sizeof(XbBatch));
    sBatches[1] = (XbBatch *)malloc(MAX_BATCHES * sizeof(XbBatch));
    sBatches[2] = (XbBatch *)malloc(MAX_BATCHES * sizeof(XbBatch));
    if (!sVerts || !sSorted || !sTris || !sTrisTmp || !sStates || !sBatches[0] || !sBatches[1] ||
        !sBatches[2])
    {
        dbglog(DBG_CRITICAL, "pvr: no memory for the vertex and batch buffers\n");
        return -1;
    }
    for (i = 0; i < 256; i++)
    {
        float q = (float)i * (6.28318531f / 255.0f);

        sQcos[i] = cosf(q);
        sQsin[i] = sinf(q);
    }

    sD3D = Direct3DCreate8(D3D_SDK_VERSION);
    if (sD3D == NULL)
    {
        dbglog(DBG_CRITICAL, "pvr: Direct3DCreate8 failed\n");
        return -1;
    }

    /* The mode, from the dashboard's settings: 720p when the component
     * (HDTV) pack is in and 720p is ticked, else 480p when that is ticked
     * -- as the Dreamcast build picks VGA's progressive mode when the
     * cable allows it -- else 480i. 60 Hz throughout. At 720p the game's
     * 4:3 picture is drawn at 960x720, a 1.5x scale of its 640x480, with
     * black bars either side: the game's camera and HUD are 4:3, so
     * filling 16:9 would stretch everything. If 720p's buffers cannot be
     * had, 480 is tried before giving up. */
    {
        DWORD vflags = XGetVideoFlags();
        int want720 = XGetAVPack() == XC_AV_PACK_HDTV && (vflags & XC_VIDEO_FLAGS_HDTV_720p);
        /* the dashboard's widescreen setting: a 16:9 television, sent the
           480 lines anamorphic */
        int wide480 = (vflags & XC_VIDEO_FLAGS_WIDESCREEN) != 0;

        for (;;)
        {
            memset(&pp, 0, sizeof(pp));
            if (want720)
            {
                sDispW = 1280;
                sDispH = 720;
                pp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
            }
            else
            {
                sDispW = SCR_W;
                sDispH = SCR_H;
                if (vflags & XC_VIDEO_FLAGS_HDTV_480p)
                    pp.Flags = D3DPRESENTFLAG_PROGRESSIVE;
                if (wide480)
                    pp.Flags |= D3DPRESENTFLAG_WIDESCREEN;
            }
            sDisp16x9 = want720 || wide480;
            pp.BackBufferWidth = sDispW;
            pp.BackBufferHeight = sDispH;
            pp.BackBufferFormat = D3DFMT_A8R8G8B8;
            pp.BackBufferCount = 1;
            pp.EnableAutoDepthStencil = TRUE;
            pp.AutoDepthStencilFormat = D3DFMT_F24S8;
            pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
            hr = IDirect3D8_CreateDevice(sD3D, 0, D3DDEVTYPE_HAL, NULL,
                                         D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &sDev);
            if (SUCCEEDED(hr) || !want720)
                break;
            dbglog(DBG_ERROR, "pvr: 720p CreateDevice failed (%08lx); trying 480\n",
                   (unsigned long)hr);
            want720 = 0;
        }
    }
    if (FAILED(hr))
    {
        dbglog(DBG_CRITICAL, "pvr: CreateDevice failed (%08lx)\n", (unsigned long)hr);
        return -1;
    }
    sDispArea.x1 = -1;
    update_picture();
    IDirect3DDevice8_GetBackBuffer(sDev, 0, D3DBACKBUFFER_TYPE_MONO, &sBackBuf);
    IDirect3DDevice8_GetDepthStencilSurface(sDev, &sDepth);

    IDirect3DDevice8_SetVertexShader(sDev, XB_FVF);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ZENABLE, D3DZB_TRUE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_DITHERENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    for (i = 0; i < 4; i++)
    {
        IDirect3DDevice8_SetTextureStageState(sDev, i, D3DTSS_TEXCOORDINDEX, 0);
        IDirect3DDevice8_SetTextureStageState(sDev, i, D3DTSS_RESULTARG, D3DTA_CURRENT);
        if (i > 0)
        {
            IDirect3DDevice8_SetTextureStageState(sDev, i, D3DTSS_COLOROP, D3DTOP_DISABLE);
            IDirect3DDevice8_SetTextureStageState(sDev, i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        }
    }
    sAppliedBump = 0;

    /* a black first frame rather than whatever the boot left */
    IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF000000, 0.0f, 0);
    IDirect3DDevice8_Present(sDev, NULL, NULL, NULL, NULL);

    sInited = 1;
    dbglog(DBG_INFO, "pvr: Direct3D up, %ux%u%s, picture %ldx%ld at %ld, float depth, "
           "texture RAM at %p\n", (unsigned)sDispW, (unsigned)sDispH,
           (pp.Flags & D3DPRESENTFLAG_PROGRESSIVE) ? " progressive" : "",
           (long)(sDispArea.x2 - sDispArea.x1), (long)(sDispArea.y2 - sDispArea.y1),
           (long)sDispArea.x1, (void *)sVram);
    return 0;
}

int pvr_init_defaults(void)
{
    return pvr_init(NULL);
}

int pvr_shutdown(void)
{
    return 0;
}

/* ===================================================== header compiler
 * KallistiOS's own (kernel/arch/dreamcast/hardware/pvr/pvr_prim.c), so the
 * words the port's headers carry are exactly the Dreamcast's. */

void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src)
{
    uint32_t cmd, mode2, mode3;

    cmd = PVR_CMD_POLYHDR
        | FIELD_PREP(PVR_TA_CMD_TXRENABLE, (uint32_t)src->txr.enable)
        | FIELD_PREP(PVR_TA_CMD_TYPE, (uint32_t)src->list_type)
        | FIELD_PREP(PVR_TA_CMD_CLRFMT, (uint32_t)src->fmt.color)
        | FIELD_PREP(PVR_TA_CMD_SHADE, (uint32_t)src->gen.shading)
        | FIELD_PREP(PVR_TA_CMD_UVFMT, (uint32_t)src->fmt.uv)
        | FIELD_PREP(PVR_TA_CMD_USERCLIP, (uint32_t)src->gen.clip_mode)
        | FIELD_PREP(PVR_TA_CMD_MODIFIER, (uint32_t)src->fmt.modifier)
        | FIELD_PREP(PVR_TA_CMD_MODIFIERMODE, (uint32_t)src->gen.modifier_mode)
        | FIELD_PREP(PVR_TA_CMD_SPECULAR, (uint32_t)src->gen.specular);
    dst->cmd = cmd;

    dst->mode1 = FIELD_PREP(PVR_TA_PM1_DEPTHCMP, (uint32_t)src->depth.comparison)
        | FIELD_PREP(PVR_TA_PM1_CULLING, (uint32_t)src->gen.culling)
        | FIELD_PREP(PVR_TA_PM1_DEPTHWRITE, (uint32_t)src->depth.write)
        | FIELD_PREP(PVR_TA_PM1_TXRENABLE, (uint32_t)src->txr.enable);

    mode2 = FIELD_PREP(PVR_TA_PM2_SRCBLEND, (uint32_t)src->blend.src)
        | FIELD_PREP(PVR_TA_PM2_DSTBLEND, (uint32_t)src->blend.dst)
        | FIELD_PREP(PVR_TA_PM2_SRCENABLE, (uint32_t)src->blend.src_enable)
        | FIELD_PREP(PVR_TA_PM2_DSTENABLE, (uint32_t)src->blend.dst_enable)
        | FIELD_PREP(PVR_TA_PM2_FOG, (uint32_t)src->gen.fog_type)
        | FIELD_PREP(PVR_TA_PM2_CLAMP, (uint32_t)src->gen.color_clamp)
        | FIELD_PREP(PVR_TA_PM2_ALPHA, (uint32_t)src->gen.alpha);

    if (!src->txr.enable)
    {
        mode3 = 0;
    }
    else
    {
        mode2 |= FIELD_PREP(PVR_TA_PM2_TXRALPHA, (uint32_t)src->txr.alpha)
            | FIELD_PREP(PVR_TA_PM2_UVFLIP, (uint32_t)src->txr.uv_flip)
            | FIELD_PREP(PVR_TA_PM2_UVCLAMP, (uint32_t)src->txr.uv_clamp)
            | FIELD_PREP(PVR_TA_PM2_FILTER, (uint32_t)src->txr.filter)
            | FIELD_PREP(PVR_TA_PM2_MIPBIAS, (uint32_t)src->txr.mipmap_bias)
            | FIELD_PREP(PVR_TA_PM2_TXRENV, (uint32_t)src->txr.env)
            | FIELD_PREP(PVR_TA_PM2_USIZE, (uint32_t)(__builtin_ctz(src->txr.width) - 3))
            | FIELD_PREP(PVR_TA_PM2_VSIZE, (uint32_t)(__builtin_ctz(src->txr.height) - 3));
        mode3 = FIELD_PREP(PVR_TA_PM3_MIPMAP, (uint32_t)src->txr.mipmap)
            | (uint32_t)src->txr.format
            | to_pvr_txr_ptr(src->txr.base);
    }
    dst->mode2 = mode2;
    dst->mode3 = mode3;
    if (src->fmt.modifier && src->gen.modifier_mode)
    {
        dst->mode2_1 = mode2;
        dst->mode3_1 = mode3;
    }
}

void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list)
{
    bool alpha;

    memset(dst, 0, sizeof(pvr_poly_cxt_t));
    dst->list_type = list;
    alpha = list > PVR_LIST_OP_MOD;
    dst->fmt.color = PVR_CLRFMT_ARGBPACKED;
    dst->fmt.uv = false;
    dst->gen.shading = true;
    dst->depth.comparison = PVR_DEPTHCMP_GREATER;
    dst->depth.write = PVR_DEPTHWRITE_ENABLE;
    dst->gen.culling = PVR_CULLING_CCW;
    dst->txr.enable = false;
    dst->gen.alpha = alpha;
    if (!alpha)
    {
        dst->blend.src = PVR_BLEND_ONE;
        dst->blend.dst = PVR_BLEND_ZERO;
    }
    else
    {
        dst->blend.src = PVR_BLEND_SRCALPHA;
        dst->blend.dst = PVR_BLEND_INVSRCALPHA;
    }
    dst->blend.src_enable = PVR_BLEND_DISABLE;
    dst->blend.dst_enable = PVR_BLEND_DISABLE;
    dst->gen.fog_type = PVR_FOG_DISABLE;
    dst->gen.color_clamp = false;
}

void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list,
                                 int textureformat, int tw, int th, pvr_ptr_t textureaddr,
                                 int filtering)
{
    bool alpha;

    memset(dst, 0, sizeof(pvr_poly_cxt_t));
    dst->list_type = list;
    alpha = list > PVR_LIST_OP_MOD;
    dst->fmt.color = PVR_CLRFMT_ARGBPACKED;
    dst->fmt.uv = false;
    dst->gen.shading = true;
    dst->depth.comparison = PVR_DEPTHCMP_GREATER;
    dst->depth.write = PVR_DEPTHWRITE_ENABLE;
    dst->gen.culling = PVR_CULLING_CCW;
    dst->txr.enable = true;
    dst->gen.alpha = alpha;
    dst->txr.alpha = false;
    if (!alpha)
    {
        dst->blend.src = PVR_BLEND_ONE;
        dst->blend.dst = PVR_BLEND_ZERO;
        dst->txr.env = PVR_TXRENV_MODULATE;
    }
    else
    {
        dst->blend.src = PVR_BLEND_SRCALPHA;
        dst->blend.dst = PVR_BLEND_INVSRCALPHA;
        dst->txr.env = PVR_TXRENV_MODULATEALPHA;
    }
    dst->blend.src_enable = PVR_BLEND_DISABLE;
    dst->blend.dst_enable = PVR_BLEND_DISABLE;
    dst->gen.fog_type = PVR_FOG_DISABLE;
    dst->gen.color_clamp = false;
    dst->txr.uv_flip = PVR_UVFLIP_NONE;
    dst->txr.uv_clamp = PVR_UVCLAMP_NONE;
    dst->txr.filter = (pvr_filter_mode_t)filtering;
    dst->txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
    dst->txr.width = tw;
    dst->txr.height = th;
    dst->txr.base = textureaddr;
    dst->txr.format = textureformat;
}

/* ===================================================== sprite compiler
 * KallistiOS v2.2.1's own (hardware/pvr/pvr_prim.c), so the words the
 * game's sprite headers carry are exactly the Dreamcast's. */

void pvr_sprite_cxt_col(pvr_sprite_cxt_t *dst, pvr_list_t list)
{
    int alpha;

    memset(dst, 0, sizeof(pvr_sprite_cxt_t));
    dst->list_type = list;
    alpha = list > PVR_LIST_OP_MOD;
    dst->depth.comparison = PVR_DEPTHCMP_GREATER;
    dst->depth.write = PVR_DEPTHWRITE_ENABLE;
    dst->gen.culling = PVR_CULLING_CCW;
    dst->txr.enable = PVR_TEXTURE_DISABLE;
    if (!alpha)
    {
        dst->gen.alpha = PVR_ALPHA_DISABLE;
        dst->blend.src = PVR_BLEND_ONE;
        dst->blend.dst = PVR_BLEND_ZERO;
    }
    else
    {
        dst->gen.alpha = PVR_ALPHA_ENABLE;
        dst->blend.src = PVR_BLEND_SRCALPHA;
        dst->blend.dst = PVR_BLEND_INVSRCALPHA;
    }
    dst->blend.src_enable = PVR_BLEND_DISABLE;
    dst->blend.dst_enable = PVR_BLEND_DISABLE;
    dst->gen.fog_type = PVR_FOG_DISABLE;
    dst->gen.color_clamp = PVR_CLRCLAMP_DISABLE;
}

void pvr_sprite_cxt_txr(pvr_sprite_cxt_t *dst, pvr_list_t list,
                        int textureformat, int tw, int th, pvr_ptr_t textureaddr,
                        int filtering)
{
    int alpha;

    memset(dst, 0, sizeof(pvr_sprite_cxt_t));
    dst->list_type = list;
    alpha = list > PVR_LIST_OP_MOD;
    dst->depth.comparison = PVR_DEPTHCMP_GREATER;
    dst->depth.write = PVR_DEPTHWRITE_ENABLE;
    dst->gen.culling = PVR_CULLING_CCW;
    if (!alpha)
    {
        dst->gen.alpha = PVR_ALPHA_DISABLE;
        dst->txr.alpha = PVR_TXRALPHA_ENABLE;
        dst->blend.src = PVR_BLEND_ONE;
        dst->blend.dst = PVR_BLEND_ZERO;
        dst->txr.env = PVR_TXRENV_MODULATE;
    }
    else
    {
        dst->gen.alpha = PVR_ALPHA_ENABLE;
        dst->txr.alpha = PVR_TXRALPHA_ENABLE;
        dst->blend.src = PVR_BLEND_SRCALPHA;
        dst->blend.dst = PVR_BLEND_INVSRCALPHA;
        dst->txr.env = PVR_TXRENV_MODULATEALPHA;
    }
    dst->blend.src_enable = PVR_BLEND_DISABLE;
    dst->blend.dst_enable = PVR_BLEND_DISABLE;
    dst->gen.fog_type = PVR_FOG_DISABLE;
    dst->gen.color_clamp = PVR_CLRCLAMP_DISABLE;
    dst->txr.enable = PVR_TEXTURE_ENABLE;
    dst->txr.uv_flip = PVR_UVFLIP_NONE;
    dst->txr.uv_clamp = PVR_UVCLAMP_NONE;
    dst->txr.filter = filtering;
    dst->txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
    dst->txr.width = tw;
    dst->txr.height = th;
    dst->txr.base = textureaddr;
    dst->txr.format = textureformat;
}

void pvr_sprite_compile(pvr_sprite_hdr_t *dst, const pvr_sprite_cxt_t *src)
{
    uint32_t cmd, mode2, mode3;

    cmd = PVR_CMD_SPRITE
        | FIELD_PREP(PVR_TA_CMD_TXRENABLE, (uint32_t)src->txr.enable)
        | FIELD_PREP(PVR_TA_CMD_TYPE, (uint32_t)src->list_type)
        | FIELD_PREP(PVR_TA_CMD_UVFMT, (uint32_t)PVR_UVFMT_16BIT)
        | FIELD_PREP(PVR_TA_CMD_USERCLIP, (uint32_t)src->gen.clip_mode)
        | FIELD_PREP(PVR_TA_CMD_SPECULAR, (uint32_t)src->gen.specular);
    dst->cmd = cmd;

    dst->mode1 = FIELD_PREP(PVR_TA_PM1_DEPTHCMP, (uint32_t)src->depth.comparison)
        | FIELD_PREP(PVR_TA_PM1_CULLING, (uint32_t)src->gen.culling)
        | FIELD_PREP(PVR_TA_PM1_DEPTHWRITE, (uint32_t)src->depth.write)
        | FIELD_PREP(PVR_TA_PM1_TXRENABLE, (uint32_t)src->txr.enable);

    mode2 = FIELD_PREP(PVR_TA_PM2_SRCBLEND, (uint32_t)src->blend.src)
        | FIELD_PREP(PVR_TA_PM2_DSTBLEND, (uint32_t)src->blend.dst)
        | FIELD_PREP(PVR_TA_PM2_SRCENABLE, (uint32_t)src->blend.src_enable)
        | FIELD_PREP(PVR_TA_PM2_DSTENABLE, (uint32_t)src->blend.dst_enable)
        | FIELD_PREP(PVR_TA_PM2_FOG, (uint32_t)src->gen.fog_type)
        | FIELD_PREP(PVR_TA_PM2_CLAMP, (uint32_t)src->gen.color_clamp)
        | FIELD_PREP(PVR_TA_PM2_ALPHA, (uint32_t)src->gen.alpha);

    if (src->txr.enable == PVR_TEXTURE_DISABLE)
    {
        mode3 = 0;
    }
    else
    {
        mode2 |= FIELD_PREP(PVR_TA_PM2_TXRALPHA, (uint32_t)src->txr.alpha)
            | FIELD_PREP(PVR_TA_PM2_UVFLIP, (uint32_t)src->txr.uv_flip)
            | FIELD_PREP(PVR_TA_PM2_UVCLAMP, (uint32_t)src->txr.uv_clamp)
            | FIELD_PREP(PVR_TA_PM2_FILTER, (uint32_t)src->txr.filter)
            | FIELD_PREP(PVR_TA_PM2_MIPBIAS, (uint32_t)src->txr.mipmap_bias)
            | FIELD_PREP(PVR_TA_PM2_TXRENV, (uint32_t)src->txr.env)
            | FIELD_PREP(PVR_TA_PM2_USIZE, (uint32_t)(__builtin_ctz(src->txr.width) - 3))
            | FIELD_PREP(PVR_TA_PM2_VSIZE, (uint32_t)(__builtin_ctz(src->txr.height) - 3));
        mode3 = FIELD_PREP(PVR_TA_PM3_MIPMAP, (uint32_t)src->txr.mipmap)
            | (uint32_t)src->txr.format
            | ((((uint32_t)(uintptr_t)src->txr.base) & 0x00fffff8) >> 3);
    }
    dst->mode2 = mode2;
    dst->mode3 = mode3;
    /* words 4 and 5 are the sprite's colours, the game's to fill */
    dst->argb = 0xFFFFFFFF;
    dst->oargb = 0;
    dst->mode2_1 = dst->argb;
    dst->mode3_1 = dst->oargb;
}

/* ===================================================================== fog
 * KallistiOS's hardware/pvr/pvr_fog.c ((C)2002 Paul Boese, KOS licence,
 * Platform/xbox/LICENSE.KOS): the table functions, as they write the
 * registers. */

static float sFogAlpha;

static const float kInverseWDepth[] = {
    0.94118f, 0.88889f, 0.84211f, 0.80000f, 0.76190f, 0.72727f, 0.69565f, 0.66667f,
    0.64000f, 0.61538f, 0.59259f, 0.57143f, 0.55172f, 0.53333f, 0.51613f, 0.50000f,
    0.47059f, 0.44444f, 0.42105f, 0.40000f, 0.38095f, 0.36364f, 0.34783f, 0.33333f,
    0.32000f, 0.30769f, 0.29630f, 0.28571f, 0.27586f, 0.26667f, 0.25806f, 0.25000f,
    0.23529f, 0.22222f, 0.21053f, 0.20000f, 0.19048f, 0.18182f, 0.17391f, 0.16667f,
    0.16000f, 0.15385f, 0.14815f, 0.14286f, 0.13793f, 0.13333f, 0.12903f, 0.12500f,
    0.11765f, 0.11111f, 0.10526f, 0.10000f, 0.09524f, 0.09091f, 0.08696f, 0.08333f,
    0.08000f, 0.07692f, 0.07407f, 0.07143f, 0.06897f, 0.06667f, 0.06452f, 0.06250f,
    0.05882f, 0.05556f, 0.05263f, 0.05000f, 0.04762f, 0.04545f, 0.04348f, 0.04167f,
    0.04000f, 0.03846f, 0.03704f, 0.03571f, 0.03448f, 0.03333f, 0.03226f, 0.03125f,
    0.02941f, 0.02778f, 0.02632f, 0.02500f, 0.02381f, 0.02273f, 0.02174f, 0.02083f,
    0.02000f, 0.01923f, 0.01852f, 0.01786f, 0.01724f, 0.01667f, 0.01613f, 0.01562f,
    0.01471f, 0.01389f, 0.01316f, 0.01250f, 0.01190f, 0.01136f, 0.01087f, 0.01042f,
    0.01000f, 0.00962f, 0.00926f, 0.00893f, 0.00862f, 0.00833f, 0.00806f, 0.00781f,
    0.00735f, 0.00694f, 0.00658f, 0.00625f, 0.00595f, 0.00568f, 0.00543f, 0.00521f,
    0.00500f, 0.00481f, 0.00463f, 0.00446f, 0.00431f, 0.00417f, 0.00403f, 0.00391f,
};

/* KOS's float16: an IEEE float as the PVR's 16-bit normalised form */
static uint32_t fog_float16(float f)
{
    union { float f; uint32_t i; } u;
    uint32_t sign, exponent, mantissa;

    u.f = f;
    sign = (u.i >> 16) & 0x8000;
    exponent = (((u.i >> 23) & 0xff) - 127) & 0xff;
    mantissa = (u.i >> 8) & 0x7f00;
    return sign | mantissa | exponent;
}

void pvr_fog_table_color(float a, float r, float g, float b)
{
    PVR_SET(PVR_FOG_TABLE_COLOR, PVR_PACK_COLOR(a, r, g, b));
    sFogAlpha = a;
}

void pvr_fog_vertex_color(float a, float r, float g, float b)
{
    PVR_SET(PVR_FOG_VERTEX_COLOR, PVR_PACK_COLOR(a, r, g, b));
}

void pvr_fog_far_depth(float d)
{
    PVR_SET(PVR_FOG_DENSITY, fog_float16(d > 0.0f ? -d : d));
}

static float fog_neg_exp(float arg)
{
    return expf(-(arg < 0.0f ? -arg : arg));
}

void pvr_fog_table_exp2(float density)
{
    uint32_t i, valh, vall;
    float z = 259.999999f, d2 = density * density;

    pvr_fog_far_depth(259.999999f);
    valh = (uint32_t)(sFogAlpha * 255.0f * (1.0f - fog_neg_exp(-(z * z * d2)))) & 0xff;
    for (i = 0; i < 128; i++)
    {
        z = kInverseWDepth[i] * 259.999999f;
        vall = (uint32_t)(sFogAlpha * 255.0f * (1.0f - fog_neg_exp(-(z * z * d2)))) & 0xff;
        PVR_SET(PVR_FOG_TABLE_BASE + i * 4, ((valh << 8) & 0xff00) + vall);
        valh = vall;
    }
}

void pvr_fog_table_exp(float density)
{
    uint32_t i, valh, vall;
    float z = 259.999999f;

    pvr_fog_far_depth(259.999999f);
    valh = (uint32_t)(sFogAlpha * 255.0f * (1.0f - fog_neg_exp(-(z * density)))) & 0xff;
    for (i = 0; i < 128; i++)
    {
        z = kInverseWDepth[i] * 259.999999f;
        vall = (uint32_t)(sFogAlpha * 255.0f * (1.0f - fog_neg_exp(-(z * density)))) & 0xff;
        PVR_SET(PVR_FOG_TABLE_BASE + i * 4, ((valh << 8) & 0xff00) + vall);
        valh = vall;
    }
}

void pvr_fog_table_linear(float start, float end)
{
    uint32_t i, idx, tdx, valh, vall, table_start, non_zero_entries, step_size;

    start = start < 0.0f ? -start : start;
    end = end < 0.0f ? -end : end;
    if (start >= end)
        return;
    table_start = (uint32_t)((start / end) * 128.0f);
    non_zero_entries = 128 - table_start;
    step_size = 128 / non_zero_entries;
    pvr_fog_far_depth(end);
    valh = (uint32_t)(255.0f * sFogAlpha);
    for (idx = 0, i = 0, tdx = 127; idx < 128; idx++, i += step_size, tdx--)
    {
        if (tdx >= table_start && i < 128)
            vall = (uint32_t)(kInverseWDepth[i] * 255.0f * sFogAlpha);
        else
            vall = 0;
        PVR_SET(PVR_FOG_TABLE_BASE + idx * 4, ((valh << 8) & 0xff00) + vall);
        valh = vall;
    }
}

void pvr_fog_table_custom(float *table)
{
    uint32_t i, valh, vall;

#define CLAMP01(x) ((x) < 0.0f ? 0.0f : (x) > 1.0f ? 1.0f : (x))
    valh = (uint32_t)(sFogAlpha * 255.0f * CLAMP01(table[0])) & 0xff;
    for (i = 1; i <= 128; i++)
    {
        vall = (uint32_t)(sFogAlpha * 255.0f * CLAMP01(table[i])) & 0xff;
        PVR_SET(PVR_FOG_TABLE_BASE + (i - 1) * 4, ((valh << 8) & 0xff00) + vall);
        valh = vall;
    }
#undef CLAMP01
}

/* ================================================= registers and misc */

/* The vertex fog colour changed while a header is in force: the polygons
 * that follow take the new colour, so they get a state of their own (the
 * ones already queued keep theirs). */
static void fog_color_changed(void)
{
    XbState s;

    if (sDirect.state == 0xFFFFFFFF)
        return;
    if (sStates[sDirect.state].fog != PVR_FOG_VERTEX || sStates[sDirect.state].fog_color == sFogVertexColor)
        return;
    s = sStates[sDirect.state];
    s.fog_color = sFogVertexColor;
    sDirect.state = state_add(&s, sDirect.state);
}

void pvr_xbox_reg_write(uint32_t reg, uint32_t value)
{
    if (reg == PVR_PT_ALPHA_REF)
        sPtAlphaRef = value & 0xFF;
    else if (reg == PVR_TEXTURE_MODULO)
        sTxrStride = (value & 0x1F) * 32;
    else if (reg >= PVR_PALETTE_TABLE_BASE && reg < PVR_PALETTE_TABLE_BASE + 4 * 1024)
        pvr_set_pal_entry((reg - PVR_PALETTE_TABLE_BASE) / 4, value);
    else if (reg >= PVR_FOG_TABLE_BASE && reg < PVR_FOG_TABLE_BASE + 4 * 128)
        sFogTable[(reg - PVR_FOG_TABLE_BASE) / 4] = value & 0xFFFF;
    else if (reg == PVR_FOG_VERTEX_COLOR)
    {
        sFogVertexColor = value;
        fog_color_changed();
    }
    else if (reg == PVR_FOG_TABLE_COLOR)
        sFogTableColor = value;
    else if (reg == PVR_FOG_DENSITY)
        fog_density_set(value & 0xFFFF);
    else if (reg == PVR_OBJECT_CLIP)
        sObjectClip = value;
    else if (reg == PVR_SCALER_CFG)
        sScalerCfg = value;     /* the display's scaler: the Xbox scales its own way */
    /* every other register is the TA's or the display's, which the backend
     * does not model */
}

uint32_t pvr_xbox_reg_read(uint32_t reg)
{
    if (reg == PVR_PT_ALPHA_REF)
        return sPtAlphaRef;
    if (reg == PVR_TEXTURE_MODULO)
        return sTxrStride / 32;
    if (reg >= PVR_PALETTE_TABLE_BASE && reg < PVR_PALETTE_TABLE_BASE + 4 * 1024)
        return sPal[(reg - PVR_PALETTE_TABLE_BASE) / 4];
    if (reg >= PVR_FOG_TABLE_BASE && reg < PVR_FOG_TABLE_BASE + 4 * 128)
        return sFogTable[(reg - PVR_FOG_TABLE_BASE) / 4];
    if (reg == PVR_FOG_VERTEX_COLOR)
        return sFogVertexColor;
    if (reg == PVR_FOG_TABLE_COLOR)
        return sFogTableColor;
    if (reg == PVR_FOG_DENSITY)
        return sFogDensity;
    if (reg == PVR_OBJECT_CLIP)
        return sObjectClip;
    if (reg == PVR_SCALER_CFG)
        return sScalerCfg;
    return 0;
}

/* ================================================ the TA input and DR */

/* pvr_dr_target's two store-queue slots */
static uint32_t sDrSlot[2][8] __attribute__((aligned(32)));

void *pvr_xbox_dr_slot(uint32_t which)
{
    return sDrSlot[which & 1];
}

void pvr_xbox_dr_commit(void *addr)
{
    pvr_prim(addr, 32);
}

/* dc/sq.h: blocks for the TA's input go to the emulated TA */
void *sq_fast_cpy(void *dest, const void *src, size_t cnt)
{
    if ((uintptr_t)dest == (uintptr_t)SQ_MASK_DEST(PVR_TA_INPUT))
    {
        pvr_prim(src, cnt * 32);
    }
    else
    {
        static int told;

        if (told++ < 4)
            dbglog(DBG_ERROR, "sq_fast_cpy: store-queue target %08lx is not the TA\n",
                   (unsigned long)(uintptr_t)dest);
    }
    return dest;
}

/* ========================================== the framebuffer, for vram_s */

static uint16_t *sVramMirror;
static uint32_t sVramFrame = 0xFFFFFFFF;

/* A 640x480 RGB565 picture of the last frame shown, made the first time it
 * is asked for in a frame. The one reader (src/i_main.c, the melt and fade
 * wipes) samples every 2nd pixel of every 2nd row, and reads of the display
 * surface are uncached, so only those pixels are converted; the rest of the
 * picture keeps whatever it held. The
 * front buffer is read, not the one being drawn: the frame the Dreamcast's
 * vram_s shows is the last one finished, and reading the surface the GPU
 * is still drawing into would wait for it. Each swap surface is locked
 * once, to learn its CPU address and pitch (Xbox surfaces do not move). */
uint16_t *pvr_xbox_vram_s(void)
{
    static struct { IDirect3DSurface8 *surf; const uint8_t *bits; INT pitch; } map[4];
    IDirect3DSurface8 *front = NULL;
    const uint8_t *src = NULL;
    INT pitch = 0;
    int i, x, y;

    if (sVramMirror == NULL)
    {
        sVramMirror = (uint16_t *)calloc(SCR_W * SCR_H, sizeof(uint16_t));
        if (sVramMirror == NULL)
        {
            static uint16_t dummy[SCR_W * SCR_H];

            sVramMirror = dummy;
        }
    }
    if (!sInited || sVramFrame == sFrame)
        return sVramMirror;
    sVramFrame = sFrame;

    if (FAILED(IDirect3DDevice8_GetBackBuffer(sDev, -1, D3DBACKBUFFER_TYPE_MONO, &front)) || front == NULL)
        return sVramMirror;
    for (i = 0; i < 4; i++)
    {
        if (map[i].surf == front)
        {
            src = map[i].bits;
            pitch = map[i].pitch;
            break;
        }
    }
    if (src == NULL)
    {
        D3DLOCKED_RECT lr;

        if (SUCCEEDED(IDirect3DSurface8_LockRect(front, &lr, NULL, D3DLOCK_READONLY)))
        {
            IDirect3DSurface8_UnlockRect(front);
            for (i = 0; i < 4; i++)
            {
                if (map[i].surf == NULL)
                {
                    map[i].surf = front;
                    map[i].bits = (const uint8_t *)lr.pBits;
                    map[i].pitch = lr.Pitch;
                    break;
                }
            }
            src = (const uint8_t *)lr.pBits;
            pitch = lr.Pitch;
        }
    }
    if (src != NULL)
    {
        /* game pixel (x, y) is display pixel (offX + x*scale, y*scale) */
        for (y = 0; y < SCR_H; y += 2)
        {
            int dy = (int)((float)y * sDispScaleY + sDispOffY);
            const uint32_t *row;

            if (dy >= (int)sDispH)
                break;
            row = (const uint32_t *)(src + (size_t)dy * pitch);
            for (x = 0; x < SCR_W; x += 2)
            {
                int dx = (int)((float)x * sDispScaleX + sDispOffX);
                uint32_t p;

                p = row[dx];
                sVramMirror[y * SCR_W + x] = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) |
                                                         ((p >> 3) & 0x001F));
            }
        }
    }
    IDirect3DSurface8_Release(front);
    return sVramMirror;
}

int pvr_xbox_device_ready(void)
{
    return sInited;
}

void pvr_set_bg_color(float r, float g, float b)
{
    int ir = (int)(255 * r), ig = (int)(255 * g), ib = (int)(255 * b);

    ir = ir < 0 ? 0 : ir > 255 ? 255 : ir;
    ig = ig < 0 ? 0 : ig > 255 ? 255 : ig;
    ib = ib < 0 ? 0 : ib > 255 ? 255 : ib;
    sBgColor = D3DCOLOR_ARGB(255, ir, ig, ib);
}

void pvr_set_zclip(float zc)
{
    sZClip = zc;
}

int pvr_get_vbl_count(void)
{
    return (int)sFrame;
}

int pvr_get_stats(pvr_stats_t *stat)
{
    if (!sInited || stat == NULL)
        return -1;
    memset(stat, 0, sizeof(*stat));
    stat->frame_count = sFrame;
    stat->vbl_count = sFrame;
    stat->vtx_buffer_used = sVertCount * sizeof(XbVert);
    stat->vtx_buffer_used_max = MAX_VERTS * sizeof(XbVert);
    stat->frame_rate = 60.0f;
    stat->enabled_list_mask = (1u << PVR_LIST_OP_POLY) | (1u << PVR_LIST_PT_POLY) |
                              (1u << PVR_LIST_TR_POLY);
    return 0;
}

/* ================================================================ video */

void vid_set_mode(int dm, vid_pixel_mode_t pm)
{
    /* the device is opened by pvr_init at 640x480; see dc/video.h */
    (void)dm;
    (void)pm;
}

void vid_waitvbl(void)
{
    if (sInited)
        D3DDevice_BlockUntilVerticalBlank();
    else
        Sleep(16);
}

void vid_border_color(int r, int g, int b)
{
    /* the border is the overscan around the Dreamcast's picture; the Xbox
     * modes have none to colour */
    (void)r;
    (void)g;
    (void)b;
}

void vid_set_enabled(int val)
{
    sVideoEnabled = val ? 1 : 0;
}

/* The cable the game asks about picks interlaced NTSC or VGA (progressive)
 * for its 640x480 mode; the display mode itself is chosen in pvr_init from
 * the dashboard's settings, so the answer is the one that matches it. */
int vid_check_cable(void)
{
    DWORD vflags = XGetVideoFlags();

    if (XGetAVPack() == XC_AV_PACK_VGA ||
        (vflags & (XC_VIDEO_FLAGS_HDTV_480p | XC_VIDEO_FLAGS_HDTV_720p)))
        return CT_VGA;
    return CT_COMPOSITE;
}


/* KOS fills the framebuffer; the game calls it for I_Error's red screen,
 * then prints over it. Shown at once. */
void vid_clear(int r, int g, int b)
{
    if (!sInited)
        return;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    IDirect3DDevice8_SetRenderTarget(sDev, sBackBuf, sDepth);
    sTargetNow = NULL;
    IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, r, g, b), 0.0f, 0);
    IDirect3DDevice8_Present(sDev, NULL, NULL, NULL, NULL);
}

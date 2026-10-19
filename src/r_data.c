#include "doomdef.h"
#include "r_local.h"
#include "p_local.h"
#include "sheets.h"

#include <math.h>
#include <dc/pvr.h>

pvr_vertex_t thing_verts[3];
pvr_vertex_t line_verts[4];

pvr_poly_hdr_t flush_hdr;
pvr_poly_hdr_t laser_hdr;
pvr_poly_hdr_t line_hdr;
pvr_poly_hdr_t thing_hdr;

static pvr_poly_cxt_t flush_cxt;
static pvr_poly_cxt_t laser_cxt;
static pvr_poly_cxt_t line_cxt;
static pvr_poly_cxt_t thing_cxt;

int firsttex;
int lasttex;
int numtextures;
int firstswx;
int *textures;

int firstsprite;
int lastsprite;
int numsprites;

int skytexture;

void R_InitTextures(void);
void R_InitSprites(void);
void R_InitStatus(void);
void R_InitFont(void);
void R_InitSymbols(void);

/*===========================================================================*/

#define PI_VAL 3.141592653589793

/*
================
=
= R_InitData
=
= Locates all the lumps that will be used by all views
= Must be called after W_Init
=================
*/

void R_InitData(void)
{
	// with single precision float
	// this table is not accurate enough for demos to sync
	// so I generated it offline on PC and put it in tables.c
#if 0
	int i;
	int val = 0;

	for(i = 0; i < (5*FINEANGLES/4); i++)
	{
		finesine[i] = (fixed_t) (sinf((((float) val * (float) PI_VAL) / 8192.0f)) * 65536.0f);
		val += 2;
	}
#endif
	R_InitStatus();
	R_InitFont();
// called earlier elsewhere
//	R_InitSymbols();
	R_InitTextures();
	R_InitSprites();

	pvr_poly_cxt_col(&flush_cxt, PVR_LIST_TR_POLY);
	flush_cxt.blend.src_enable = 1;
	flush_cxt.blend.dst_enable = 0;
	flush_cxt.blend.src = PVR_BLEND_SRCALPHA;
	flush_cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
	pvr_poly_compile(&flush_hdr, &flush_cxt);

	pvr_poly_cxt_col(&laser_cxt, PVR_LIST_OP_POLY);
	pvr_poly_compile(&laser_hdr, &laser_cxt);

	pvr_poly_cxt_col(&thing_cxt, PVR_LIST_OP_POLY);
	pvr_poly_compile(&thing_hdr, &thing_cxt);

	for (int vn = 0; vn < 3; vn++) {
		thing_verts[vn].flags = PVR_CMD_VERTEX;
	}
	thing_verts[2].flags = PVR_CMD_VERTEX_EOL;

	pvr_poly_cxt_col(&line_cxt, PVR_LIST_OP_POLY);
	pvr_poly_compile(&line_hdr, &line_cxt);

	for (int vn = 0; vn < 4; vn++) {
		line_verts[vn].flags = PVR_CMD_VERTEX;
	}
	line_verts[3].flags = PVR_CMD_VERTEX_EOL;
}

/*
==================
=
= R_InitTextures
=
= Initializes the texture list with the textures from the world map
=
==================
*/

pvr_ptr_t *bump_txr_ptr;
pvr_poly_hdr_t **bump_hdrs;

pvr_ptr_t **pvr_texture_ptrs;

pvr_poly_hdr_t **txr_hdr_bump;
pvr_poly_hdr_t **txr_hdr_nobump;

uint16_t tmp_8bpp_pal[256];

pvr_ptr_t pvrstatus;

extern pvr_sprite_hdr_t status_shdr;
extern pvr_sprite_cxt_t status_scxt;
extern pvr_sprite_txr_t status_stxr;

void R_InitStatus(void)
{
	uint16_t *status16;
	status16 = malloc(128 * 16 * sizeof(uint16_t));
	if (!status16)
		I_Error("OOM for STATUS lump texture");

	pvrstatus = pvr_mem_malloc(128 * 16 * 2);
	if (!pvrstatus)
		I_Error("PVR OOM for STATUS lump texture");

	// 1 tile, not compressed
	void *data = (uint8_t *)W_CacheLumpName("STATUS", PU_STATIC, dec_jag);
	int width = (SwapShort(((spriteN64_t *)data)->width) + 7) & ~7;
	int height = SwapShort(((spriteN64_t *)data)->height);
	uint8_t *src = data + sizeof(spriteN64_t);
	uint8_t *offset = src + SwapShort(((spriteN64_t *)data)->cmpsize);
	// palette
	tmp_8bpp_pal[0] = 0;
	short *p = (short *)offset;
	p++;
	for (int j = 1; j < 256; j++) {
		short val = SwapShort(*p++);
		// Unpack and expand to 8bpp, then flip from BGR to RGB.
		uint8_t b = (val & 0x003E) << 2;
		uint8_t g = (val & 0x07C0) >> 3;
		uint8_t r = (val & 0xF800) >> 8;
		uint8_t a = 0xff;
		tmp_8bpp_pal[j] = get_color_argb1555(r, g, b, a);
	}

	int i = 0;
	int x1;
	int x2;

	for (int h = 1; h < 16; h += 2) {
		for (i = 0; i < width / 4; i += 2) {
			int *tmpSrc = (int *)(src + (h * 80));
			x1 = *(int *)(tmpSrc + i);
			x2 = *(int *)(tmpSrc + i + 1);

			*(int *)(tmpSrc + i) = x2;
			*(int *)(tmpSrc + i + 1) = x1;
		}
	}

	for (int h = 0; h < height; h++)
		for (int w = 0; w < width; w++)
			status16[w + (h * 128)] = tmp_8bpp_pal[src[w + (h * width)]];

	pvr_txr_load_ex(status16, pvrstatus, 128, 16, PVR_TXRLOAD_16BPP);

	pvr_sprite_cxt_txr(&status_scxt, PVR_LIST_TR_POLY, D64_TARGB, 128, 16, pvrstatus, PVR_FILTER_NONE);
	pvr_sprite_compile(&status_shdr, &status_scxt);

	Z_Free(data);
	free(status16);
}

extern pvr_ptr_t pvrfont;
extern pvr_sprite_hdr_t font_shdr;
extern pvr_sprite_cxt_t font_scxt;
extern pvr_sprite_txr_t font_stxr;

void R_InitFont(void)
{
	uint8_t *font8;
	uint16_t *font16;
	int fontlump = W_GetNumForName("SFONT");
	pvrfont = pvr_mem_malloc(256 * 16 * 2);
	if (!pvrfont)
		I_Error("PVR OOM for SFONT lump texture");

	void *data = W_CacheLumpNum(fontlump, PU_STATIC, dec_jag);
	int width = SwapShort(((spriteN64_t *)data)->width);
	int height = SwapShort(((spriteN64_t *)data)->height);
	uint8_t *src = data + sizeof(spriteN64_t);
	uint8_t *offset = src + 0x800;

	font16 = (uint16_t *)malloc(256 * 16 * sizeof(uint16_t));
	if (!font16)
		I_Error("OOM for indexed font data");

	// palette
	short *p = (short *)offset;
	tmp_8bpp_pal[0] = 0;
	p++;
	for (int j = 1; j < 16; j++) {
		short val = SwapShort(*p++);
		// Unpack and expand to 8bpp, then flip from BGR to RGB.
		uint8_t b = (val & 0x003E) << 2;
		uint8_t g = (val & 0x07C0) >> 3;
		uint8_t r = (val & 0xF800) >> 8;
		uint8_t a = 0xff;
		tmp_8bpp_pal[j] = get_color_argb1555(r, g, b, a);
	}
	tmp_8bpp_pal[0] = 0;

	int size = (width * height) / 2;

	font8 = src;
	int mask = 32; // 256 / 8;
	// Flip nibbles per byte
	for (int k = 0; k < size; k++) {
		uint8_t tmp = font8[k];
		font8[k] = (tmp >> 4);
		font8[k] |= ((tmp & 0xf) << 4);
	}
	int *font32 = (int *)(src);
	// Flip each sets of dwords based on texture width
	for (int k = 0; k < size / 4; k += 2) {
		int x1;
		int x2;
		if (k & mask) {
			x1 = *(int *)(font32 + k);
			x2 = *(int *)(font32 + k + 1);
			*(int *)(font32 + k) = x2;
			*(int *)(font32 + k + 1) = x1;
		}
	}

	for (int j = 0; j < (width * height); j += 2) {
		uint8_t sps = font8[j >> 1];
		font16[j] = tmp_8bpp_pal[sps & 0xf];
		font16[j + 1] = tmp_8bpp_pal[(sps >> 4) & 0xf];
	}

	pvr_txr_load_ex(font16, pvrfont, 256, 16, PVR_TXRLOAD_16BPP);

	pvr_sprite_cxt_txr(&font_scxt, PVR_LIST_TR_POLY, D64_TARGB, 256, 16, pvrfont, PVR_FILTER_NONE);
	pvr_sprite_compile(&font_shdr, &font_scxt);

	Z_Free(data);
	free(font16);
}

uint16_t *symbols16;
extern pvr_ptr_t pvr_symbols;
int symbols16size = 0;
int rawsymbol_w;
int rawsymbol_h;
float recip_symw;
float recip_symh;

extern pvr_sprite_hdr_t symbols_shdr;
extern pvr_sprite_cxt_t symbols_scxt;
extern pvr_sprite_txr_t symbols_stxr;

void R_InitSymbols(void)
{
	int symbols16_w;
	int symbols16_h;
	void *data;

	ssize_t symbolssize = fs_load("/pc/symbols.raw", &data);
	if (symbolssize == -1) {
		symbolssize = fs_load("/cd/symbols.raw", &data);
		if (symbolssize == -1) {
			// force the video output to do anything
			pvr_scene_begin();
			pvr_list_begin(PVR_LIST_OP_POLY);
			pvr_list_finish();
			pvr_scene_finish();
			// force the video output to do anything
			pvr_scene_begin();
			pvr_list_begin(PVR_LIST_OP_POLY);
			pvr_list_finish();
			pvr_scene_finish();
			// now dbgio_printf to fb should reliably show up even on HDMI-VGA
			I_Error("Cant load from /pc or /cd");
		} else {
			dbgio_printf("using /cd for assets\n");
			fnpre = "/cd";
		}
	} else {
		dbgio_printf("using /pc for assets\n");
		fnpre = "/pc";
	}

	uint8_t *src = data + sizeof(gfxN64_t);

	int width = SwapShort(((gfxN64_t *)data)->width);
	int height = SwapShort(((gfxN64_t *)data)->height);

	// width is 259... which means np2(w) was 512
	// wasting 256x128x2 bytes of vram for nothing since there is nothing in those last 3 columns of pixels
	// force it to 256 wide
	// verified in-engine this looks ok
	symbols16_w = 256;//np2(width);
	symbols16_h = np2(height);
	symbols16size = (symbols16_w * symbols16_h * 2);

	recip_symw = 1.0f / (float)symbols16_w;
	recip_symh = 1.0f / (float)symbols16_h;

	rawsymbol_w = width;
	rawsymbol_h = height;

	pvr_symbols = pvr_mem_malloc(symbols16_w * symbols16_h * 2);
	if (!pvr_symbols)
		I_Error("PVR OOM for SYMBOLS lump texture");

	symbols16 = (uint16_t *)malloc(symbols16size);
	if (!symbols16)
		I_Error("OOM for STATUS lump texture");

	// Load Palette Data
	int offset = SwapShort(((gfxN64_t *)data)->width) * SwapShort(((gfxN64_t *)data)->height);
	offset = (offset + 7) & ~7;
	// palette
	short *p = data + offset + sizeof(gfxN64_t);
	for (int j = 0; j < 256; j++) {
		short val = SwapShort(*p++);
		// Unpack and expand to 8bpp, then flip from BGR to RGB.
		uint8_t b = (val & 0x003E) << 2;
		uint8_t g = (val & 0x07C0) >> 3;
		uint8_t r = (val & 0xF800) >> 8;
		uint8_t a = (val & 1);
		tmp_8bpp_pal[j] = get_color_argb1555(r, g, b, a);
	}
	tmp_8bpp_pal[0] = 0;

	for (int h = 0; h < height; h++)
		for (int w = 0; w < symbols16_w; w++)
			symbols16[w + (h * symbols16_w)] = tmp_8bpp_pal[src[w + (h * width)]];

	pvr_txr_load_ex(symbols16, pvr_symbols, symbols16_w, symbols16_h, PVR_TXRLOAD_16BPP);

	free(symbols16);

	pvr_sprite_cxt_txr(&symbols_scxt, PVR_LIST_TR_POLY, D64_TARGB, symbols16_w, symbols16_h, pvr_symbols, PVR_FILTER_NONE);
	pvr_sprite_compile(&symbols_shdr, &symbols_scxt);
}

extern int lump_frame[575 + 310];
extern int used_lumps[575 + 310];

void R_InitTextures(void)
{
	int swx, i;

	firsttex = W_GetNumForName("T_START") + 1;
	lasttex = W_GetNumForName("T_END") - 1;

	numtextures = (lasttex - firsttex) + 1;

	pvr_texture_ptrs = (pvr_ptr_t **)malloc(numtextures * sizeof(pvr_ptr_t *));
	if (pvr_texture_ptrs == NULL)
		I_Error("failed malloc pvr_texture_ptrs");

	txr_hdr_bump = (pvr_poly_hdr_t **)malloc(numtextures * sizeof(pvr_poly_hdr_t *));
	if (!txr_hdr_bump)
		I_Error("could not malloc txr_hdr_bump* array");

	txr_hdr_nobump = (pvr_poly_hdr_t **)malloc(numtextures * sizeof(pvr_poly_hdr_t *));
	if (!txr_hdr_nobump)
		I_Error("could not malloc txr_hdr_nobump* array");

	memset(used_lumps, 0xff, sizeof(int) * ALL_SPRITES_COUNT);
	memset(lump_frame, 0xff, sizeof(int) * ALL_SPRITES_COUNT);

	memset(pvr_texture_ptrs, 0, sizeof(pvr_ptr_t *) * numtextures);

	memset(txr_hdr_bump, 0, sizeof(pvr_poly_hdr_t *) * numtextures);
	memset(txr_hdr_nobump, 0, sizeof(pvr_poly_hdr_t *) * numtextures);

	bump_txr_ptr = (pvr_ptr_t *)malloc(numtextures * sizeof(pvr_ptr_t));
	if (bump_txr_ptr == NULL)
		I_Error("failed malloc bump_txr_ptr");

	bump_hdrs = (pvr_poly_hdr_t **)malloc(numtextures * sizeof(pvr_poly_hdr_t*));
	if (!bump_hdrs)
		I_Error("could not malloc bump_hdr array");

	memset(bump_txr_ptr, 0, sizeof(pvr_ptr_t) * numtextures);
	memset(bump_hdrs, 0, sizeof(pvr_poly_hdr_t*) * numtextures);

	textures = Z_Malloc(numtextures * sizeof(int), PU_STATIC, NULL);

	for (i = 0; i < numtextures; i++)
		textures[i] = (i + firsttex) << 4;

	swx = 1300; //W_CheckNumForName("SWX", 0x7fffff00, 0);
	firstswx = (swx - firsttex);
}

/*
================
=
= R_InitSprites
=
=================
*/

void R_InitSprites(void)
{
	firstsprite = W_GetNumForName("S_START") + 1;
	lastsprite = W_GetNumForName("S_END") - 1;
	numsprites = (lastsprite - firstsprite) + 1;

	setup_sprite_headers();
}

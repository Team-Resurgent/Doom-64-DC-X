//Renderer phase 2 - Sky Rendering Routines

#include "doomdef.h"
#include "r_local.h"

#define FIRESKY_WIDTH 64
#define FIRESKY_HEIGHT 64

extern pvr_dr_state_t dr_state;

typedef enum {
	SKF_CLOUD = 1,
	SKF_THUNDER = 2,
	SKF_MOUNTAIN = 4,
} skyflags_e;

fixed_t FogNear;
int FogColor;
skyfunc_t R_RenderSKY;
// Fire data double buffer
uint8_t *SkyFireData[2];
uint8_t *SkyCloudData;
int Skyfadeback;
int FireSide;

int ThunderCounter;
int LightningCounter;

float f_ThunderCounter;
float f_LightningCounter;

int SkyPicSpace;
int SkyPicMount;
int SkyCloudColor;
//int SkyVoidColor;
int SkyFlags;

static pvr_vertex_t skypic_verts[4];

void R_RenderSpaceSky(void);
void R_RenderCloudSky(void);
void R_RenderVoidSky(void);
void R_RenderEvilSky(void);
void R_RenderClouds(void);
void R_RenderSkyPic(int lump, int yoffset, int callno);
void R_RenderFireSky(void);
void R_CloudThunder(void);
void R_RenderDoomE1Sky(void);

static uint32_t FireSkyColor1;
static uint32_t FireSkyColor2;

static uint32_t skycloudv0col;
static uint32_t skycloudv2col;

static pvr_ptr_t pvrcloud;
static pvr_poly_hdr_t pvrcloudhdr;
static pvr_poly_cxt_t pvrcloudcxt;

static pvr_ptr_t pvrfire;
static pvr_poly_hdr_t pvrfirehdr;
static pvr_poly_cxt_t pvrfirecxt;

static int CloudOffsetX, CloudOffsetY;

extern uint16_t bgpal[256];
extern uint16_t biggest_bg[512 * 256];

static pvr_poly_hdr_t pvrskyhdr[2];
static pvr_poly_cxt_t pvrskycxt;
pvr_ptr_t pvrsky[2];
int lastlump[2] = { -1, -1 };

char remap_table[256];

// https://stackoverflow.com/a/9085524
static float ColorDistance(int c1r, int c1g, int c1b, int c2r, int c2g, int c2b)
{
	float rmean = ((float)c1r + (float)c2r) * 0.5f;
	float r = fabsf((float)c1r - (float)c2r);
	float g = fabsf((float)c1g - (float)c2g);
	float b = fabsf((float)c1b - (float)c2b);
	return sqrtf(
		(((512 + rmean) * r * r) * 0.00390625f)
		+ (4 * g * g)
		+ (((767 - rmean) * b * b) * 0.00390625f) );
}

// this does a much better job than GIMP version of the art pipeline
void FindNearestColor(int oi, int r, int g, int b, int palette[256][3]) {
	int i, bestIndex = 0;
	float distanceSquared;
	float minDistanceSquared = 1e20f;

	if (oi == 0 && r == 0 && g == 0 && b == 0) {
		remap_table[oi] = 0;
		return;
	}

	for (i=0; i< 256; i++) {
		distanceSquared = ColorDistance(r, g, b, palette[i][0], palette[i][1], palette[i][2]);
		if (distanceSquared < minDistanceSquared) {
			minDistanceSquared = distanceSquared;
			bestIndex = i;
		}
	}
	
	remap_table[oi] = bestIndex;
}

extern int D64NONENEMY[256][3];

void R_SetupSky(void)
{
	uint8_t *raw_fire_data;

	if (!pvrfire) {
		pvrfire = pvr_mem_malloc(64 * 64);
		if (!pvrfire)
			I_Error("PVR OOM for fire texture");

		pvr_poly_cxt_txr(&pvrfirecxt, PVR_LIST_OP_POLY, D64_TPAL(PAL_I8), 64, 64, pvrfire, PVR_FILTER_BILINEAR);

		pvrfirecxt.depth.write = PVR_DEPTHWRITE_DISABLE;

		pvr_poly_compile(&pvrfirehdr, &pvrfirecxt);
	}

	if (!pvrcloud) {
		pvrcloud = pvr_mem_malloc(64 * 64);
		if (!pvrcloud)
			I_Error("PVR OOM for cloud texture");

		pvr_poly_cxt_txr(&pvrcloudcxt, PVR_LIST_OP_POLY, D64_TPAL(PAL_I8), 64, 64, pvrcloud, PVR_FILTER_BILINEAR);

		pvrcloudcxt.gen.specular = PVR_SPECULAR_ENABLE;
		pvrcloudcxt.depth.write = PVR_DEPTHWRITE_DISABLE;

		pvr_poly_compile(&pvrcloudhdr, &pvrcloudcxt);

		uint8_t *dccloud = malloc(64 * 64);
		if (!dccloud)
			I_Error("OOM for raw cloud data");

		SkyCloudData = (uint8_t *)W_CacheLumpName("CLOUD", PU_STATIC, dec_jag);
		memcpy(dccloud, SkyCloudData + 8, 4096);
		int *tmpSrc = (int *)dccloud;
		int i = 0;
		int mask = 64 / 4;
		// Flip each sets of dwords based on texture width
		for (i = 0; i < 4096 / 4; i += 2) {
			int x1;
			int x2;
			if (i & mask) {
				x1 = *(int *)(tmpSrc + i);
				x2 = *(int *)(tmpSrc + i + 1);
				*(int *)(tmpSrc + i) = x2;
				*(int *)(tmpSrc + i + 1) = x1;
			}
		}
		Z_Free(SkyCloudData);
		pvr_txr_load_ex(dccloud, pvrcloud, 64, 64, PVR_TXRLOAD_8BPP);
		free(dccloud);
	}

	FogNear = 985;
	FogColor = PACKRGBA(0, 0, 0, 0);
	R_RenderSKY = NULL;
	SkyFlags = 0;
	CloudOffsetX = 0;
	CloudOffsetY = 0;
	ThunderCounter = 180;
	LightningCounter = 0;

	f_ThunderCounter = 180;
	f_LightningCounter = 0;

	FireSide = 0;
	Skyfadeback = 0;
	SkyPicSpace = W_GetNumForName("SPACE");
	pvr_set_bg_color(0, 0, 0);

	switch (skytexture) {
	case 1:
	case 10:
		SkyFlags = (SKF_CLOUD | SKF_THUNDER);
		R_RenderSKY = R_RenderCloudSky;

		SkyCloudColor = PACKRGBA(176, 128, 255, 255);

		if (skytexture == 10) {
			SkyPicMount = W_GetNumForName("MOUNTC");
			SkyFlags |= SKF_MOUNTAIN;
		}

		skycloudv0col = PACKRGBA(0, 0, 0, 255);
		skycloudv2col = PACKRGBA(0, 0, 21, 255);
		break;

	case 2:
		SkyFlags = SKF_CLOUD;
		R_RenderSKY = R_RenderCloudSky;

		SkyCloudColor = PACKRGBA(255, 48, 48, 255);

		skycloudv0col = PACKRGBA(16, 0, 0, 255);
		skycloudv2col = PACKRGBA(16, 0, 0, 255);
		break;

	case 3:
	case 5:
		SkyFlags = SKF_CLOUD;
		R_RenderSKY = R_RenderCloudSky;

		SkyCloudColor = PACKRGBA(208, 112, 64, 255);

		if (skytexture == 3) {
			SkyPicMount = W_GetNumForName("MOUNTB");
			SkyFlags |= SKF_MOUNTAIN;
		} else {
			FogNear = 975;
			FogColor = PACKRGBA(48, 16, 8, 0);
		}

		skycloudv0col = PACKRGBA(0, 0, 0, 255);
		skycloudv2col = PACKRGBA(64, 16, 0, 255);
		break;

	case 4:
	case 9:
		R_RenderSKY = R_RenderFireSky;

		raw_fire_data = W_CacheLumpName("FIRE", PU_LEVEL, dec_jag);
		SkyFireData[0] = (raw_fire_data + 8);
		SkyFireData[1] = Z_Malloc((FIRESKY_WIDTH * FIRESKY_HEIGHT), PU_LEVEL, NULL);

		memcpy(SkyFireData[1], SkyFireData[0], (FIRESKY_WIDTH * FIRESKY_HEIGHT));

		if (skytexture == 4) {
			FireSkyColor1 = PACKRGBA(255, 0, 0, 255);
			FireSkyColor2 = PACKRGBA(255, 127, 0, 255);//96, 0, 255);
		} else {
			FireSkyColor1 = PACKRGBA(0, 255, 0, 255);
			FireSkyColor2 = PACKRGBA(/* 112 */127, /* 112 */127, 0, 255);
		}
		break;

	case 6:
		R_RenderSKY = R_RenderSpaceSky;
		break;

	case 7:
		FogNear = 995;
		R_RenderSKY = R_RenderEvilSky;
		break;

	case 8:
		R_RenderSKY = R_RenderVoidSky;
		FogNear = 975;
		FogColor = PACKRGBA(0, 64, 64, 0);
		//SkyVoidColor = PACKRGBA(0, 56, 56, 0);
		break;

	case 11:
		R_RenderSKY = R_RenderSpaceSky;
		SkyPicMount = W_GetNumForName("MOUNTA");
		SkyFlags |= SKF_MOUNTAIN;
		break;
	}

	if (gamemap > 40) {
		R_RenderSKY = R_RenderDoomE1Sky;
		SkyCloudColor = 0x24242480;
		FogNear = 990;
		FogColor = 0x262626ff;
		skycloudv2col = 0x50505080;
		skycloudv0col = 0x00000080;
	}
}

void R_RenderSpaceSky(void)
{
	R_RenderSkyPic(SkyPicSpace, 128, 0);

	if (SkyFlags & SKF_MOUNTAIN)
		R_RenderSkyPic(SkyPicMount, 170, 1);
}

void R_RenderCloudSky(void)
{
	if (SkyFlags & SKF_CLOUD)
		R_RenderClouds();

	if (SkyFlags & SKF_THUNDER)
		R_CloudThunder();

	if (SkyFlags & SKF_MOUNTAIN)
		R_RenderSkyPic(SkyPicMount, 170, 0);
}

void R_RenderVoidSky(void)
{
	pvr_set_bg_color(0.0f, 0.22f, 0.22f);
}

void R_RenderEvilSky(void)
{
	int color = 0;

	if (Skyfadeback) {
		Skyfadeback += 4;

		if (Skyfadeback > 255)
			Skyfadeback = 255;

		if (Skyfadeback > 128)
			color = 128;
		else
			color = Skyfadeback;
	}

	if (Skyfadeback < 255) {
		R_RenderSkyPic(SkyPicSpace, 128, 0);
		R_RenderSkyPic(SkyPicSpace, 240, 1);
	}

	if (Skyfadeback)
		M_DrawBackground(EVIL, color);
}


void R_RenderClouds(void)
{
	// implementation borrowed from Doom 64 EX
	float pos = TRUEANGLES(viewangle) * 0.00555555555555555555555555555556f;
	//(TRUEANGLES(viewangle) / 360.0f) * 2.0f;
	float u0, v0, u1, v1;

	if (!gamepaused) {
		CloudOffsetX -= ((finecosine[viewangle >> ANGLETOFINESHIFT] >> 10) * (f_vblsinframe[0]*0.5f));
		CloudOffsetX &= 65535;
		CloudOffsetY += ((finesine[viewangle >> ANGLETOFINESHIFT] >> 9) * (f_vblsinframe[0]*0.5f));
		CloudOffsetY &= 65535;
	}

	u0 = ((float)CloudOffsetX * recip64k) - pos;
	u1 = (((float)CloudOffsetX * recip64k) + 1.5f) - pos;
	v0 = ((float)CloudOffsetY * recip64k);
	v1 = ((float)CloudOffsetY * recip64k) + 2.0f;

	// transformed/screen projected coords
	skypic_verts[0].flags = PVR_CMD_VERTEX;
	skypic_verts[0].x = 920.000000;
	skypic_verts[0].y = 0.000000;
	skypic_verts[0].z = 0.006250;
	skypic_verts[0].u = u1;
	skypic_verts[0].v = v0;
	skypic_verts[0].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	skypic_verts[0].oargb = D64_PVR_REPACK_COLOR(skycloudv0col);

	skypic_verts[1].flags = PVR_CMD_VERTEX;
	skypic_verts[1].x = 640.000000;
	skypic_verts[1].y = 240.000000;
	skypic_verts[1].z = 0.003333;
	skypic_verts[1].u = u1;
	skypic_verts[1].v = v1;
	skypic_verts[1].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	skypic_verts[1].oargb = D64_PVR_REPACK_COLOR(skycloudv2col);

	skypic_verts[2].flags = PVR_CMD_VERTEX;
	skypic_verts[2].x = -280.000000;
	skypic_verts[2].y = 0.000000;
	skypic_verts[2].z = 0.006250;
	skypic_verts[2].u = u0;
	skypic_verts[2].v = v0;
	skypic_verts[2].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	skypic_verts[2].oargb = D64_PVR_REPACK_COLOR(skycloudv0col);

	skypic_verts[3].flags = PVR_CMD_VERTEX_EOL;
	skypic_verts[3].x = 0.000000;
	skypic_verts[3].y = 240.000000;
	skypic_verts[3].z = 0.003333;
	skypic_verts[3].u = u0;
	skypic_verts[3].v = v1;
	skypic_verts[3].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	skypic_verts[3].oargb = D64_PVR_REPACK_COLOR(skycloudv2col);

	// equivalent to above commented-out Direct Rendering API use
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &pvrcloudhdr, 1);
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), skypic_verts, 4);

	global_render_state.context_change = 1;
}

void R_RenderDoomE1Sky(void) {
	uint8_t *data;
	uint8_t *src;
	uint8_t *paldata;

	int width = 256;
	int height = 128;

	int yl;
	int ang;

	R_RenderClouds();

	if (lastlump[0] != MAXINT) {
		if (pvrsky[0]) {
			pvr_mem_free(pvrsky[0]);
			pvrsky[0] = 0;
		}

		pvrsky[0] = pvr_mem_malloc(256 * 128);
		if (!pvrsky[0])
			I_Error("PVR OOM for Doom E1 sky texture");

		pvr_poly_cxt_txr(&pvrskycxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 256, 128, pvrsky[0], PVR_FILTER_BILINEAR);

		pvrskycxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		pvrskycxt.txr.uv_flip = PVR_UVFLIP_NONE;

		pvr_poly_compile(&pvrskyhdr[0], &pvrskycxt);

		data = Z_Alloc(33296, PU_STATIC, NULL);
		sprintf(fnbuf, "%s/doom1mn.lmp", fnpre);
		file_t doom1mn = fs_open(fnbuf, O_RDONLY);
		ssize_t total, left, r;
		uint8_t *out;

		if (doom1mn == FILEHND_INVALID)
			I_Error("cant open doom1mn for reading");

		left = fs_total(doom1mn);
		total = 0;
		out = (uint8 *)data;

		/* Load the data */
		while (left > 0) {
			r = fs_read(doom1mn, out, left);
			if (r <= 0)
				break;
			left -= r;
			total += r;
			out += r;
		}
		fs_close(doom1mn);
		lastlump[0] = MAXINT;
		src = data + sizeof(spriteN64_t);
		paldata = (src + (width * height));
		short *palsrc = (short *)paldata;
		for (unsigned j = 0; j < 256; j++) {
			short val = SwapShort(*palsrc++);
			// Unpack and expand to 8bpp, then flip from BGR to RGB.
			uint8_t b = (val & 0x003E) << 2;
			uint8_t g = (val & 0x07C0) >> 3;
			uint8_t r = (val & 0xF800) >> 8;
			FindNearestColor(j, r, g, b, D64NONENEMY);
			remap_table[49] = 0;
		}
		// ?
		bgpal[0] = 0;
		int *tmpSrc = (int *)src;
		int i = 0;
		int mask = 256 / 4;
		// Flip each sets of dwords based on texture width
		for (i = 0; i < (256 * height) / 4; i += 2) {
			int x1;
			int x2;
			if (i & mask) {
				x1 = *(int *)(tmpSrc + i);
				x2 = *(int *)(tmpSrc + i + 1);
				*(int *)(tmpSrc + i) = x2;
				*(int *)(tmpSrc + i + 1) = x1;
			}
		}

		for (int h = 0; h < height; h++)
			for (int w = 0; w < 256; w++)
				src[w + (h * 256)] = remap_table[src[w + (h * 256)]];

		Z_Free(data);
		pvr_txr_load_ex(src, pvrsky[0], 256, 128, PVR_TXRLOAD_8BPP);
	}

	ang = 0 - ((viewangle >> 22) & 255);
	float u0, v0, u1, v1;
	u0 = (float)ang * recip256;
	u1 = u0 + 1.0f;
	v0 = recip256; // 0.5f / 128.0f;
	v1 = 1.0f;
	yl = (176 - height);

	for (int vn = 0; vn < 4; vn++) {
		skypic_verts[vn].flags = PVR_CMD_VERTEX;
		skypic_verts[vn].z = 0.00014f;
		skypic_verts[vn].argb = 0xff808080;
		skypic_verts[vn].oargb = 0;
	}
	skypic_verts[3].flags = PVR_CMD_VERTEX_EOL;

	pvr_vertex_t *vert = skypic_verts;

	vert->x = 0.0f;
	vert->y = (float)(yl + height) * (float)RES_RATIO;
	vert->u = u0;
	vert->v = v1;
	vert++;

	vert->x = 0.0f;
	vert->y = (float)yl * (float)RES_RATIO;
	vert->u = u0;
	vert->v = v0;
	vert++;

	vert->x = 640.0f;
	vert->y = (float)(yl + height) * (float)RES_RATIO;
	vert->u = u1;
	vert->v = v1;
	vert++;

	vert->x = 640.0f;
	vert->y = (float)yl * (float)RES_RATIO;
	vert->u = u1;
	vert->v = v0;

	pvr_list_prim(PVR_LIST_TR_POLY, &pvrskyhdr[0], sizeof(pvr_poly_hdr_t));
	pvr_list_prim(PVR_LIST_TR_POLY, &skypic_verts, sizeof(skypic_verts));

	global_render_state.context_change = 1;
}

void *datap[2] = {0,0};
int sws[2] = {-1,-1};
int shs[2] = {-1,-1};

void R_RenderSkyPic(int lump, int yoffset, int callno) // 80025BDC
{
	uint8_t *data = 0;
	uint8_t *src = 0;
	uint8_t *paldata = 0;

	int width;
	int height;

	int yl;
	int ang;
	if (lump != lastlump[callno]) {
		data = W_CacheLumpNum(lump, PU_STATIC, dec_jag);
		width = (SwapShort(((spriteN64_t *)data)->width) + 7) & ~7;
		height = SwapShort(((spriteN64_t *)data)->height);
		sws[callno] = width;
		shs[callno] = height;

		if (!pvrsky[callno]) {
			pvrsky[callno] = pvr_mem_malloc(256 * 128);
			if (!pvrsky[callno])
				I_Error("PVR OOM for sky texture %d [%d]", lump, callno);

			pvr_poly_cxt_txr(&pvrskycxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 256, 128, pvrsky[callno], PVR_FILTER_BILINEAR);

			pvrskycxt.depth.write = PVR_DEPTHWRITE_DISABLE;
			pvrskycxt.txr.uv_flip = PVR_UVFLIP_NONE;

			pvr_poly_compile(&pvrskyhdr[callno], &pvrskycxt);
		}

		src = data + sizeof(spriteN64_t);
		paldata = (src + (width * height));
		short *palsrc = (short *)paldata;

		lastlump[callno] = lump;
		for (int j = 0; j < 256; j++) {
			short val = SwapShort(*palsrc++);
			// Unpack and expand to 8bpp, then flip from BGR to RGB.
			uint8_t b = (val & 0x003E) << 2;
			uint8_t g = (val & 0x07C0) >> 3;
			uint8_t r = (val & 0xF800) >> 8;
			FindNearestColor(j, r, g, b, D64NONENEMY);
		}

		int *tmpSrc = (int *)src;
		int i = 0;
		int mask = 256 / 4;
		// Flip each sets of dwords based on texture width
		for (i = 0; i < (256 * height) / 4; i += 2) {
			int x1;
			int x2;
			if (i & mask) {
				x1 = *(int *)(tmpSrc + i);
				x2 = *(int *)(tmpSrc + i + 1);
				*(int *)(tmpSrc + i) = x2;
				*(int *)(tmpSrc + i + 1) = x1;
			}
		}
		for (int h = 0; h < height; h++)
			for (int w = 0; w < 256; w++)
				src[w + (h * 256)] = remap_table[src[w + (h * 256)]];

		pvr_txr_load_ex(src, pvrsky[callno], 256, 128, PVR_TXRLOAD_8BPP);
		Z_Free(data);
	} else {
		width = sws[callno];
		height = shs[callno];
	}

	float u0, v0, u1, v1;
	ang = 0 - ((viewangle >> 22) & 255);
	u0 = (float)ang * recip256;
	u1 = u0 + 1.0f;
	v0 = recip256;
	v1 = (float)height * recip128;

	yl = (yoffset - height);

	for (int vn = 0; vn < 4; vn++) {
		skypic_verts[vn].flags = PVR_CMD_VERTEX;
		skypic_verts[vn].z = (0.00014f + (callno ? 0.000005 : 0.0));
		skypic_verts[vn].argb = 0xffffffff;
		skypic_verts[vn].oargb = 0;
	}
	skypic_verts[3].flags = PVR_CMD_VERTEX_EOL;

	pvr_vertex_t *vert = skypic_verts;

	vert->x = 0.0f;
	vert->y = (float)(yl + height) * (float)RES_RATIO;
	vert->u = u0;
	vert->v = v1;
	vert++;

	vert->x = 0.0f;
	vert->y = (float)yl * (float)RES_RATIO;
	vert->u = u0;
	vert->v = v0;
	vert++;

	vert->x = 640.0f;
	vert->y = (float)(yl + height) * (float)RES_RATIO;
	vert->u = u1;
	vert->v = v1;
	vert++;

	vert->x = 640.0f;
	vert->y = (float)yl * (float)RES_RATIO;
	vert->u = u1;
	vert->v = v0;

	pvr_list_prim(PVR_LIST_TR_POLY, &pvrskyhdr[callno], sizeof(pvr_poly_hdr_t));
	pvr_list_prim(PVR_LIST_TR_POLY, &skypic_verts, sizeof(skypic_verts));

	global_render_state.context_change = 1;
}

// Doom 64 Ultra
// https://github.com/Immorpher/doom64ultra/blob/f1cddbce4b1d24255606472de2e77fa18494032b/src/r_phase2.c
static int R_SpreadFire(uint8_t *src, uint8_t *srcoffset, int x, int rand)
{
	int pixel;
	int randIdx;
	uint8_t *tmpSrc;

	pixel = *srcoffset;
	if (pixel != 0) {
		randIdx = rndtable[rand];
		rand = (rand + 2) & 0xff;

		tmpSrc = &src[(x - (randIdx & 3) + 1) & (FIRESKY_WIDTH-1)];
		*(tmpSrc - FIRESKY_WIDTH) = pixel - ((randIdx & 1) << 4);
	} else
		*(srcoffset - FIRESKY_WIDTH) = 0;

	return rand;
}

void R_RenderFireSky(void)
{
	static int last_f_gametic = 0;
	uint8_t *buff;
	uint8_t *src;
	uint8_t *srcoffset;
	int x;
	int y;
	int rand;
	int ang;

	if (((int)f_gametic > (int)f_gamevbls) && (((int)f_gametic) & 1) && (!gamepaused) && (last_f_gametic != (int)f_gametic)) {
		last_f_gametic = (int)f_gametic;
		buff = SkyFireData[FireSide];
		memcpy(buff, SkyFireData[FireSide ^ 1], (FIRESKY_WIDTH * FIRESKY_HEIGHT));

		rand = (M_Random() & 0xff);
		src = (buff + FIRESKY_WIDTH);

		for (x = 0; x < FIRESKY_WIDTH; x++) {
			srcoffset = (src + x);

			for (y = 1; y < FIRESKY_HEIGHT; y++) {
				rand = R_SpreadFire(src, srcoffset, x, rand);

				src += FIRESKY_WIDTH;
				srcoffset += FIRESKY_WIDTH;
			}

			src -= ((FIRESKY_WIDTH*FIRESKY_HEIGHT) - FIRESKY_WIDTH);
		}

		FireSide ^= 1;
	} else
		buff = SkyFireData[FireSide ^ 1];

	pvr_txr_load_ex(buff, pvrfire, 64, 64, PVR_TXRLOAD_8BPP);

	ang = 0 - ((viewangle >> 22) & 255);
	float u0 = (float)ang * recip256;
	float u1 = u0 + 5.0f;
	float v0 = 0;
	float v1 = 1.0f;

	// transformed/screen projected coords
	skypic_verts[0].flags = PVR_CMD_VERTEX;
	skypic_verts[0].x = 0.0f;
	skypic_verts[0].y = 240.0f;
	skypic_verts[0].z = 0.00625f;
	skypic_verts[0].u = u0;
	skypic_verts[0].v = v1;
	skypic_verts[0].argb = D64_PVR_REPACK_COLOR(FireSkyColor2);

	skypic_verts[1].flags = PVR_CMD_VERTEX;
	skypic_verts[1].x = 0.0f;
	skypic_verts[1].y = 0.0f;
	skypic_verts[1].z = 0.00625f;
	skypic_verts[1].u = u0;
	skypic_verts[1].v = v0;
	skypic_verts[1].argb = D64_PVR_REPACK_COLOR(FireSkyColor1);

	skypic_verts[2].flags = PVR_CMD_VERTEX;
	skypic_verts[2].x = 640.0f;
	skypic_verts[2].y = 240.0f;
	skypic_verts[2].z = 0.00625f;
	skypic_verts[2].u = u1;
	skypic_verts[2].v = v1;
	skypic_verts[2].argb = D64_PVR_REPACK_COLOR(FireSkyColor2);

	skypic_verts[3].flags = PVR_CMD_VERTEX_EOL;
	skypic_verts[3].x = 640.0f;
	skypic_verts[3].y = 0.0f;
	skypic_verts[3].z = 0.00625f;
	skypic_verts[3].u = u1;
	skypic_verts[3].v = v0;
	skypic_verts[3].argb = D64_PVR_REPACK_COLOR(FireSkyColor1);

	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &pvrfirehdr, 1);
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), skypic_verts, 4);

	global_render_state.context_change = 1;
}

int add_lightning = 0;

void R_CloudThunder(void)
{
	int rand;
	if (!(gamepaused)) {
		f_ThunderCounter -= f_vblsinframe[0];

		if (f_ThunderCounter <= 0.0) {
			if (f_LightningCounter == 0) {
				S_StartSound(NULL, sfx_thndrlow + (M_Random() & 1));

				if (menu_settings.Rumble)
					I_Rumble(rumble_patterns[rumble_thunder]);
			} else if (!(f_LightningCounter < 6)) {
				// Reset loop after 6 lightning flickers
				rand = (M_Random() & 7);
				f_ThunderCounter = (float)(((((rand << 4) - rand) << 2) + 60) << 1);
				f_LightningCounter = 0;
				return;
			}

			if (!add_lightning && ((int)f_LightningCounter & 1) == 0) {
				skycloudv0col += 0x11111100;
				skycloudv2col += 0x11111100;
				add_lightning = 1;
			} else if (add_lightning && ((int)f_LightningCounter & 1) != 0) {
				skycloudv0col -= 0x11111100;
				skycloudv2col -= 0x11111100;
				add_lightning = 0;
			}

			// Do short delay loops for lightning flickers
			f_ThunderCounter = (float)(((M_Random() & 7) + 1) << 1); 
			f_LightningCounter += 1.0f;
		}
	}
}

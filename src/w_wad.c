/* W_wad.c */

#include "doomdef.h"
#include "st_main.h"
#include "palettes.h"
#include "md5.h"

#include "hash.h"

#include <errno.h>

/*=============== */
/*   TYPES */
/*=============== */

typedef struct {
	char identification[4]; /* should be IWAD */
	int numlumps;
	int infotableofs;
	int pad;
} wadinfo_t;

typedef struct {
	char identification[4]; /* should be IWAD */
	int numlumps;
	int infotableofs;
} bumpwadinfo_t;

/*============= */
/* GLOBALS */
/*============= */

// see doomdef.h
// anywhere that loads file from "disc" uses these two vars
char *fnpre = STORAGE_PREFIX;
char  __attribute__((aligned(32))) fnbuf[256];

pvr_ptr_t pvr_non_enemy;

pvr_poly_hdr_t pvr_sprite_hdr;
pvr_poly_hdr_t pvr_sprite_hdr_nofilter;

pvr_poly_hdr_t pvr_sprite_hdr_bump;
pvr_poly_hdr_t pvr_sprite_hdr_nofilter_bump;

pvr_ptr_t wepnbump_txr = 0;
pvr_poly_hdr_t wepnbump_hdr;

pvr_ptr_t wepndecs_txr;
pvr_poly_hdr_t wepndecs_hdr;
pvr_poly_hdr_t wepndecs_hdr_nofilter;

// updated when additional content present
int extra_episodes = 0;
// only built-in additional content present
int kneedeep_only = 0;

// verification that the lost levels are present and dumped correctly
uint8 md5sum[16];

/*============= */
/* LOCALS */
/*============= */

static file_t wad_file;
static void *fullwad;
static lumpcache_t *lumpcache;
static int numlumps;
static lumpinfo_t *lumpinfo;

static file_t s2_file;
static void *s2wad;
static lumpcache_t *s2_lumpcache;
static int s2_numlumps;
static lumpinfo_t *s2_lumpinfo;

static file_t bump_file;
static void *bumpwad;
static lumpcache_t *bump_lumpcache;
static int bump_numlumps;
static lumpinfo_t *bump_lumpinfo;

static int mapnumlumps;
static lumpinfo_t *maplump;
static uint8_t *mapfileptr;

// lumpname hashing
static hashtable_t ht;
static hashtable_t altht;

static lumpinfo_t testlump;

// for lumpname comparisons
static char __attribute__((aligned(8))) name8[8];
// returnable lumpname
static char __attribute__((aligned(32))) retname[9];
static char __attribute__((aligned(8))) lumpname[8];

// 256 kb buffer to replace Z_Alloc in W_ReadLump
static uint8_t __attribute__((aligned(32))) input_w_readlump[262144];
static uint8_t *input = (uint8_t *)input_w_readlump;

static weapontype_t active_wepn = wp_nochange;
static uint8_t __attribute__((aligned(32))) *all_comp_wepn_bumps[10];

static void *pwepnbump;
static void *pnon_enemy;

static char drawstr[256];

static pvr_poly_hdr_t backhdr;
static pvr_poly_cxt_t backcxt;

static pvr_poly_hdr_t load2_hdr;
static pvr_poly_cxt_t load2_cxt;

static pvr_poly_cxt_t pvr_sprite_cxt;

static pvr_vertex_t wlsverts[16];

// verification that the lost levels are present and dumped correctly
static MD5_CTX ctx;

// verification that the lost levels are present and dumped correctly
static uint8_t md5_lostlevel[7][16] = {
{0xc0,0xb6,0x50,0x82,0x8e,0x55,0x2e,0x4c,0x75,0xa9,0x3c,0xcb,0x39,0x4c,0x89,0x75},
{0x11,0x3e,0x8c,0x80,0x46,0x9b,0x53,0xd6,0xab,0x92,0xcd,0x47,0x71,0x53,0x52,0x0a},
{0x54,0x59,0xda,0x76,0xa3,0xdf,0x1d,0xfa,0x0a,0x32,0x60,0x02,0xe4,0xe9,0x04,0xbe},
{0x62,0x34,0xa3,0xad,0x54,0x2f,0x44,0x41,0xc9,0xf9,0x1e,0xab,0x77,0x42,0xaf,0xc6},
{0x60,0x5b,0x30,0x72,0x6b,0x9b,0x0d,0xb6,0x8d,0x8e,0x62,0x6b,0x42,0x33,0x7a,0xe9},
{0x63,0xf6,0x9f,0x1b,0x9a,0xdf,0xf7,0xb8,0x94,0x2b,0x70,0x4d,0x89,0x41,0x32,0xec},
{0xf4,0x2b,0x74,0xf5,0xa5,0xa6,0xa7,0xa4,0x62,0xc0,0xcf,0xdb,0xfe,0x4a,0xd5,0xe7},
};

// verification that the lost levels are present and dumped correctly
static int size_lostlevel[7] = {
253568,
360456,
311768,
395260,
361192,
215276,
61996
};

/*=========*/
/* EXTERNS */
/*=========*/

extern pvr_dr_state_t dr_state;

/*=========*/
/* PROTOTYPES */
/*=========*/

void R_InitSymbols(void);

/*
============================================================================

						LUMP BASED ROUTINES

============================================================================
*/

// Hash table for fast lookups
static int comp_keys(void *el1, void *el2)
{
	int ti1 = *(int *)(((lumpinfo_t *)el1)->name);
	int ti2 = *(int *)(((lumpinfo_t *)el2)->name);
	int ti3 = *(int *)(&(((lumpinfo_t *)el1)->name)[4]);
	int ti4 = *(int *)(&(((lumpinfo_t *)el2)->name)[4]);

	return !(((ti1 & 0xffffff7f) == (ti2 & 0xffffff7f)) && (ti3 == ti4));
}

static unsigned long int W_LumpNameHash(char *s)
{
	// This is the djb2 string hash function, modded to work on strings
	// that have a maximum length of 8.
	unsigned long int result = 5381;
	unsigned long int i;

	result = ((result << 5) ^ result ) ^ (s[0] & 0x7f);

	for (i=1; i < 8 && s[i] != '\0'; ++i)
		result = ((result << 5) ^ result) ^ s[i];

	return result;
}

static unsigned long int hash(void *element, void *params)
{
	return W_LumpNameHash(((lumpinfo_t *)element)->name) & 255;
}

/*
====================
=
= W_DrawLoadScreen
=
====================
*/

// this does not get used after W_Init/S_Init return
void W_DrawLoadScreen(char *what, int current, int total)
{
	uint32_t color2 = 0xff323232;
	uint32_t color = 0xff525252;
	uint32_t color4 = 0xffa00000;
	uint32_t color3 = 0xff700000;
	pvr_vertex_t *vert = wlsverts;

	sprintf(drawstr, "loading: %s", what);

	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_OP_POLY);
	pvr_dr_init(&dr_state);

	pvr_poly_cxt_col(&load2_cxt, PVR_LIST_OP_POLY);
	load2_cxt.blend.src = PVR_BLEND_ONE;
	load2_cxt.blend.dst = PVR_BLEND_ONE;
	pvr_poly_compile(&load2_hdr, &load2_cxt);

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 160.0f;
	vert->y = (480 / 2) + 8;
	vert->z = 5.0f;
	vert++->argb = color;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 160.0f;
	vert->y = (480 / 2) - 16;
	vert->z = 5.0f;
	vert++->argb = color;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 480.0f;
	vert->y = (480 / 2) + 8;
	vert->z = 5.0f;
	vert++->argb = color;

	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 480.0f;
	vert->y = (480 / 2) - 16;
	vert->z = 5.0f;
	vert++->argb = color;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 162.0f;
	vert->y = (480 / 2) + 6;
	vert->z = 5.1f;
	vert++->argb = color2;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 162.0f;
	vert->y = (480 / 2) - 14;
	vert->z = 5.1f;
	vert++->argb = color2;

	vert->flags = PVR_CMD_VERTEX;
	// want max endpoint of 480-2 == 478
	// +316
	vert->x = 162.0f + (316.0f * (float)current / (float)total);
	vert->y = (480 / 2) + 6;
	vert->z = 5.1f;
	vert++->argb = color2;

	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 162.0f + (316.0f * (float)current / (float)total);
	vert->y = (480 / 2) - 14;
	vert->z = 5.1f;
	vert++->argb = color2;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 164.0f;
	vert->y = (480 / 2) + 4;
	vert->z = 5.2f;
	vert++->argb = color4;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 164.0f;
	vert->y = (480 / 2) - 12;
	vert->z = 5.2f;
	vert++->argb = color4;

	vert->flags = PVR_CMD_VERTEX;
	// want max endpoint of 478 - 2 == 476
	// +312
	vert->x = 164.0f + (312.0f * (float)current / (float)total);
	vert->y = (480 / 2) + 4;
	vert->z = 5.2f;
	vert++->argb = color4;

	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 164.0f + (312.0f * (float)current / (float)total);
	vert->y = (480 / 2) - 12;
	vert->z = 5.2f;
	vert++->argb = color4;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 167.0f;
	vert->y = (480 / 2) + 2;
	vert->z = 5.3f;
	vert++->argb = color3;

	vert->flags = PVR_CMD_VERTEX;
	vert->x = 167.0f;
	vert->y = (480 / 2) - 10;
	vert->z = 5.3f;
	vert++->argb = color3;

	vert->flags = PVR_CMD_VERTEX;
	// want max endpoint of 476 - 3 = 473
	// +306
	vert->x = 167.0f + (306.0f * (float)current / (float)total);
	vert->y = (480 / 2) + 2;
	vert->z = 5.3f;
	vert++->argb = color3;

	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 167.0f + (306.0f * (float)current / (float)total);
	vert->y = (480 / 2) - 10;
	vert->z = 5.3f;
	vert->argb = color3;

	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &load2_hdr, 1);	
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), wlsverts, 16);

	ST_DrawString(-1, ((((480 / 2) - 16) - 32)) / 2, drawstr, 0xa0a0a0ff, ST_ABOVE_OVL);

	pvr_list_finish();
	pvr_scene_finish();
}

/*
====================
=
= W_LoadWepnBumps
=
====================
*/

static void W_LoadWepnBumps(void) {
	ssize_t vqsize;

	sprintf(fnbuf, "%s/tex/wepn_decs.raw", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	wepndecs_txr = pvr_mem_malloc(64*64);
	if (!wepndecs_txr)
		I_Error("PVR OOM for muzzle flash texture");
	pvr_txr_load_ex(pwepnbump, wepndecs_txr, 64, 64, PVR_TXRLOAD_8BPP);
	free(pwepnbump);

	pvr_poly_cxt_t wepndecs_cxt;
	pvr_poly_cxt_txr(&wepndecs_cxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 64, 64, wepndecs_txr, PVR_FILTER_BILINEAR);
	wepndecs_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	wepndecs_cxt.gen.fog_type = PVR_FOG_TABLE;
	wepndecs_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_poly_compile(&wepndecs_hdr, &wepndecs_cxt);

	pvr_poly_cxt_txr(&wepndecs_cxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 64, 64, wepndecs_txr, PVR_FILTER_NONE);
	wepndecs_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	wepndecs_cxt.gen.fog_type = PVR_FOG_TABLE;
	wepndecs_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_poly_compile(&wepndecs_hdr_nofilter, &wepndecs_cxt);

	sprintf(fnbuf, "%s/tex/sawg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[0] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[0])
		I_Error("Could not allocate wepnbump 0");
	memcpy(all_comp_wepn_bumps[0], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/pung_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[1] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[1])
		I_Error("Could not allocate wepnbump 1");
	memcpy(all_comp_wepn_bumps[1], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/pisg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[2] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[2])
		I_Error("Could not allocate wepnbump 2");
	memcpy(all_comp_wepn_bumps[2], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/sht1_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[3] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[3])
		I_Error("Could not allocate wepnbump 3");
	memcpy(all_comp_wepn_bumps[3], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/sht2_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[4] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[4])
		I_Error("Could not allocate wepnbump 4");
	memcpy(all_comp_wepn_bumps[4], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/chgg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[5] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[5])
		I_Error("Could not allocate wepnbump 5");
	memcpy(all_comp_wepn_bumps[5], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/rock_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[6] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[6])
		I_Error("Could not allocate wepnbump 6");
	memcpy(all_comp_wepn_bumps[6], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/plas_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[7] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[7])
		I_Error("Could not allocate wepnbump 7");
	memcpy(all_comp_wepn_bumps[7], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/bfgg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[8] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[8])
		I_Error("Could not allocate wepnbump 8");
	memcpy(all_comp_wepn_bumps[8], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/lasr_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1)
		I_Error("Could not load %s", fnbuf);
	all_comp_wepn_bumps[9] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[9])
		I_Error("Could not allocate wepnbump 9");
	memcpy(all_comp_wepn_bumps[9], pwepnbump, vqsize);
	free(pwepnbump);
}

extern void P_FlushSprites(void);
extern void P_FlushAllCached(void);
static pvr_poly_cxt_t wepnbump_cxt;

void W_ReplaceWeaponBumps(weapontype_t wepn)
{
	int w,h;

	if (wepn == wp_nochange)
		return;

	if (gamemap == 33)
		return;

	// clean up old bump texture
	if (wepnbump_txr) {
		pvr_mem_free(wepnbump_txr);
		wepnbump_txr = 0;
	}

	active_wepn = wepn;

	switch (wepn) {
	case wp_chainsaw:
		w = 512;
		h = 128;
		break;

	case wp_fist:
		w = 512;
		h = 64;
		break;

	case wp_pistol:
		w = 256;
		h = 128;
		break;

	case wp_shotgun:
		w = 256;
		h = 128;
		break;

	case wp_supershotgun:
		w = 256;
		h = 64;
		break;

	case wp_chaingun:
		w = 256;
		h = 128;
		break;

	case wp_missile:
		w = 256;
		h = 128;
		break;

	case wp_plasma:
		w = 256;
		h = 128;
		break;

	case wp_bfg:
		w = 512;
		h = 128;
		break;

	case wp_laser:
		w = 256;
		h = 128;
		break;

	default:
		active_wepn = wp_nochange;
		return;
		break;
	}

	if ((uint32_t)(w*h*2) > pvr_mem_available())
		P_FlushAllCached();

	wepnbump_txr = pvr_mem_malloc(w*h*2);
	if (!wepnbump_txr)
		I_Error("PVR OOM for weapon normal map texture");

	decode_bumpmap((uint8_t *)all_comp_wepn_bumps[wepn], (uint8_t *)wepnbump_txr, w, h);

	pvr_poly_cxt_txr(&wepnbump_cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_BUMP | PVR_TXRFMT_TWIDDLED, w, h, wepnbump_txr, PVR_FILTER_BILINEAR);

	wepnbump_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	wepnbump_cxt.txr.env = PVR_TXRENV_DECAL;
	wepnbump_cxt.blend.src = PVR_BLEND_ONE;
	wepnbump_cxt.blend.dst = PVR_BLEND_ZERO;
	wepnbump_cxt.blend.src_enable = 0;
	// use secondary accumulation buffer
	wepnbump_cxt.blend.dst_enable = 1;

	pvr_poly_compile(&wepnbump_hdr, &wepnbump_cxt);
}

/*
====================
=
= W_Init
=
====================
*/

void W_Init(void)
{
	wadinfo_t *wadfileptr;
	wadinfo_t *s2_wadfileptr;
	bumpwadinfo_t *bump_wadfileptr;

	int infotableofs;
	int s2_infotableofs;
	int bump_infotableofs;

	ssize_t loadsize;
	unsigned char *chunk = NULL;

	hashtable_init(&ht, 256, comp_keys, hash, 0);
	hashtable_init(&altht, 256, comp_keys, hash, 0);

	extra_episodes = -6;

	pvr_set_pal_format(PVR_PAL_ARGB1555);

	// color 0 is always transparent (replacing RGB ff 00 ff)
	pvr_set_pal_entry(0, 0);
	for (int i = 1; i < 256; i++)
		pvr_set_pal_entry(i, get_color_argb1555(D64MONSTER[i][0], D64MONSTER[i][1], D64MONSTER[i][2],1));

	pvr_set_pal_entry(256, 0);
	for (int i = 1; i < 256; i++)
		pvr_set_pal_entry(256 + i, get_color_argb1555(D64NONENEMY[i][0], D64NONENEMY[i][1], D64NONENEMY[i][2],1));

	pvr_set_pal_entry(512, 0);
	for (int i = 1; i < 256; i++)
		pvr_set_pal_entry(512 + i, get_color_argb1555(PALTEXCONV[i][0], PALTEXCONV[i][1], PALTEXCONV[i][2],1));

	pvr_set_pal_entry(768, 0);
	for (int i = 1; i < 256; i++)
		pvr_set_pal_entry(768 + i, get_color_argb1555(i,i,i,1));

	R_InitSymbols();

	chunk = malloc(65536);
	if (!chunk)
		I_Error("OOM");

	for (int i = 34; i <= 40; i++) {
		sprintf(fnbuf, "%s/maps/map%d.wad", fnpre, i);
		file_t mapfd = fs_open(fnbuf, O_RDONLY);
		if (-1 != mapfd) {
			extra_episodes++;
			MD5Init(&ctx);

			size_t mapsize = size_lostlevel[i - 34];
			while (mapsize > 65536) {
				ssize_t rv = fs_read(mapfd, (void *)chunk, 65536);
				if (rv == -1) {
					extra_episodes = 0;
					fs_close(mapfd);
					goto kneedeep_check;
				}
				mapsize -= 65536;
				MD5Update(&ctx, chunk, 65536);
			}
			memset(chunk, 0, 65536);
			fs_read(mapfd, (void *)chunk, mapsize);
			fs_close(mapfd);
			MD5Update(&ctx, chunk, mapsize);
			MD5Final(md5sum, &ctx);
			if (memcmp(md5sum, md5_lostlevel[i - 34], 16))
			{
				extra_episodes = 0;
				break;
			}
		} else {
			extra_episodes = 0;
			break;
		}
	}

kneedeep_check:
	sprintf(fnbuf, "%s/maps/map%d.wad", fnpre, 41);
	file_t mapfd = fs_open(fnbuf, O_RDONLY);
	if (-1 != mapfd) {
		if (extra_episodes == 0) {
			kneedeep_only = 1;
		}
		dbgio_printf("found map 41\n");
		extra_episodes++;
		fs_close(mapfd);
	}

	if (chunk)
		free(chunk);

	pvr_ptr_t back_tex = 0;
	back_tex = pvr_mem_malloc(512 * 512 * 2);
	if (!back_tex)
		I_Error("Could not allocate texture");

	pvr_poly_cxt_txr(&backcxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565, 512, 512, back_tex, PVR_FILTER_BILINEAR);
	pvr_poly_compile(&backhdr, &backcxt);

	void *backbuf = NULL;
	sprintf(fnbuf, startupfile, fnpre);
	fs_load(fnbuf, &backbuf);

	if (backbuf) {
		pvr_txr_load(backbuf, back_tex, 512 * 512 * 2);
		MD5Init(&ctx);
		MD5Update(&ctx, backbuf, 512*512*2);
		MD5Final(backres, &ctx);
		if (memcmp(backres, backcheck, 16))
			I_Error(waderrstr);
	}
	else
		I_Error(waderrstr);

	pvr_vertex_t *backvert;

	for (int i = 0; i < 300; i++) {
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_dr_init(&dr_state);

		backvert = wlsverts;
		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		backvert->flags = PVR_CMD_VERTEX;
		backvert->x = 0.0f;
		backvert->y = 0.0f;
		backvert->z = 1.0f;
		backvert->u = 0.0f;
		backvert++->v = 0.0f;

		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		backvert->flags = PVR_CMD_VERTEX;
		backvert->x = 640.0f;
		backvert->y = 0.0f;
		backvert->z = 1.0f;
		backvert->u = 1.0f;
		backvert++->v = 0.0f;

		backvert->flags = PVR_CMD_VERTEX;
		backvert->x = 0.0f;
		backvert->y = 480.0f;
		backvert->z = 1.0f;
		backvert->u = 0.0f;
		backvert->v = 1.0f;
		backvert->argb = 0xffffffff;
		backvert++->oargb = 0;

		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		backvert->flags = PVR_CMD_VERTEX_EOL;
		backvert->x = 640.0f;
		backvert->y = 480.0f;
		backvert->z = 1.0f;
		backvert->u = 1.0f;
		backvert->v = 1.0f;

		sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &backhdr, 1);	
		sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), wlsverts, 4);

		pvr_list_finish();
		pvr_scene_finish();
	}

	if (back_tex)
		pvr_mem_free(back_tex);
	if (backbuf)
		free(backbuf);

	// get optional custom controller mapping from disk
	char *mapping_file;
	sprintf(fnbuf, "%s/controls.ini", fnpre);
	fs_load(fnbuf, (void **)&mapping_file);
	// it is ok if `mapping_file` is `NULL`
	I_ParseMappingFile(mapping_file);
	// but if it isn't NULL
	if (mapping_file) {
		W_DrawLoadScreen("controller config", 50, 100);
		sleep(1);
		W_DrawLoadScreen("controller config", 100, 100);
		// just be sure to free it here
		free(mapping_file);
	}

	// weapon bumpmaps
	W_DrawLoadScreen("weapon bumpmaps", 50, 100);
	W_LoadWepnBumps();
	W_DrawLoadScreen("weapon bumpmaps", 100, 100);

	// all non-enemy sprites are in an uncompressed, pretwiddled 8bpp 1024^2 sheet texture
	W_DrawLoadScreen("non-enemy sprites", 50, 100);
	sprintf(fnbuf, "%s/tex/non_enemy.tex", fnpre);
	loadsize = fs_load(fnbuf, &pnon_enemy);
	if (-1 == loadsize)
		I_Error("Could not load %s", fnbuf);

	W_DrawLoadScreen("non-enemy sprites", 50, 100);
	dbgio_printf("non_enemy loaded size is %d\n", loadsize);
	pvr_non_enemy = pvr_mem_malloc(loadsize);
	if (!pvr_non_enemy)
		I_Error("PVR OOM for non-enemy texture");
	pvr_txr_load(pnon_enemy, pvr_non_enemy, loadsize);
	free(pnon_enemy);

	W_DrawLoadScreen("non-enemy sprites", 100, 100);

	dbgio_printf("PVR mem free after non_enemy: %u\n", pvr_mem_available());

	// doom64 wad
	dbgio_printf("W_Init: Loading IWAD into RAM...\n");

	wadfileptr = (wadinfo_t *)malloc(sizeof(wadinfo_t));
	if (!wadfileptr)
		I_Error("failed malloc wadfileptr");

	sprintf(fnbuf, "%s/pow2.wad", fnpre);
	wad_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == wad_file)
		I_Error("Could not open %s for reading.", fnbuf);

	size_t full_wad_size = fs_seek(wad_file, 0, SEEK_END);
	size_t wad_rem_size = full_wad_size;
	fullwad = malloc(wad_rem_size);
	if (!fullwad)
		I_Error("OOM for %s", fnbuf);

	size_t wad_read = 0;
	fs_seek(wad_file, 0, SEEK_SET);
	while (wad_rem_size > (128 * 1024)) {
		ssize_t rv = fs_read(wad_file, (void *)fullwad + wad_read, (128 * 1024));
		if (rv == -1)
			I_Error("failed to read IWAD");
		wad_read += rv;
		wad_rem_size -= rv;
		W_DrawLoadScreen("doom 64 iwad", wad_read, full_wad_size);
	}
	fs_read(wad_file, (void *)fullwad + wad_read, wad_rem_size);
	wad_read += wad_rem_size;
	W_DrawLoadScreen("doom 64 iwad", wad_read, full_wad_size);
#if RANGECHECK
	malloc_stats();
#endif
	dbgio_printf("\tDone.\n");
	fs_close(wad_file);

	memcpy((void *)wadfileptr, fullwad + 0, sizeof(wadinfo_t));
	if (strncasecmp(&wadfileptr->identification[1], "WAD", 3))
		I_Error("invalid main IWAD id");

	numlumps = (wadfileptr->numlumps);
	lumpinfo = (lumpinfo_t *)Z_Malloc(numlumps * sizeof(lumpinfo_t), PU_STATIC, 0);
	infotableofs = (wadfileptr->infotableofs);
	memcpy((void *)lumpinfo, fullwad + infotableofs, numlumps * sizeof(lumpinfo_t));
	lumpcache = (lumpcache_t *)Z_Malloc(numlumps * sizeof(lumpcache_t), PU_STATIC, 0);
	memset(lumpcache, 0, numlumps * sizeof(lumpcache_t));
	free(wadfileptr);

	lumpinfo_t *lump_p = &lumpinfo[0];
	for (int i=0; i<numlumps ; i++,lump_p++)
		hashtable_insert(&ht, (void*)lump_p, -1);

	// alternate palette sprite wad
	dbgio_printf("W_Init: Loading alt sprite PWAD into RAM...\n");

	s2_wadfileptr = (wadinfo_t *)malloc(sizeof(wadinfo_t));
	if (!s2_wadfileptr)
		I_Error("failed malloc s2_wadfileptr");

	sprintf(fnbuf, "%s/alt.wad", fnpre);
	s2_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == s2_file)
		I_Error("Could not open %s for reading.", fnbuf);

	size_t alt_wad_size = fs_seek(s2_file, 0, SEEK_END);
	wad_rem_size = alt_wad_size;
	s2wad = malloc(wad_rem_size);
	if (!s2wad)
		I_Error("OOM for %s", fnbuf);

	wad_read = 0;
	fs_seek(s2_file, 0, SEEK_SET);
	while (wad_rem_size > (128 * 1024)) {
		ssize_t rv = fs_read(s2_file, (void *)s2wad + wad_read, (128 * 1024));
		if (rv == -1)
			I_Error("failed to read PWAD");
		wad_read += rv;
		wad_rem_size -= rv;
		W_DrawLoadScreen("alt sprite wad", wad_read, alt_wad_size);
	}
	fs_read(s2_file, (void *)s2wad + wad_read, wad_rem_size);
	wad_read += wad_rem_size;
	W_DrawLoadScreen("alt sprite wad", wad_read, alt_wad_size);
	dbgio_printf("\tDone.\n");
	fs_close(s2_file);
#if RANGECHECK
	malloc_stats();
#endif

	memcpy((void *)s2_wadfileptr, s2wad + 0, sizeof(wadinfo_t));
	if (strncasecmp(s2_wadfileptr->identification, "PWAD", 4))
		I_Error("invalid alt sprite PWAD id");

	s2_numlumps = (s2_wadfileptr->numlumps);
	s2_lumpinfo = (lumpinfo_t *)Z_Malloc(s2_numlumps * sizeof(lumpinfo_t), PU_STATIC, 0);
	s2_infotableofs = (s2_wadfileptr->infotableofs);
	memcpy((void *)s2_lumpinfo, s2wad + s2_infotableofs, s2_numlumps * sizeof(lumpinfo_t));
	s2_lumpcache = (lumpcache_t *)Z_Malloc( s2_numlumps * sizeof(lumpcache_t), PU_STATIC, 0);
	memset(s2_lumpcache, 0, s2_numlumps * sizeof(lumpcache_t));
	free(s2_wadfileptr);

	lumpinfo_t *s2_lump_p = &s2_lumpinfo[0];
	for (int i=0; i<s2_numlumps ; i++,s2_lump_p++)
		hashtable_insert(&altht, (void*)s2_lump_p, -1);

	// compressed bumpmap wad
	dbgio_printf("W_Init: Loading bumpmap PWAD into RAM...\n");

	bump_wadfileptr =(bumpwadinfo_t *)malloc(sizeof(bumpwadinfo_t));
	if (!bump_wadfileptr)
		I_Error("failed malloc bump_wadfileptr");

	sprintf(fnbuf, "%s/bump.wad", fnpre);
	bump_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == bump_file)
		I_Error("Could not open %s for reading.", fnbuf);

	size_t bump_wad_size = fs_seek(bump_file, 0, SEEK_END);
	wad_rem_size = bump_wad_size;
	bumpwad = malloc(wad_rem_size);
	if (!bumpwad)
		I_Error("OOM for %s", fnbuf);

	wad_read = 0;
	fs_seek(bump_file, 0, SEEK_SET);
	while (wad_rem_size > (128 * 1024)) {
		ssize_t rv = fs_read(bump_file, (void *)bumpwad + wad_read, (128 * 1024));
		if (rv == -1)
			I_Error("failed to read bumpmap wad");
		wad_read += rv;
		wad_rem_size -= rv;
		W_DrawLoadScreen("bumpmap wad", wad_read, bump_wad_size);
	}
	fs_read(bump_file, (void *)bumpwad + wad_read, wad_rem_size);
	wad_read += wad_rem_size;
	W_DrawLoadScreen("bumpmap wad", wad_read, bump_wad_size);
	dbgio_printf("\tDone.\n");
	fs_close(bump_file);
#if RANGECHECK
	malloc_stats();
#endif

	memcpy((void *)bump_wadfileptr, bumpwad + 0, sizeof(bumpwadinfo_t));
	if (strncasecmp(bump_wadfileptr->identification, "PWAD", 4))
		I_Error("invalid bumpmap PWAD id");

	bump_numlumps = (bump_wadfileptr->numlumps);
	bump_lumpinfo = (lumpinfo_t *)Z_Malloc(bump_numlumps * sizeof(lumpinfo_t), PU_STATIC, 0);
	bump_infotableofs = (bump_wadfileptr->infotableofs);
	memcpy((void *)bump_lumpinfo, bumpwad + bump_infotableofs, bump_numlumps * sizeof(lumpinfo_t));
	bump_lumpcache = (lumpcache_t *)Z_Malloc(bump_numlumps * sizeof(lumpcache_t), PU_STATIC, 0);
	memset(bump_lumpcache, 0, bump_numlumps * sizeof(lumpcache_t));
	free(bump_wadfileptr);

	// common shared poly context/header used for all non-enemy sprites
	// headers for sprite diffuse when no bumpmapping
	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 1024, 1024, pvr_non_enemy, PVR_FILTER_BILINEAR);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
#if FOG_VERTEX
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_VERTEX;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_VERTEX;
#else
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
#endif
	pvr_poly_compile(&pvr_sprite_hdr, &pvr_sprite_cxt);

	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 1024, 1024, pvr_non_enemy, PVR_FILTER_NONE);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
#if FOG_VERTEX
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_VERTEX;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_VERTEX;
#else
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
#endif
	pvr_poly_compile(&pvr_sprite_hdr_nofilter, &pvr_sprite_cxt);

	// headers for sprite diffuse when bumpmapping active (weapons)
	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 1024, 1024, pvr_non_enemy, PVR_FILTER_BILINEAR);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_sprite_cxt.blend.src = PVR_BLEND_DESTCOLOR;
	pvr_sprite_cxt.blend.dst = PVR_BLEND_ZERO;
	pvr_sprite_cxt.blend.src_enable = 0;
	// use secondary accumulation buffer
	pvr_sprite_cxt.blend.dst_enable = 1;
	pvr_poly_compile(&pvr_sprite_hdr_bump, &pvr_sprite_cxt);

	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY, D64_TPAL(PAL_ITEM), 1024, 1024, pvr_non_enemy, PVR_FILTER_NONE);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_sprite_cxt.blend.src = PVR_BLEND_DESTCOLOR;
	pvr_sprite_cxt.blend.dst = PVR_BLEND_ZERO;
	pvr_sprite_cxt.blend.src_enable = 0;
	// use secondary accumulation buffer
	pvr_sprite_cxt.blend.dst_enable = 1;
	pvr_poly_compile(&pvr_sprite_hdr_nofilter_bump, &pvr_sprite_cxt);
}

// return human-readable "uncompressed" name for a lump number
char *W_GetNameForNum(int num)
{
/* 	memset(retname, 0, 9);
	int ln_len = strlen(lumpinfo[num].name);
	if (ln_len > 8)
		ln_len = 8;
	memcpy(retname, lumpinfo[num].name, ln_len); */
	strncpy(retname, lumpinfo[num].name, 8);
	retname[0] &= 0x7f;

	return retname;
}

/*
====================
=
= W_CheckNumForName
=
= Returns -1 if name not found
=
====================
*/

int W_CheckNumForName(char *name)
{
	void *ret_node;
	lumpinfo_t *retlump;

	// -Werror not very happy with this :-D
	// strncpy(testlump.name, name, 8);
	size_t copylen = strlen(name);
	if (copylen > 8) copylen = 8;
	memset(testlump.name, 0, 8);
	memcpy(testlump.name, name, copylen);

	retlump = (lumpinfo_t *)is_in_hashtable(&ht, &testlump, &ret_node);

	if (!retlump)
		return -1;

	return retlump - lumpinfo;
}

/*
====================
=
= W_GetNumForName
=
= Calls W_CheckNumForName, but bombs out if not found
=
====================
*/

int W_GetNumForName(char *name)
{
#if RANGECHECK
	int i;

	i = W_CheckNumForName(name);
	if (i != -1)
		return i;

	I_Error("%s not found!", name);
	return -1;
#else
	return W_CheckNumForName(name);
#endif	
}

/*
====================
=
= W_LumpLength
=
= Returns the buffer size needed to load the given lump
=
====================
*/

int W_LumpLength(int lump)
{
#if RANGECHECK
	if ((lump < 0) || (lump >= numlumps))
		I_Error("lump %i out of range", lump);
#endif
	return lumpinfo[lump].size;
}

/*
====================
=
= W_ReadLump
=
= Loads the lump into the given buffer, which must be >= W_LumpLength()
=
====================
*/

void W_ReadLump(int lump, void *dest, decodetype dectype)
{
	lumpinfo_t *l;

#if RANGECHECK
	if ((lump < 0) || (lump >= numlumps))
		I_Error("lump %i out of range", lump);
#endif

	l = &lumpinfo[lump];
	/* compressed */
	if ((l->name[0] & 0x80)) {
		memcpy((void *)input, fullwad + l->filepos, l[1].filepos - l->filepos);
		if (dectype == dec_jag)
			DecodeJaguar((uint8_t *)input, (uint8_t *)dest);
		else // dec_d64
			DecodeD64((uint8_t *)input, (uint8_t *)dest);
	} else {
		memcpy((void *)dest, fullwad + l->filepos, l->size);
	}
}

/*
====================
=
= W_CacheLumpNum
=
====================
*/
#if RANGECHECK
int last_touched = -1;
#endif

void *W_CacheLumpNum(int lump, int tag, decodetype dectype)
{
	lumpcache_t *lc;

#if RANGECHECK
	if ((lump < 0) || (lump >= numlumps))
		I_Error("lump %i out of range", lump);
#endif

#if 0
	// wadtool emits uncompressed spriteDC_t header-only lumps
	// for all non-enemy sprites [1,346] and [924,965]
	// we can return a direct pointer to the data in the WAD
	if (lump >= 1 && lump <= 346)
		return fullwad + lumpinfo[lump].filepos;
	else if (lump >= 924 && lump <= 965)
		return fullwad + lumpinfo[lump].filepos;
#endif

	lc = &lumpcache[lump];

	if (!lc->cache) { /* read the lump in */
		Z_Malloc(lumpinfo[lump].size, tag, &lc->cache);
		W_ReadLump(lump, lc->cache, dectype);
	} else {
		if (tag & PU_CACHE) {
#if RANGECHECK
			last_touched = lump;
#endif
			Z_Touch(lc->cache);
		}
	}

	return lc->cache;
}

/*
====================
=
= W_CacheLumpName
=
====================
*/

void *W_CacheLumpName(char *name, int tag, decodetype dectype)
{
	return W_CacheLumpNum(W_GetNumForName(name), tag, dectype);
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/*
alt sprite routines
*/
/*
====================
=
= W_S2_CheckNumForName
=
= Returns -1 if name not found
=
====================
*/

int W_S2_CheckNumForName(char *name)
{
	void *ret_node;
	lumpinfo_t *retlump;

	// -Werror not very happy with this :-D
	// strncpy(testlump.name, name, 8);
	size_t copylen = strlen(name);
	if (copylen > 8) copylen = 8;
	memset(testlump.name, 0, 8);
	memcpy(testlump.name, name, copylen);

	retlump = (lumpinfo_t *)is_in_hashtable(&altht, &testlump, &ret_node);

	if (!retlump)
		return -1;

	return retlump - s2_lumpinfo;
}

/*
====================
=
= W_S2_GetNumForName
=
= Calls W_S2_CheckNumForName, but bombs out if not found
=
====================
*/

int W_S2_GetNumForName(char *name)
{
	return W_S2_CheckNumForName(name);
}

/*
====================
=
= W_S2_LumpLength
=
= Returns the buffer size needed to load the given lump
=
====================
*/

int W_S2_LumpLength(int lump)
{
#if RANGECHECK
	if ((lump < 0) || (lump >= s2_numlumps))
		I_Error("lump %i out of range", lump);
#endif

	return s2_lumpinfo[lump].size;
}

/*
====================
=
= W_S2_ReadLump
=
= Loads the lump into the given buffer, which must be >= W_S2_LumpLength()
=
====================
*/
void W_S2_ReadLump(int lump, void *dest)
{
	lumpinfo_t *l;
#if RANGECHECK
	if ((lump < 0) || (lump >= s2_numlumps))
		I_Error("lump %i out of range", lump);
#endif

	l = &s2_lumpinfo[lump];

	memcpy((void *)input, s2wad + l->filepos, l[1].filepos - l->filepos);

	// always jag compressed (by wadtool)
	DecodeJaguar((uint8_t *)input, (uint8_t *)dest);
}

/*
====================
=
= W_S2_CacheLumpNum
=
====================
*/

// this is only ever used with dec_jag, so save a register by not passing
// decodetype as a parameter
void *W_S2_CacheLumpNum(int lump, int tag)
{
	lumpcache_t *lc;
#if RANGECHECK
	if ((lump < 0) || (lump >= s2_numlumps))
		I_Error("lump %i out of range", lump);
#endif

	lc = &s2_lumpcache[lump];

	if (!lc->cache) { /* read the lump in */
		Z_Malloc(s2_lumpinfo[lump].size, tag, &lc->cache);
		W_S2_ReadLump(lump, lc->cache);
	} else {
		if (tag & PU_CACHE) {
#if RANGECHECK			
			last_touched = lump + 100000;
#endif
			Z_Touch(lc->cache);
		}
	}

	return lc->cache;
}

/*
====================
=
= W_S2_CacheLumpName
=
====================
*/

void *W_S2_CacheLumpName(char *name, int tag)
{
	return W_S2_CacheLumpNum(W_S2_GetNumForName(name), tag);
}

/*
bumpmap routines
*/
/*
====================
=
= W_Bump_CheckNumForName
=
= Returns -1 if name not found or lump is 0 size
= 0 size lump means bumpmap didnt exist at generation time
=
====================
*/
int W_Bump_CheckNumForName(char *name)
{
	lumpinfo_t *lump_p;

	int n_len = MIN(8, strlen(name));

	memset(name8, 0, 8);
	memcpy(name8, name, n_len);

	lump_p = bump_lumpinfo;

	for (int i = 0; i < bump_numlumps; i++) {
		int ln_len = MIN(8, strlen(lump_p->name));

		memset(lumpname, 0, 8);
		memcpy(lumpname, lump_p->name, ln_len);

		if (!memcmp(name8, lumpname, 8)) {
			if (lump_p->size == 0)
				return -1;

			return i;
		}

		lump_p++;
	}

	return -1;
}

/*
====================
=
= W_Bump_GetNumForName
=
= Calls W_Bump_CheckNumForName, but bombs out if not found
=
====================
*/

int W_Bump_GetNumForName(char *name)
{
#if RANGECHECK
	int i;

	i = W_Bump_CheckNumForName(name);
	if (i != -1)
		return i;

#if 0 //RANGECHECK
	I_Error("%s not found!", name);
#endif
	return -1;
#endif

	return W_Bump_CheckNumForName(name);
}

/*
====================
=
= W_Bump_LumpLength
=
= Returns the buffer size needed to load the given lump
=
====================
*/

int W_Bump_LumpLength(int lump)
{
#if RANGECHECK
	if ((lump < 0) || (lump >= bump_numlumps))
		I_Error("lump %i out of range", lump);
#endif

	return bump_lumpinfo[lump].size;
}

/*
====================
=
= W_Bump_ReadLump
=
= Loads the lump into the given buffer, which must be >= W_Bump_LumpLength()
=
====================
*/

void W_Bump_ReadLump(int lump, void *dest, int w, int h)
{
	lumpinfo_t *l;

#if RANGECHECK
	if ((lump < 0) || (lump >= bump_numlumps))
		I_Error("lump %i out of range", lump);
#endif

	l = &bump_lumpinfo[lump];

	decode_bumpmap((uint8_t *)(bumpwad + l->filepos), (uint8_t *)dest, w, h);
}

/*
============================================================================

MAP LUMP BASED ROUTINES

============================================================================
*/

/*
====================
=
= W_OpenMapWad
=
= Exclusive Psx Doom / Doom64
====================
*/
static char mapname[8];
void W_OpenMapWad(int mapnum)
{
	int infotableofs;

#if RANGECHECK
	if (mapnum == 0) {
		dbgio_printf("requested map 0, not crashing, giving you 1 instead\n");
		mapnum = 1;
	}
#endif

	mapname[0] = 'm';
	mapname[1] = 'a';
	mapname[2] = 'p';
	mapname[3] = '0' + (char)(mapnum / 10);
	mapname[4] = '0' + (char)(mapnum % 10);
	mapname[5] = 0;

	sprintf(fnbuf, "%s/maps/%s.wad", fnpre, mapname);
	file_t mapfd = fs_open(fnbuf, O_RDONLY);
	if (-1 == mapfd) {
		I_Error("Could not open %s for reading.", fnbuf);
	}
	size_t mapsize = fs_seek(mapfd, 0, SEEK_END);
	fs_seek(mapfd, 0, SEEK_SET);
	mapfileptr = Z_Alloc(mapsize, PU_STATIC, NULL);
	fs_read(mapfd, mapfileptr, mapsize);
	fs_close(mapfd);

	mapnumlumps = (((bumpwadinfo_t *)mapfileptr)->numlumps);
	infotableofs = (((bumpwadinfo_t *)mapfileptr)->infotableofs);

	maplump = (lumpinfo_t *)(mapfileptr + infotableofs);
}

/*
====================
=
= W_FreeMapLump
=
= Exclusive Doom64
====================
*/

void W_FreeMapLump(void)
{
	Z_Free(mapfileptr);
	mapnumlumps = 0;
}

/*
====================
=
= W_MapLumpLength
=
= Exclusive Psx Doom / Doom64
====================
*/

int W_MapLumpLength(int lump)
{
#if RANGECHECK
	if (lump >= mapnumlumps)
		I_Error("%i out of range", lump);
#endif
	return maplump[lump].size;
}

/*
====================
=
= W_GetMapLump
=
= Exclusive Doom64
====================
*/

void *W_GetMapLump(int lump) // 8002C890
{
#if RANGECHECK
	if (lump >= mapnumlumps)
		I_Error("lump %d out of range", lump);
#endif
	return (void *)((uint8_t *)mapfileptr + maplump[lump].filepos);
}

static char tmpmapname[64];
char extramaps[64][64];
void W_ListExtraMaps(void)
{
	file_t d;
	dirent_t *de;
	int extramapnum = 0;

	sprintf(fnbuf, "%s/maps", fnpre);

	d = fs_open(fnbuf, O_RDONLY | O_DIR);
	if(!d) return;

	memset(extramaps, 0, 64*64);
	tmpmapname[0] = 'm';
	tmpmapname[1] = 'a';
	tmpmapname[2] = 'p';
	tmpmapname[5] = 0;

	while (NULL != (de = fs_readdir(d))) {
		if (extramapnum == 64) break;
		if (strcmp(de->name, ".") == 0) continue;
		if (strcmp(de->name, "..") == 0) continue;
		for (int mapnum=1;mapnum<50;mapnum++) {
			tmpmapname[3] = '0' + (char)(mapnum / 10);
			tmpmapname[4] = '0' + (char)(mapnum % 10);

			if (strcmp(de->name, tmpmapname) == 0) continue;
		}

		memcpy(extramaps[extramapnum++], de->name, strlen(de->name));
	}

	fs_close(d);
}

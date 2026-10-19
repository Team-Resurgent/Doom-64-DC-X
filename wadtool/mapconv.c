/*

derived from D64Tool by Erick194

https://github.com/Erick194/D64TOOL/tree/main

*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>

void Error(const char *s, ...)
{
	va_list args;
	va_start(args, s);
	vfprintf(stderr, s, args);
	fprintf(stderr, "\n");
	va_end(args);
	exit(0);
}

static int T_START = 0;
static int T_END = 0;
#include "d64tool_texs.h"
char Textures[2048][9] = {};
char Flats[2048][9] = {};

int GetFlatNum(char *name)
{
	int i = 0;

	for (i = 0; i < 2048; i++) {
		if (!strcmp(name, Flats[i]))
			break;
	}

	return i;
}

int GetTextureNum(char *name)
{
	int i = 0;

	for (i = 0; i < 2048; i++) {
		if (!strcmp(name, Textures[i]))
			break;
	}

	return i;
}

//
// P_InitTextureHashTable
//

static unsigned short texturehashlist[2][2048];

//
// P_GetTextureHashKey
//

static unsigned short P_GetTextureHashKey(int hash)
{
	int i;

	for (i = 0; i < 2048 /*numtextures*/; i++) {
		if (texturehashlist[0][i] == hash) {
			return texturehashlist[1][i];
		}
	}

	return 0;
}

unsigned int W_HashLumpName(const char *str)
{
	unsigned int hash = 1315423911;
	unsigned int i = 0;

	for (i = 0; i < 8 && *str != '\0'; str++, i++) {
		hash ^= ((hash << 5) + toupper((int)*str) + (hash >> 2));
	}

	return hash;
}

enum
{
	LABEL,
	THINGS,
	LINEDEFS,
	SIDEDEFS,
	VERTEXES,
	SEGS,
	SSECTORS,
	NODES,
	SECTORS,
	REJECT,
	BLOCKMAP,
	LEAFS,
	LIGHTS,
	MACROS,
	SCRIPTS
};

FILE *mapin;
FILE *mapout;

static int mapdatacnt = 0;
static int mapdatasize = 0;

typedef struct
{
	int filepos; // also texture_t * for comp lumps
	int size;
	char name[8 + 1];
} mc_lumpinfo_t;

typedef struct
{
	char identification[4]; // should be IWAD
	int numlumps;
	int infotableofs;
} mc_wadinfo_t;

mc_wadinfo_t mc_wadfile;

mc_lumpinfo_t *mc_lumpinfo; // points directly to rom image
int mc_numlumps;

int offcnt = 12;

void CopyLump(int lump)
{
	//	unsigned char *input, *output;
	unsigned char data;
	int i;
	int size = 0;
	int pow = 0;
	int ilen = 0;
	int olen = 0;

	fseek(mapin, mc_lumpinfo[lump].filepos, SEEK_SET);

	// setnew filepos
	mc_lumpinfo[lump].filepos = offcnt;

	for (i = 0; i < mc_lumpinfo[lump].size; i++) {
		fread(&data, sizeof(unsigned char), 1, mapin);
		fputc(data, mapout);
		size++;
		olen++;
	}

	pow = mc_lumpinfo[lump].size % 4;
	if (pow != 0) {
		for (i = 0; i < 4 - pow; i++) {
			fputc(0x00, mapout);
			olen++;
		}
	}

	mc_lumpinfo[lump].size = size;
	offcnt += olen;
}

void ConvertSidefs(int lump)
{
	int size = 0;
	char nulltex[9] = {"-"};

	short xoffset;
	short yoffset;
	char upper[9] = {0};
	char lower[9] = {0};
	char middle[9] = {0};
	short faces;

	unsigned short tex_up = 0;
	unsigned short tex_low = 0;
	unsigned short tex_mid = 0;

	unsigned short tex_up2;
	unsigned short tex_low2;
	unsigned short tex_mid2;

	short texnull = -1;

	int numsidefs = 0;

	fseek(mapin, mc_lumpinfo[lump].filepos, SEEK_SET);

	// setnew filepos
	mc_lumpinfo[lump].filepos = offcnt;

	numsidefs = mc_lumpinfo[lump].size / 12;

	for (int i = 0; i < numsidefs; i++) {
		fread(&xoffset, sizeof(short), 1, mapin);
		fread(&yoffset, sizeof(short), 1, mapin);
		fread(&tex_up, sizeof(short), 1, mapin);
		fread(&tex_low, sizeof(short), 1, mapin);
		fread(&tex_mid, sizeof(short), 1, mapin);
		fread(&faces, sizeof(short), 1, mapin);

		// write lump

		fwrite(&xoffset, sizeof(short), 1, mapout);
		fwrite(&yoffset, sizeof(short), 1, mapout);

		tex_up2 = P_GetTextureHashKey(tex_up);
		fwrite(&tex_up2, sizeof(short), 1, mapout);

		tex_low2 = P_GetTextureHashKey(tex_low);
		fwrite(&tex_low2, sizeof(short), 1, mapout);

		tex_mid2 = P_GetTextureHashKey(tex_mid);
		fwrite(&tex_mid2, sizeof(short), 1, mapout);

		fwrite(&faces, sizeof(short), 1, mapout);
		size += 12;
	}

	mc_lumpinfo[lump].size = size;

	int olen = size;
	int pow = mc_lumpinfo[lump].size % 4;
	if (pow != 0) {
		for (int i = 0; i < 4 - pow; i++) {
			fputc(0x00, mapout);
			olen++;
		}
	}

	offcnt += olen;
}

void ConvertSectors(int lump)
{
	int size = 0;
	char nulltex[9] = {"-"};
	short texnull = -1;
	short floorz = 0;
	short ceilz = 0;
	char floortex[9] = {0};
	char ceiltex[9] = {0};
	short light = 0;
	short light2 = 0;
	short special = 0;
	short tag = 0;
	short flags = 0;

	unsigned short color[5];

	unsigned short tex_floor = 0;
	unsigned short tex_ceil = 0;

	unsigned short tex_floor2;
	unsigned short tex_ceil2;

	int numsectors = 0;

	fseek(mapin, mc_lumpinfo[lump].filepos, SEEK_SET);

	// setnew filepos
	mc_lumpinfo[lump].filepos = offcnt;
	numsectors = mc_lumpinfo[lump].size / 24;

	for (int i = 0; i < numsectors; i++) {
		fread(&floorz, sizeof(short), 1, mapin);
		fread(&ceilz, sizeof(short), 1, mapin);

		fread(&tex_floor, sizeof(short), 1, mapin);
		fread(&tex_ceil, sizeof(short), 1, mapin);

		fread(&color, sizeof(color), 1, mapin);

		fread(&special, sizeof(short), 1, mapin);
		fread(&tag, sizeof(short), 1, mapin);
		fread(&flags, sizeof(short), 1, mapin);

		// write lump

		fwrite(&floorz, sizeof(short), 1, mapout);
		fwrite(&ceilz, sizeof(short), 1, mapout);

		tex_floor2 = P_GetTextureHashKey(tex_floor);
		fwrite(&tex_floor2, sizeof(short), 1, mapout);

		tex_ceil2 = P_GetTextureHashKey(tex_ceil);
		fwrite(&tex_ceil2, sizeof(short), 1, mapout);

		fwrite(&color, sizeof(color), 1, mapout);

		fwrite(&special, sizeof(short), 1, mapout);
		fwrite(&tag, sizeof(short), 1, mapout);
		fwrite(&flags, sizeof(short), 1, mapout);

		size += 24;
	}

	mc_lumpinfo[lump].size = size;

	int olen = size;
	int pow = mc_lumpinfo[lump].size % 4;
	if (pow != 0) {
		for (int i = 0; i < 4 - pow; i++) {
			fputc(0x00, mapout);
			olen++;
		}
	}

	offcnt += olen;
}

void Read_MapWad(char *name)
{
	int i;
	bool haveScripts = false;
	if ((mapin = fopen(name, "r+b")) == 0) {
		Error("No encuentra el archivo %s\n", name);
	}

	memset(&mc_wadfile, 0, sizeof(mc_wadfile));

	fread(&mc_wadfile, sizeof(mc_wadfile), 1, mapin);
	mc_wadfile.identification[0] = 'I';

	mapdatacnt = 0;
	mapdatasize = mc_numlumps;

	mc_numlumps = mc_wadfile.numlumps;
	mc_lumpinfo = (mc_lumpinfo_t *)malloc(mc_numlumps * sizeof(mc_lumpinfo_t));
	memset(mc_lumpinfo, 0, mc_numlumps * sizeof(mc_lumpinfo_t));

	fseek(mapin, mc_wadfile.infotableofs, SEEK_SET);

	for (i = 0; i < mc_numlumps; i++) {
		fread(&mc_lumpinfo[i].filepos, sizeof(unsigned int), 1, mapin);
		fread(&mc_lumpinfo[i].size, sizeof(unsigned int), 1, mapin);
		fread(&mc_lumpinfo[i].name, sizeof(unsigned int) * 2, 1, mapin);

		if (!strcmp(mc_lumpinfo[i].name, "T_START")) {
			T_START = i + 1;
		}

		if (!strcmp(mc_lumpinfo[i].name, "T_END")) {
			T_END = i;
		}

		if (!strcmp(mc_lumpinfo[i].name, "SCRIPTS")) {
			haveScripts = true;
		}
	}

	if (haveScripts) {
		mc_numlumps -= 1;
	}
}

void convert(char *infn, char *outfn)
{
	int i;
	char Leafs[9] = {"LEAFS"};
	char Endofwad[9] = {"MACROS"};
	int LumpPos = 0x0C;
	int LumpSize = 0x00;

	memset(&mc_wadfile, 0, sizeof(mc_wadfile));
	mc_numlumps = -1;
	T_START = T_END = 0;
	memset(texturehashlist, 0, 2 * 2048 * 2);
	memset(Textures, 0, 2048 * 9);
	memset(Flats, 0, 2048 * 9);
	mapin = mapout = NULL;
	offcnt = 12;

	// Carga TEXTURES.txt y FLATS.txt
	for (i = 0; i < 2048; i++) {
		strncpy(Textures[i], "-", 8);
		strncpy(Flats[i], "-", 8);
	}

	for (i = 0; i < 503; i++) {
		strcpy(&Textures[i][0], &TEXTURES2[i][0]);

		texturehashlist[0][i] = W_HashLumpName(Textures[i]) % 65536;
		texturehashlist[1][i] = i;
	}

	Read_MapWad(infn);

	mapout = fopen(outfn, "wb");
	fwrite(&mc_wadfile, sizeof(mc_wadfile), 1, mapout);

	CopyLump(LABEL);
	CopyLump(THINGS);
	CopyLump(LINEDEFS);
	ConvertSidefs(SIDEDEFS);
	CopyLump(VERTEXES);
	CopyLump(SEGS);
	CopyLump(SSECTORS);
	CopyLump(NODES);
	ConvertSectors(SECTORS);
	CopyLump(REJECT);
	CopyLump(BLOCKMAP);
	CopyLump(LEAFS);
	CopyLump(LIGHTS);
	CopyLump(MACROS);

	for (i = 0; i < mc_numlumps; i++) {
		fwrite(&mc_lumpinfo[i].filepos, sizeof(unsigned int), 1, mapout);
		fwrite(&mc_lumpinfo[i].size, sizeof(unsigned int), 1, mapout);
		fwrite(&mc_lumpinfo[i].name, sizeof(unsigned int) * 2, 1, mapout);
	}

	fseek(mapout, 0, SEEK_SET);
	mc_wadfile.numlumps = mc_numlumps;
	mc_wadfile.infotableofs = offcnt;
	fwrite(&mc_wadfile, sizeof(mc_wadfile), 1, mapout);
	fclose(mapout);

	free(mc_lumpinfo);
	fclose(mapin);
}

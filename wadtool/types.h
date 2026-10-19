typedef struct {
	int filepos;
	int size;
	char name[8];
} lumpinfo_t;

typedef struct {
	char identification[4];
	int numlumps;
	int infotableofs;
} wadinfo_t;

typedef struct {
	short compressed;
	short numpal;
	short width;
	short height;
	uint8_t data[0];
} gfxN64_t;

typedef struct {
	short id;
	short numpal;
	short wshift;
	short hshift;
	uint8_t data[0];
} textureN64_t;

typedef struct  {
	unsigned short tiles; // 0
	short compressed; // 2
	unsigned short cmpsize; // 4
	short xoffs; // 6
	short yoffs; // 8
	unsigned short width; // 10
	unsigned short height; // 12
	unsigned short tileheight; // 14
	uint8_t data[0]; // 16 - all of the sprite data itself
} spriteN64_t;

typedef struct  {
	unsigned short width; // 0
	unsigned short height; // 2
	short xoffs; // 4
	short yoffs; // 6
	uint8_t data[0]; // 8 - all of the sprite data itself
} spriteDC_t;


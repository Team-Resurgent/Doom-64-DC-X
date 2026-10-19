//
// BC5 decompression for normal maps.
// Thanks to erik5249 for putting this together.
// Modified slightly to decompress AND twiddle directly into PVR memory.
//

#include "doomdef.h"

// r_idxs0 and g_idxs0 are integers with the bottom 3 bits representing the
//  palette index of the current pixel
// r0,r1,g0,g1 are the palette endpoints,  indexes 0 and 7 will be replaced
//  with r0/g0 and r1/g1 repspectively
// palette indexes inbetween will be lerped between those
// out is the output pointer :)

#define recip7 0.14285714924335479736328125f

void decode_bm_pixel(uint32_t r_idxs, uint32_t g_idxs, uint8_t r0, uint8_t r1, uint8_t g0, uint8_t g1, uint8_t *out)
{
	uint8_t r_idx = (r_idxs & 0b111);
	uint8_t g_idx = (g_idxs & 0b111);

	float rd7 = ((float)r_idx * recip7);
	float gd7 = ((float)g_idx * recip7);

	float r_res = r0 * (1.0f - rd7) + r1 * rd7;
	float g_res = g0 * (1.0f - gd7) + g1 * gd7;

	uint16_t *outp = (uint16_t *)out;
	*outp = (uint16_t)((((uint8_t)g_res) << 8) | ((uint8_t)r_res));
}

void decode_bm_block(uint8_t *blk, uint8_t *out)
{
	uint8_t r0 = blk[0];
	uint8_t r1 = blk[1];
	uint32_t r_idxs0 = (*(uint16_t *)(blk + 4) << 16) | (*(uint16_t *)(blk + 2));
	uint16_t r_idxs1 = *(uint16_t *)(blk + 6);
	uint8_t g0 = blk[8];
	uint8_t g1 = blk[9];
	uint32_t g_idxs0 = (*(uint16_t *)(blk + 12) << 16) | (*(uint16_t *)(blk + 10));
	uint16_t g_idxs1 = *(uint16_t *)(blk + 14);

	// r_idxs0 and g_idxs0 have the first 10 pixels,
	//  plus 2 bits of the 11th pixel
	// r_idxs0 and g_idxs0 are
	// AA999888777666555444333222111000

	// r_idxs1 and g_idxs1 have last bit of 11th pixel,
	//  plus 3 bits each for the remaining 5 pixels
	// FFFEEEDDDCCCBBBA

	// decompress first two rows, 8 pixels
	for (int y = 0; y < 2; y++) {

		int y_idx = (y & 1) | ((y & 2) << 1);

		for (int x = 0; x < 4; x++) {

			int x_idx = ((x & 1) << 1) | ((x & 2) << 2);

			int idx = (y_idx | x_idx) << 1;

			decode_bm_pixel(r_idxs0, g_idxs0, r0, r1, g0, g1, out + idx);

			r_idxs0 >>= 3;
			g_idxs0 >>= 3;
		}
	}

	// r_idxs0 and g_idxs0 are now
	// xxxxxxxxxxxxxxxxxxxxxxxxAA999888
	// r_idxs1 and g_idxs1 are
	//                 FFFEEEDDDCCCBBBA

	// shift these into place and OR them onto r_idxs,g_idxs
	uint32_t r_idxs1_shifted = (r_idxs1 << 8);
	uint32_t g_idxs1_shifted = (g_idxs1 << 8);
	r_idxs0 |= r_idxs1_shifted;
	g_idxs0 |= g_idxs1_shifted;

	// decompress second two rows, 8 pixels
	for (int y = 2; y < 4; y++) {
		int y_idx = (y & 1) | ((y & 2) << 1);

		for (int x = 0; x < 4; x++) {
			int x_idx = ((x & 1) << 1) | ((x & 2) << 2);

			int idx = (y_idx | x_idx) << 1;

			decode_bm_pixel(r_idxs0, g_idxs0, r0, r1, g0, g1, out + idx);

			r_idxs0 >>= 3;
			g_idxs0 >>= 3;
		}
	}
}

#define TWID_BLOCK_IDX(x) ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ((x & 8) << 3) | ((x & 16) << 4))

#define TWID_BLOCK_IDXS(x, y) (TWID_BLOCK_IDX((y)) | (TWID_BLOCK_IDX((x)) << 1))

// in must be a buffer of width*height bytes, two byte aligned
// out must be a buffer of width*height*2 bytes
void decode_bumpmap(uint8_t *in, uint8_t *out, int width, int height)
{
	int width_in_blocks = width / 4;
	int height_in_blocks = height / 4;

	int min_block_dim = width_in_blocks < height_in_blocks ? width_in_blocks : height_in_blocks;

	int block_dim_mask = min_block_dim - 1;

	int in_idx = 0;
	for (int block_y = 0; block_y < height_in_blocks; block_y++) {
		int mask_block_y = block_y & block_dim_mask;
		int rem_block_y = block_y / min_block_dim;

		for (int block_x = 0; block_x < width_in_blocks; block_x++) {
			int mask_block_x = block_x & block_dim_mask;
			int rem_block_x = block_x / min_block_dim;
			int rem_block_coord = rem_block_x + rem_block_y;
			int scaled_rem = rem_block_coord * min_block_dim * min_block_dim;
			int twid_low_bits_block_idx = TWID_BLOCK_IDXS(mask_block_x, mask_block_y);
			int out_idx = (twid_low_bits_block_idx + scaled_rem) << 5;

#if RANGECHECK
			if (in_idx > ((width * height) - 1))
				I_Error("decode_bumpmap input data overflow");
#endif

			decode_bm_block(&in[in_idx], &out[out_idx]);

			in_idx += 16;
		}
	}
}

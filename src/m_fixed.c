
/* m_fixed.c -- fixed point implementation */

#include "i_main.h"
#include "doomdef.h"
#include "p_spec.h"
#include "r_local.h"

/*
===============
=
= FixedDiv2
=
===============
*/

// now only used by FixedDiv(a,b)
static fixed_t FixedDiv2(register fixed_t a, register fixed_t b)
{
	int64_t result = ((int64_t)a << FRACBITS) / (int64_t)b;

	return (fixed_t)result;
}

/*
===============
=
= FixedDiv
=
===============
*/

// now only used in level setup
fixed_t FixedDiv(fixed_t a, fixed_t b)
{
	fixed_t aa, bb;
	unsigned c;
	int sign;

	sign = a^b;

	if (a < 0)
		aa = -a;
	else
		aa = a;

	if (b < 0)
		bb = -b;
	else
		bb = b;

	if ((signed)((unsigned)(aa >> 14)) >= bb) {
		if (sign < 0)
			c = MININT;
		else
			c = MAXINT;
	} else {
		c = (fixed_t)FixedDiv2(a, b);
	}

	return c;
}

/*
===============
=
= FixedDivFloat
=
===============
*/

// used anywhere a FixedDiv occurs outside of level setup (BSP traversal mostly)
// significantly faster than int divide and *just about* accurate enough for gameplay
fixed_t FixedDivFloat(register fixed_t a, register fixed_t b)
{
	float af = (float)a;
	float bf = (float)b;
	float cf = af / bf;
	return (fixed_t)(cf * 65536.0f);
}

/*
===============
=
= FixedMul
=
===============
*/

// this compiles into a mult + xtrct, not awful
fixed_t FixedMul(fixed_t a, fixed_t b)
{
	int64_t result = ((int64_t)a * (int64_t)b) >> FRACBITS;
	return (fixed_t)result;
}
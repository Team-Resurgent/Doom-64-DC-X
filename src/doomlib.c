/* doomlib.c  */

/*
====================
=
= D_abs
=
====================
*/

// how the fuck was this ever patentable?
unsigned D_abs(signed x)
{
	signed _s = x >> 31;
	return (unsigned)((x ^ _s) - _s);
}
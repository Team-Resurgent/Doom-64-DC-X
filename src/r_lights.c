#include "doomdef.h"
#include "r_local.h"
#include <math.h>

// array of lights generated in r_phase1.c
extern projectile_light_t __attribute__((aligned(32))) projectile_lights[NUM_DYNLIGHT];
// packed bumpmap parameters
extern uint32_t boargb;

#define BUMPYINT 127
#define K1 (255 - BUMPYINT)

// branch-free, division-free atan2f approximation
// copysignf has a branch
static inline float bump_atan2f(const float y, const float x)
{
	float abs_y = fabsf(y) + 1e-10f;
	float absy_plus_absx = abs_y + fabsf(x);
	float inv_absy_plus_absx = approx_recip(absy_plus_absx);
	float angle = halfpi_i754 - copysignf(quarterpi_i754, x);
	float r = (x - copysignf(abs_y, x)) * inv_absy_plus_absx;
	angle += (0.1963f * r * r - 0.9817f) * r;
	return copysignf(angle, y);
}

static void assign_lightcolor(d64ListVert_t *v)
{
	if (!v->lit)
		return;

	uint32_t cocol = v->v->oargb;
	float maxrgb = 1.0f;
	float recipmaxcomponent;

	// vertex sector light color
	float vsr = (float)((cocol >> 16) & 0xff) * recip255;
	float vsg = (float)((cocol >> 8) & 0xff) * recip255;
	float vsb = (float)((cocol) & 0xff) * recip255;

	// blend projectile light with dynamic sector light
	vsr += v->r;
	vsg += v->g;
	vsb += v->b;

	// scale blended light down
	// clamping individual components gives incorrect colors
	if (vsr > maxrgb)
		maxrgb = vsr;
	if (vsg > maxrgb)
		maxrgb = vsg;
	if (vsb > maxrgb)
		maxrgb = vsb;

	recipmaxcomponent = approx_recip(maxrgb) * COMPONENT_INTENSITY;

	vsr *= recipmaxcomponent;
	vsg *= recipmaxcomponent;
	vsb *= recipmaxcomponent;

	// any contribution from projectile lights
	// we overwrite the vertex oargb with the new blended color
	v->v->oargb = 0xff000000 | ((int)(vsr) << 16) | ((int)(vsg) << 8) | ((int)(vsb));
}

/* static float MagnitudeFast(float x, float y, float z)
{
    float ax = fabsf(x);
    float ay = fabsf(y);
    float az = fabsf(z);
    return (ax+ay+az)*0.6f;
} */

static unsigned plit = 0;

// calculate light intensity on array of vertices
static void light_vert(d64ListVert_t *v, projectile_light_t *l, unsigned c)
{
	// for every vertex in input array
	for (unsigned i = 0; i < c; i++) {
		// calculate direction vector from input light to current vertex
		float dx = l->x - v->v->x;
		float dy = l->y - v->v->y;
		float dz = l->z - v->v->z;

		// magnitude of light direction vector
		float light_dist;// = MagnitudeFast(dx,dy,dz);
		vec3f_length(dx, dy, dz, light_dist);

		float light_distrad_diff = l->radius - light_dist;

		// light distance is less than light radius
		if (light_distrad_diff > 0) {
			// see r_phase3.c for R_TransformProjectileLights
			// distance field holds inverse of radius
			float light_scale = light_distrad_diff * l->distance;

			// accumulate light contributions in vertex
			// linear attentuation
			
			v->r += l->r * light_scale;
			v->g += l->g * light_scale;
			v->b += l->b * light_scale;

			// indicate any light was applied to this vertex
			v->lit = 1;
			plit = 1;
		}

		v++;
	}
}

// 2 dimensional dot product of (x1,z1) and (x2,z2)
// compared to vec3f_dot, leads to better code generation in loops
// no reload of constant (x2,z2) every loop iteration
#define dot2d(x1, z1, x2, z2, r) (r = ((x1) * (x2) + (z1) * (z2)))

// calculates per-vertex light contributions and normal mapping parameters
// for a Doom wall polygon
void light_wall_hasbump(d64Poly_t *p, unsigned lightmask)
{
	unsigned i;
	// accumulated light direction vector
	// not averaged because it will be normalized before use
	float acc_ldx = 0.0f;
	float acc_ldy = 0.0f;
	float acc_ldz = 0.0f;

	// 3d center of wall
	float center_x = (p->dVerts[0].v->x + p->dVerts[3].v->x) * 0.5f;
	float center_y = (p->dVerts[0].v->y + p->dVerts[3].v->y) * 0.5f;
	float center_z = (p->dVerts[0].v->z + p->dVerts[3].v->z) * 0.5f;

	unsigned first_idx = (lightmask >> 24) & 0xf;
	unsigned last_idx = (lightmask >> 16) & 0xf;

	plit = 0;

	// for every dynamic light that was generated this frame
	for (i = first_idx; i <= last_idx; i++) {
		if (((lightmask >> i) & 1)) {
			projectile_light_t *pl = &projectile_lights[i];
			float dotprod;
			// calculate direction vector between light and center of wall
			float dx = pl->x - center_x;
			float dz = pl->z - center_z;

			// light direction isn't normalized
			// just need sign of dotprod, so that is ok
			// two multiplies and an add, with the added bonus of NOT RELOADING `normx` and `normz`
			// for each light loop iteration
			dot2d(dx,dz,global_render_state.normx,global_render_state.normz,dotprod);
			// light is on correct side of wall
			if (dotprod > 0.0f) {
				float dy = pl->y - center_y;
				// accumulate light direction vectors
				acc_ldx += dx;
				acc_ldy += dy;
				acc_ldz += dz;
				// calculate per-vertex light contribution from current light
				light_vert(p->dVerts, pl, 4);
			}
		}
	}

	if (plit) {
		// normalize accumulated light direction vector over surface
		vec3f_normalize(acc_ldx, acc_ldy, acc_ldz);

		// for every vertex in wall poly
		// combine per-vertex dynamic light with per-vertex static sector lighting
		for (i = 0; i < 4; i++)
			assign_lightcolor(&p->dVerts[i]);

		// the following is a simplifcation of calculating the light direction
		// (elevation and azimuth angles) for the wall
		// because walls in Doom are always perfectly vertical,
		// there is no need for tangent/bitangent calculations
		// we can just rotate the wall and the light position in the x,z plane
		// so they are oriented with (x,y) plane (with surface normal (0,0,1))
		// and the calculations are simpler

		// light direction vector after re-orienting to surface normal
		float rotated_ldx;
		float rotated_ldy;
		float rotated_ldz;

		// bump-mapping parameters
		// elevation (height angle over surface) (aka T)
		float elevation;
		// azimuth (rotation angle around surface) (aka Q)
		float azimuth;
		// (sin(elevation) * bumpiness) * 255
		int K2;
		// (cos(elevation) * bumpiness) * 255
		int K3;
		// (azimuth / 2pi) * 255
		int Q;
		// sin / cos of elevation
		float sin_el;
		float cos_el;

		// 2d length of light direction vector along surface plane
		float ld_xy_len;

		// at this stage of pipeline, (x,y) plane is (left +/right-, up +/down -)
		// flip y so that negative is up instead of down
		rotated_ldy = -acc_ldy;

		// walls are a 2D construct in Doom map, in x/y plane, height comes from sector
		// always perfectly vertical, never any slope
		//
		//   v.x,v.y     v.x,v.y
		// 2d (y,x) -> 3d (z,x)
		//
		// unit normal (1,0) is a wall with angle 0 in world space
		// unit normal (normz,normx) for an arbitrary wall in world space
		//
		// for two points v1, v2
		// angle of v2 relative to v1 = arctan(v2.y/v2.x) - arctan(v1.y/v1.x)
		//
		// wall_theta	=	arctan(normx/normz) - arctan(0/1);
		//				=	arctan(sinf(wall_theta) / cosf(wall_theta)) - arctan(0/1)
		//				=	arctan(nx/nz) - 0
		//				=	arctan(sinf(wall_theta) / cosf(wall_theta)) - 0
		//				=	arctan(nx/nz)
		//				=	arctan(sinf(wall_theta) / cosf(wall_theta))
		//				=	atan2(sinf(wall_theta), cosf(wall_theta))
		//
		// substitute variables
		//
		// cosf(wall_theta) = "v2.x" = normz
		// sinf(wall_theta) = "v2.y" = normx
		//
		// 2d rotation of (x,y) by angle theta
		// x' = x*cos(theta) - y*sin(theta)
		// y' = x*sin(theta) + y*cos(theta)
		//
		// 2d rotation in x,z plane by angle -(wall_theta)
		// replace variables
		// x -> z
		// y -> -x (flip x so negative is left instead of right)
		// theta = -wall_theta
		// cos(wall_theta) = normz
		// sin(wall_theta) = normx
		//
		// and trig identities
		// cos(-theta) = cos(theta)
		// sin(-theta) = -sin(theta)
		//
		// z' = z*cos(-wall_theta) - -x*sin(-wall_theta)
		// x' = z*sin(-wall_theta) + -x*cos(-wall_theta)
		//
		// z' = z*cos(wall_theta) + -x*-sin(wall_theta)
		// x' = z*-sin(wall_theta) + -x*cos(wall_theta)
		//
		// z' = z*cos(wall_theta) + x*sin(wall_theta)
		// x' = z*sin(wall_theta) - x*cos(wall_theta)
		//
		// z' = z*normz + x*normx
		// x' = z*normx - x*normz
		rotated_ldx = (acc_ldz * global_render_state.normx) - (acc_ldx * global_render_state.normz);
		rotated_ldz = (acc_ldz * global_render_state.normz) + (acc_ldx * global_render_state.normx);

		// get the length of the normalized light direction vector
		// over the surface of the wall
		vec3f_length(rotated_ldx, rotated_ldy, 0.0f, ld_xy_len);

		// atan(lightdir y/lightdir x)
		// is the rotation angle of the normalized light direction vector
		// over the surface of (x,y)-aligned wall
		// then offset by 180 degrees to keep it in range [0,2pi)
		azimuth = F_PI + bump_atan2f(rotated_ldy, rotated_ldx);

		// atan(z / length(x,y)) gives "height" of the light direction vector
		// over the surface of the wall
		// we just need the magnitude of the angle
		//
		// we limit the lowest elevation angle over the wall
		// to pi/4
		// this eliminates ugly artifacts
		// when a light is close to a wall in the "height" angle dimension
		// but far in distance
		elevation = fmaxf(quarterpi_i754, fabsf(bump_atan2f(rotated_ldz, ld_xy_len)));

		// FSCA wrapper, sin(el)/cos(el) approximations in one call
		sin_el = sinf(elevation);
		cos_el = cosf(elevation);

		// scale bumpmap parameters
		K2 = (int)(sin_el * BUMPYINT);
		K3 = (int)(cos_el * BUMPYINT);
		Q = (int)(azimuth * 40.58451080322265625f); // 255.0f / (2.0f * F_PI)
		// pack bumpmap parameters
		boargb = (K1 << 24) | (K2 << 16) | (K3 << 8) | Q;
	}
}

// calculates per-vertex light contributions
// for a Doom wall polygon
// with no normal mapping

void light_wall_nobump(d64Poly_t *p, unsigned lightmask)
{
	unsigned i;
	// 3d center of wall
	float center_x = p->dVerts[0].v->x;
	float center_z = p->dVerts[0].v->z;

	unsigned first_idx = (lightmask >> 24) & 0xf;
	unsigned last_idx = (lightmask >> 16) & 0xf;
	plit = 0;

	for (i = first_idx; i <= last_idx; i++) {
		if ((lightmask >> i) & 1) {
			projectile_light_t *pl = &projectile_lights[i];
			float dotprod;
			// calculate light direction vector between light and center of wall
			float dx = pl->x - center_x;
			float dz = pl->z - center_z;

			// light direction isn't normalized
			// just need sign of dotprod, so that is ok
			// two multiplies and an add, with the added bonus of NOT RELOADING `normx` and `normz`
			// for each light loop iteration
			dot2d(dx,dz,global_render_state.normx,global_render_state.normz,dotprod);

			// light is on correct side of wall
			if (dotprod > 0.0f) {
				// calculate per-vertex light contribution from current light
				light_vert(p->dVerts, pl, 4);
			}
		}
	}

	if (plit) {
		// for every vertex in wall poly
		for (i = 0; i < 4; i++) {
			// combine per-vertex dynamic light with per-vertex static sector lighting
			assign_lightcolor(&p->dVerts[i]);
		}
	}
}

// calculates per-vertex light contributions
// for a Doom thing (monster/decoration sprite)
// with no normal mapping
void light_thing(d64Poly_t *p, unsigned lightmask)
{
	unsigned i;
	unsigned first_idx = (lightmask >> 24) & 0xf;
	unsigned last_idx = (lightmask >> 16) & 0xf;
	plit = 0;

	// for every dynamic light that was generated this frame
	for (i = first_idx; i <= last_idx; i++) {
		if ((lightmask >> i) & 1) {
			projectile_light_t *pl = &projectile_lights[i];
			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, pl, 4);
		}
	}

	if (plit) {
		// for every vertex in thing poly
		// combine per-vertex dynamic light with per-vertex static sector lighting
		for (i = 0; i < 4; i++)
			assign_lightcolor(&p->dVerts[i]);
	}
}

extern float pi_sub_viewangle;

// calculates per-vertex light contributions and normal mapping parameters
// for a triangle belonging to a Doom plane (floor/ceiling)
void light_plane_hasbump(d64Poly_t *p, unsigned lightmask)
{
	unsigned i;
	float acc_ldx = 0;
	float acc_ldy = 0;
	float acc_ldz = 0;

	// 3d center of floor/ceiling triangle
	float center_x = (p->dVerts[0].v->x + p->dVerts[1].v->x + p->dVerts[2].v->x) * 0.333333f;
	float center_z = (p->dVerts[0].v->z + p->dVerts[1].v->z + p->dVerts[2].v->z) * 0.333333f;
	// planes are horizontally flat, y is the same across all 3 verts
	float center_y = p->dVerts[0].v->y;

	unsigned first_idx = (lightmask >> 24) & 0xf;
	unsigned last_idx = (lightmask >> 16) & 0xf;
	plit = 0;

	// for every dynamic light that was generated this frame
	for (i = first_idx; i <= last_idx; i++) {
		if ((lightmask >> i) & 1) {
			projectile_light_t *pl = &projectile_lights[i];
			float dy = pl->y - center_y;

			if (global_render_state.in_floor == 2) {
				// ceiling
				if (dy >= 0.0f)
					continue;
			} else {
				if (dy <= 0.0f)
					continue;
			}

			float dx = pl->x - center_x;
			float dz = pl->z - center_z;

			acc_ldx += dx;
			acc_ldy += dy;
			acc_ldz += dz;
			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, pl, 3);
		}
	}

	if (plit) {
		// normalize accumulated light direction vector over surface
		vec3f_normalize(acc_ldx, acc_ldy, acc_ldz);

		// for every vertex in plane poly
		// combine per-vertex dynamic light with per-vertex static sector lighting
		for (i = 0; i < 3; i++)
			assign_lightcolor(&p->dVerts[i]);

		// the following is a simplifcation of calculating the light direction
		// (elevation and azimuth angles) for a triangle in plane (floor/ceiling)
		// because planes in Doom are always perfectly horiztonal,
		// there is no need for tangent/bitangent calculations
		// and they are even simpler than walls, because their normals are always
		// either (0,1,0) or (0,-1,0)
		// average light direction in the x,z plane is sufficient to calculate
		// azimuth
		// average light direction along the y axis is sufficient to calculate
		// elevation
		//if (bump_applied) {
		// bump-mapping parameters
		// elevation (height angle over surface) (aka T)
		float elevation;
		// azimuth (rotation angle around surface) (aka Q)
		float azimuth;
		// (sin(elevation) * bumpiness) * 255
		int K2;
		// (cos(elevation) * bumpiness) * 255
		int K3;
		// (azimuth / 2pi) * 255
		int Q;
		// sin / cos of elevation
		float sin_el;
		float cos_el;

		// we flip x so negative is left, positive is right
		// operating in un-transformed world space
		// atan(z/x) gets us rotation angle in (x,z) plane of the triangle
		// then offset by 180 degrees to bring into range [0,2pi]
		// much like we "unrotate" the walls to orient them for normal map calcs
		// we need to "unrotate" the floor with respect to the player's view angle
		// so also subtract viewangle
		// this is all done in the plane_atan2f call
		// azimuth = F_PI + bump_atan2f(acc_ldz, -acc_ldx);
		azimuth = bump_atan2f(acc_ldz, -acc_ldx) - pi_sub_viewangle;

		if (azimuth < 0.0f)
			azimuth += twopi_i754;

		// elevation above floor
		// directly overhead is pi/2
		// scale that by the normalized y component of light direction vector
		// good "approximation" of elevation angle
		//
		// we limit the lowest elevation angle over the floor/ceiling
		// to pi/4
		// this eliminates ugly artifacts
		// when a light is close to triangle surface in the "height" angle dimension
		// but far from triangle in distance
		elevation = fmaxf(quarterpi_i754, fabsf(halfpi_i754 * acc_ldy));

		// FSCA wrapper, sin(el)/cos(el) approximations in one call
		sin_el = sinf(elevation);
		cos_el = cosf(elevation);

		// scale bumpmap parameters
		K2 = (int)(sin_el * BUMPYINT);
		K3 = (int)(cos_el * BUMPYINT);
		Q = (int)(azimuth * 40.58451080322265625f); // 255.0f / (2.0f * F_PI)
		// pack bumpmap parameters
		boargb = (K1 << 24) | (K2 << 16) | (K3 << 8) | Q;
	}
}

// calculates per-vertex light contributions
// for a triangle belonging to a Doom plane (floor/ceiling)
// with no normal mapping
void light_plane_nobump(d64Poly_t *p, unsigned lightmask)
{
	unsigned i;

	unsigned first_idx = (lightmask >> 24) & 0xf;
	unsigned last_idx = (lightmask >> 16) & 0xf;
	// planes are horizontally flat, y is the same across all 3 verts
	float center_y = p->dVerts[0].v->y;

	plit = 0;
	// for every dynamic light that was generated this frame
	for (i = first_idx; i <= last_idx; i++)	{
		if ((lightmask >> i) & 1) {
			projectile_light_t *pl = &projectile_lights[i];
			if (global_render_state.in_floor == 2) {
				// ceiling
				if (pl->y >= center_y)
					continue;
			} else {
				if (pl->y <= center_y)
					continue;
			}

			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, pl, 3);
		}
	}

	if (plit) {
		// for every vertex in plane poly
		// combine per-vertex dynamic light with per-vertex static sector lighting
		for (i = 0; i < 3; i++)
			assign_lightcolor(&p->dVerts[i]);
	}
}

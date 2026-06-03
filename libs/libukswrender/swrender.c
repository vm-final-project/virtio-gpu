#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <uk/swrender.h>

/* ── framebuffer helpers ──────────────────────────────────────────────── */

int uk_sw_framebuf_alloc(struct uk_sw_framebuf *fb, uint32_t w, uint32_t h)
{
	if (!fb || !w || !h) return -1;
	fb->pixels    = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
	if (!fb->pixels) return -12; /* -ENOMEM */
	fb->width     = w;
	fb->height    = h;
	fb->stride_px = w;
	return 0;
}

void uk_sw_framebuf_free(struct uk_sw_framebuf *fb)
{
	if (!fb) return;
	free(fb->pixels);
	memset(fb, 0, sizeof(*fb));
}

/* Fast solid-colour fill. Compilers vectorise this loop to AVX2 / NEON
 * 32-byte stores when -O2 or higher is in effect. The scalar fallback is
 * still correct (any clang/gcc since 8 emits the SIMD variant for x86_64). */
void uk_sw_framebuf_clear(const struct uk_sw_framebuf *fb, uint32_t color)
{
	if (!fb || !fb->pixels) return;
	uint32_t *p = fb->pixels;
	size_t n = (size_t)fb->height * fb->stride_px;
	if (color == 0u) {
		__builtin_memset(p, 0, n * sizeof(uint32_t));
		return;
	}
	for (size_t i = 0; i < n; i++)
		p[i] = color;
}

uint32_t uk_sw_framebuf_crc(const struct uk_sw_framebuf *fb)
{
	uint32_t h = 2166136261u;
	const uint8_t *p = (const uint8_t *)fb->pixels;
	size_t n = (size_t)fb->height * fb->stride_px * 4u;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
	return h;
}


/* Per-row vertical gradient. The hot inner loop is a single-colour run, so
 * we precompute the colour for the row and then run a tight 32-bit fill,
 * which clang vectorises to 256-bit stores on x86_64. Per-pixel floating-point
 * is hoisted out of the inner loop entirely. */
void uk_sw_draw_gradient(const struct uk_sw_framebuf *fb,
                          uint32_t top_color, uint32_t bot_color)
{
	if (!fb || !fb->pixels || !fb->height || !fb->width) return;
	const uint8_t tr = (top_color >> 16) & 0xff;
	const uint8_t tg = (top_color >> 8)  & 0xff;
	const uint8_t tb =  top_color        & 0xff;
	const int dr = (int)((bot_color >> 16) & 0xff) - (int)tr;
	const int dg = (int)((bot_color >>  8) & 0xff) - (int)tg;
	const int db = (int) (bot_color        & 0xff) - (int)tb;
	const float inv = 1.0f / (float)(fb->height > 1 ? fb->height - 1u : 1u);
	for (uint32_t y = 0; y < fb->height; y++) {
		float t = (float)y * inv;
		uint32_t r = (uint32_t)((int)tr + (int)(t * (float)dr)) & 0xffu;
		uint32_t g = (uint32_t)((int)tg + (int)(t * (float)dg)) & 0xffu;
		uint32_t b = (uint32_t)((int)tb + (int)(t * (float)db)) & 0xffu;
		uint32_t c = 0xff000000u | (r << 16) | (g << 8) | b;
		uint32_t *row = fb->pixels + (size_t)y * fb->stride_px;
		for (uint32_t x = 0; x < fb->width; x++)
			row[x] = c;
	}
}

/* Barycentric rasterizer — no depth buffer, overwrites pixels. */
void uk_sw_draw_triangle(const struct uk_sw_framebuf *fb,
                          float x0, float y0, float x1, float y1,
                          float x2, float y2, uint32_t color)
{
	if (!fb || !fb->pixels) return;
	int minx = (int)fminf(fminf(x0, x1), x2);
	int miny = (int)fminf(fminf(y0, y1), y2);
	int maxx = (int)fmaxf(fmaxf(x0, x1), x2) + 1;
	int maxy = (int)fmaxf(fmaxf(y0, y1), y2) + 1;
	if (minx < 0) minx = 0;
	if (miny < 0) miny = 0;
	if (maxx > (int)fb->width)  maxx = (int)fb->width;
	if (maxy > (int)fb->height) maxy = (int)fb->height;

	float denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
	if (fabsf(denom) < 1e-6f) return;

	for (int py = miny; py < maxy; py++) {
		for (int px = minx; px < maxx; px++) {
			float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
			float w0 = ((y1 - y2) * (fx - x2) + (x2 - x1) * (fy - y2)) / denom;
			float w1 = ((y2 - y0) * (fx - x2) + (x0 - x2) * (fy - y2)) / denom;
			float w2 = 1.0f - w0 - w1;
			if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
				fb->pixels[py * (int)fb->stride_px + px] = color;
		}
	}
}

/* ── 3-D math ─────────────────────────────────────────────────────────── */

typedef struct { float m[4][4]; } Mat4;
typedef struct { float x, y, z, w; } Vec4;

static Mat4 mat4_identity(void)
{
	Mat4 r; memset(&r, 0, sizeof(r));
	r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
	return r;
}

static Vec4 mat4_mul_vec4(const Mat4 *m, Vec4 v)
{
	Vec4 o;
	o.x = m->m[0][0]*v.x + m->m[0][1]*v.y + m->m[0][2]*v.z + m->m[0][3]*v.w;
	o.y = m->m[1][0]*v.x + m->m[1][1]*v.y + m->m[1][2]*v.z + m->m[1][3]*v.w;
	o.z = m->m[2][0]*v.x + m->m[2][1]*v.y + m->m[2][2]*v.z + m->m[2][3]*v.w;
	o.w = m->m[3][0]*v.x + m->m[3][1]*v.y + m->m[3][2]*v.z + m->m[3][3]*v.w;
	return o;
}

static Mat4 mat4_mul(const Mat4 *a, const Mat4 *b)
{
	Mat4 r; memset(&r, 0, sizeof(r));
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				r.m[i][j] += a->m[i][k] * b->m[k][j];
	return r;
}

static Mat4 mat4_rot_x(float a)
{
	Mat4 m = mat4_identity();
	m.m[1][1] =  cosf(a); m.m[1][2] = -sinf(a);
	m.m[2][1] =  sinf(a); m.m[2][2] =  cosf(a);
	return m;
}
static Mat4 mat4_rot_y(float a)
{
	Mat4 m = mat4_identity();
	m.m[0][0] =  cosf(a); m.m[0][2] =  sinf(a);
	m.m[2][0] = -sinf(a); m.m[2][2] =  cosf(a);
	return m;
}
static Mat4 mat4_rot_z(float a)
{
	Mat4 m = mat4_identity();
	m.m[0][0] =  cosf(a); m.m[0][1] = -sinf(a);
	m.m[1][0] =  sinf(a); m.m[1][1] =  cosf(a);
	return m;
}

static Mat4 mat4_perspective(float fovy, float aspect, float near, float far)
{
	Mat4 m; memset(&m, 0, sizeof(m));
	float f = 1.0f / tanf(fovy * 0.5f);
	m.m[0][0] = f / aspect;
	m.m[1][1] = f;
	m.m[2][2] = (far + near) / (near - far);
	m.m[2][3] = (2.0f * far * near) / (near - far);
	m.m[3][2] = -1.0f;
	return m;
}

/* ── cube geometry ────────────────────────────────────────────────────── */

/* 8 vertices of a unit cube centred at origin. */
static const float cube_verts[8][3] = {
	{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f},
	{ 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
	{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f},
	{ 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
};

/* 12 triangles (2 per face), each with a distinct colour. */
static const int cube_tris[12][3] = {
	{0,1,2},{0,2,3}, /* back   */
	{4,6,5},{4,7,6}, /* front  */
	{0,4,5},{0,5,1}, /* bottom */
	{2,6,7},{2,7,3}, /* top    */
	{0,3,7},{0,7,4}, /* left   */
	{1,5,6},{1,6,2}, /* right  */
};
static const uint32_t face_colors[12] = {
	0xff4040ff, 0xff4040ff,  /* back:   blue  */
	0xff40ff40, 0xff40ff40,  /* front:  green */
	0xffff4040, 0xffff4040,  /* bottom: red   */
	0xffffff40, 0xffffff40,  /* top:    yellow*/
	0xffff40ff, 0xffff40ff,  /* left:   magenta */
	0xff40ffff, 0xff40ffff,  /* right:  cyan  */
};

void uk_sw_cube_init(struct uk_sw_cube_state *st,
                     float dx, float dy, float dz)
{
	if (!st) return;
	st->angle_x = 0.0f; st->angle_y = 0.0f; st->angle_z = 0.0f;
	st->delta_x = dx;   st->delta_y = dy;   st->delta_z = dz;
}

void uk_sw_cube_render(struct uk_sw_cube_state *st,
                       const struct uk_sw_framebuf *fb)
{
	if (!st || !fb || !fb->pixels) return;

	uk_sw_draw_gradient(fb, 0xff202040u, 0xff101020u);

	Mat4 rx  = mat4_rot_x(st->angle_x);
	Mat4 ry  = mat4_rot_y(st->angle_y);
	Mat4 rz  = mat4_rot_z(st->angle_z);
	Mat4 rot = mat4_mul(&rx, &ry);
	rot = mat4_mul(&rot, &rz);

	/* Translate 2.5 units away from camera. */
	Mat4 trans = mat4_identity();
	trans.m[2][3] = -2.5f;

	Mat4 mv   = mat4_mul(&trans, &rot);
	float asp = (float)fb->width / (float)fb->height;
	Mat4 proj = mat4_perspective(1.0472f /* 60° */, asp, 0.1f, 100.0f);
	Mat4 mvp  = mat4_mul(&proj, &mv);

	float hw = (float)fb->width  * 0.5f;
	float hh = (float)fb->height * 0.5f;

	for (int t = 0; t < 12; t++) {
		Vec4 p[3];
		float sx[3], sy[3];

		for (int v = 0; v < 3; v++) {
			p[v].x = cube_verts[cube_tris[t][v]][0];
			p[v].y = cube_verts[cube_tris[t][v]][1];
			p[v].z = cube_verts[cube_tris[t][v]][2];
			p[v].w = 1.0f;
			p[v] = mat4_mul_vec4(&mvp, p[v]);
		}
		/* Skip if all vertices are behind the camera (clip_w = -z_eye <= 0). */
		if (p[0].w <= 0.0f && p[1].w <= 0.0f && p[2].w <= 0.0f) continue;

		for (int v = 0; v < 3; v++) {
			float iw = (fabsf(p[v].w) > 1e-6f) ? 1.0f / p[v].w : 0.0f;
			sx[v] = (p[v].x * iw + 1.0f) * hw;
			sy[v] = (1.0f - p[v].y * iw) * hh;
		}

		/* Simple back-face culling in screen space. */
		float area = (sx[1]-sx[0])*(sy[2]-sy[0]) - (sx[2]-sx[0])*(sy[1]-sy[0]);
		if (area >= 0.0f) continue;

		uk_sw_draw_triangle(fb, sx[0], sy[0], sx[1], sy[1], sx[2], sy[2],
		                    face_colors[t]);
	}

	st->angle_x += st->delta_x;
	st->angle_y += st->delta_y;
	st->angle_z += st->delta_z;
}

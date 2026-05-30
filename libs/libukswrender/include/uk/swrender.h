#pragma once
#include <stdint.h>
#include <stddef.h>

struct uk_sw_framebuf {
	uint32_t *pixels;
	uint32_t  width;
	uint32_t  height;
	uint32_t  stride_px;
};

struct uk_sw_cube_state {
	float angle_x, angle_y, angle_z;
	float delta_x, delta_y, delta_z;
};

int      uk_sw_framebuf_alloc(struct uk_sw_framebuf *fb, uint32_t w, uint32_t h);
void     uk_sw_framebuf_free(struct uk_sw_framebuf *fb);
void     uk_sw_framebuf_clear(const struct uk_sw_framebuf *fb, uint32_t color);
uint32_t uk_sw_framebuf_crc(const struct uk_sw_framebuf *fb);
void     uk_sw_cube_init(struct uk_sw_cube_state *st, float dx, float dy, float dz);
void     uk_sw_cube_render(struct uk_sw_cube_state *st, const struct uk_sw_framebuf *fb);
void     uk_sw_draw_triangle(const struct uk_sw_framebuf *fb,
                              float x0, float y0, float x1, float y1,
                              float x2, float y2, uint32_t color);
void     uk_sw_draw_rect(const struct uk_sw_framebuf *fb,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t color);
void     uk_sw_draw_gradient(const struct uk_sw_framebuf *fb,
                              uint32_t top_color, uint32_t bot_color);

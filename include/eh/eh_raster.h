/* Minimal z-buffered, textured triangle rasterizer for the torus knot.
 *
 * Single-precision float: the RP2350's Cortex-M33 cores have a hardware
 * single-precision FPU, so float is a legitimate device path here; fixed
 * point remains an option if measurements on hardware call for it. */
#ifndef EH_RASTER_H
#define EH_RASTER_H

#include <stdint.h>

#include "eh_types.h"

/* Screen-space vertex: x, y in framebuffer pixels; z in [0, 1] with smaller
 * nearer; u, v texture coordinates in [0, 1). */
typedef struct {
    float x, y, z, u, v;
} eh_vtx;

/* Rendering is banded: the depth buffer covers only rows [y0, y1), so each
 * core needs a band-sized depth buffer rather than a full-frame one. */
typedef struct {
    eh_rgb565 *fb;
    uint16_t *zb;         /* (y1 - y0) * EH_FB_W depths for rows [y0, y1); 0xFFFF farthest */
    const eh_rgb565 *tex; /* (1 << tex_shift) square, row-major */
    int tex_shift;
    int y0, y1; /* only rows [y0, y1) are written */
    struct eh_raster_stats *stats; /* optional work counters (diagnostics) */
} eh_raster_target;

/* Accumulated by eh_raster_tri when the target has stats: rows with a
 * non-empty run, pixels tested, and tested pixels that failed depth. */
typedef struct eh_raster_stats {
    uint32_t rows, tested, depth_failed;
} eh_raster_stats;

void eh_raster_clear_depth(uint16_t *zb, int rows);

/* A triangle is front-facing when (b - a) x (c - a) > 0 in framebuffer
 * coordinates (x right, y down), i.e. clockwise as seen on screen; others are
 * culled. Pixels are covered when their centre is inside or on an edge.
 * Returns the number of pixels that passed the depth test. */
int eh_raster_tri(const eh_raster_target *t, const eh_vtx *a, const eh_vtx *b,
                   const eh_vtx *c);

/* Unshaded line for wireframe mode; no depth test. */
void eh_raster_line(const eh_raster_target *t, int x0, int y0, int x1, int y1,
                    eh_rgb565 color);

#endif

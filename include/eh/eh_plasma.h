/* Plasma: the measured performance prototype.
 *
 * Integer-only (no float; no division in the per-pixel loop) so the inner
 * loop is the shape a Cortex-M33 core would run. Every table it touches is
 * const, i.e. flash on a device; the only RAM is the caller's framebuffer
 * and ~1.1 KB of stack. */
#ifndef EH_PLASMA_H
#define EH_PLASMA_H

#include <stdint.h>

#include "eh_types.h"

/* Render rows [y0, y1) of the plasma for time t_ms using palette
 * (0..EH_PALETTE_COUNT-1). With clip_circle set, only pixels inside the round
 * display are written; others keep their previous value. Calls over disjoint
 * row bands compose bit-exactly into a full-frame render, which is the
 * contract a two-core split relies on. */
void eh_plasma_render(eh_rgb565 *fb, uint32_t t_ms, unsigned palette, int y0, int y1,
                      int clip_circle);

#endif

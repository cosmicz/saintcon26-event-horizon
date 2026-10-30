/* Build-time quality knobs and the frame-partition plan.
 *
 * Defaults are the native-resolution configuration. Each knob that costs RAM
 * can be lowered from CMake (-DEH_DEFINES="EH_TERRAIN_SHIFT=7;...") so its
 * memory and its visible effect can be compared directly; tools/ablate.py
 * does that. */
#ifndef EH_CONFIG_H
#define EH_CONFIG_H

/* Torus knot tube tessellation: segments along the knot and around the tube.
 * 320 along the knot was indistinguishable at native scale and cost ~25% of
 * the scene's estimated device time (2026-10-29), so 160. */
#ifndef EH_TORUS_NU
#define EH_TORUS_NU 160
#endif
#ifndef EH_TORUS_NV
#define EH_TORUS_NV 16
#endif

/* Chrome environment map: (1 << EH_ENV_SHIFT) texels square. */
#ifndef EH_ENV_SHIFT
#define EH_ENV_SHIFT 7
#endif

/* Tunnel wall texture: (1 << EH_TUNNEL_TEX_SHIFT) texels square. */
#ifndef EH_TUNNEL_TEX_SHIFT
#define EH_TUNNEL_TEX_SHIFT 7
#endif

/* Terrain height and colour maps: (1 << EH_TERRAIN_SHIFT) samples square. */
#ifndef EH_TERRAIN_SHIFT
#define EH_TERRAIN_SHIFT 8
#endif

/* Logo particles, and whether they draw motion streaks. */
#ifndef EH_PARTICLE_COUNT
#define EH_PARTICLE_COUNT 8192
#endif
#ifndef EH_PARTICLE_TRAILS
#define EH_PARTICLE_TRAILS 1
#endif

/* Frame partition. A frame is prepared serially, rendered as EH_PARTS parts
 * that write disjoint pixels (one or two passes), then finished serially.
 * EH_WORKERS parts may run at once, one per core; a worker index selects
 * per-core scratch such as the torus depth band. */
#define EH_PARTS 8
#define EH_WORKERS 2

#endif

/* D-pad camera and button state.
 *
 * Held directions integrate yaw/pitch at fixed rates; CENTER, A and B act on
 * press edges only so a held button does not repeat. */
#ifndef EH_CAMERA_H
#define EH_CAMERA_H

#include <stdint.h>

#define EH_CAM_YAW_RATE 1.5f   /* rad/s while LEFT/RIGHT held */
#define EH_CAM_PITCH_RATE 1.0f /* rad/s while UP/DOWN held */
#define EH_CAM_PITCH_MAX 0.6f  /* |pitch| clamp, rad */

typedef struct {
    float yaw;   /* rad, wrapped to (-pi, pi]; RIGHT increases */
    float pitch; /* rad, clamped; UP increases */
    unsigned palette;
    int wireframe;
    uint32_t prev_buttons;
} eh_camera;

void eh_camera_init(eh_camera *cam);
void eh_camera_update(eh_camera *cam, uint32_t buttons, float dt_s);

#endif

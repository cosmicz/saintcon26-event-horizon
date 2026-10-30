#include "eh/eh_camera.h"

#include "eh/eh_tables.h"
#include "eh/eh_types.h"
#include "eh_math.h"

void eh_camera_init(eh_camera *cam)
{
    cam->yaw = 0.0f;
    cam->pitch = 0.0f;
    cam->palette = 0;
    cam->wireframe = 0;
    cam->prev_buttons = 0;
}

void eh_camera_update(eh_camera *cam, uint32_t buttons, float dt_s)
{
    const uint32_t pressed = buttons & ~cam->prev_buttons;
    cam->prev_buttons = buttons;

    if (buttons & EH_BTN_RIGHT)
        cam->yaw += EH_CAM_YAW_RATE * dt_s;
    if (buttons & EH_BTN_LEFT)
        cam->yaw -= EH_CAM_YAW_RATE * dt_s;
    while (cam->yaw > EH_PI)
        cam->yaw -= EH_TAU;
    while (cam->yaw <= -EH_PI)
        cam->yaw += EH_TAU;

    if (buttons & EH_BTN_UP)
        cam->pitch += EH_CAM_PITCH_RATE * dt_s;
    if (buttons & EH_BTN_DOWN)
        cam->pitch -= EH_CAM_PITCH_RATE * dt_s;
    cam->pitch = eh_clampf(cam->pitch, -EH_CAM_PITCH_MAX, EH_CAM_PITCH_MAX);

    if (pressed & EH_BTN_CENTER)
        cam->yaw = cam->pitch = 0.0f;
    if (pressed & EH_BTN_A)
        cam->palette = (cam->palette + 1) % EH_PALETTE_COUNT;
    if (pressed & EH_BTN_B)
        cam->wireframe = !cam->wireframe;
}

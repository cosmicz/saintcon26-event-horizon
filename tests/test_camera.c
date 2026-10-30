#include <math.h>

#include "eh/eh_camera.h"
#include "eh/eh_tables.h"
#include "eh/eh_types.h"
#include "eh_test.h"

static void hold(eh_camera *c, uint32_t buttons, int steps, float dt)
{
    for (int i = 0; i < steps; i++)
        eh_camera_update(c, buttons, dt);
}

void test_camera_idle_and_yaw(void)
{
    eh_camera c;
    eh_camera_init(&c);
    hold(&c, 0, 10, 0.1f);
    EH_CHECK(c.yaw == 0.0f && c.pitch == 0.0f);

    hold(&c, EH_BTN_RIGHT, 10, 0.1f);
    EH_CHECK_MSG(fabsf(c.yaw - EH_CAM_YAW_RATE) < 1e-3f, "yaw %.4f", c.yaw);
    hold(&c, 0, 10, 0.1f);
    EH_CHECK(fabsf(c.yaw - EH_CAM_YAW_RATE) < 1e-3f);
    hold(&c, EH_BTN_LEFT, 20, 0.1f);
    EH_CHECK_MSG(fabsf(c.yaw + EH_CAM_YAW_RATE) < 1e-3f, "yaw %.4f", c.yaw);

    eh_camera_init(&c);
    hold(&c, EH_BTN_RIGHT, 30, 0.1f); /* 4.5 rad wraps into (-pi, pi] */
    EH_CHECK_MSG(fabsf(c.yaw - (4.5f - 2.0f * (float)M_PI)) < 1e-3f, "yaw %.4f", c.yaw);
}

void test_camera_pitch_clamp_and_center_reset(void)
{
    eh_camera c;
    eh_camera_init(&c);
    hold(&c, EH_BTN_UP, 20, 0.1f);
    EH_CHECK(fabsf(c.pitch - EH_CAM_PITCH_MAX) < 1e-6f);
    hold(&c, EH_BTN_DOWN, 40, 0.1f);
    EH_CHECK(fabsf(c.pitch + EH_CAM_PITCH_MAX) < 1e-6f);
    hold(&c, EH_BTN_RIGHT, 3, 0.1f);
    hold(&c, EH_BTN_CENTER, 3, 0.1f);
    EH_CHECK(c.yaw == 0.0f && c.pitch == 0.0f);
}

void test_camera_buttons_edge_triggered(void)
{
    eh_camera c;
    eh_camera_init(&c);
    /* Held for 3 updates: 3 is not 1 modulo the palette count, so a
     * level-triggered implementation cannot pass by wrapping. */
    hold(&c, EH_BTN_A, 3, 0.1f);
    EH_CHECK(c.palette == 1);
    hold(&c, 0, 1, 0.1f);
    hold(&c, EH_BTN_A, 1, 0.1f);
    EH_CHECK(c.palette == 2);
    for (int i = 0; i < EH_PALETTE_COUNT - 2; i++) {
        hold(&c, 0, 1, 0.1f);
        hold(&c, EH_BTN_A, 1, 0.1f);
    }
    EH_CHECK(c.palette == 0);

    EH_CHECK(c.wireframe == 0);
    hold(&c, EH_BTN_B, 4, 0.1f); /* even count: level-triggered would end at 0 */
    EH_CHECK(c.wireframe == 1);
    hold(&c, 0, 1, 0.1f);
    hold(&c, EH_BTN_B, 1, 0.1f);
    EH_CHECK(c.wireframe == 0);
}

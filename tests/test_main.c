/* Test registry. Usage: eh_tests [NAME]; with no name, runs all. */
#include <stdio.h>
#include <string.h>

#include "eh_test.h"

int eh_test_failures;

#define EH_TESTS(X)                              \
    X(test_sin8)                                 \
    X(test_circle_spans)                         \
    X(test_spans_cover_every_displayed_pixel)    \
    X(test_demo_display_independent_of_prior_fb) \
    X(test_palettes_cyclic_and_distinct)         \
    X(test_led_samples_on_rim_clockwise)         \
    X(test_plasma_deterministic_and_animated)    \
    X(test_plasma_circle_clip)                   \
    X(test_plasma_bands_compose_exactly)         \
    X(test_plasma_palette_select)                \
    X(test_raster_coverage_and_culling)          \
    X(test_raster_depth_order_independent)       \
    X(test_raster_band_and_texture)              \
    X(test_raster_line)                          \
    X(test_raster_matches_reference_coverage)    \
    X(test_camera_idle_and_yaw)                  \
    X(test_camera_pitch_clamp_and_center_reset)  \
    X(test_camera_buttons_edge_triggered)        \
    X(test_leds_solid_and_cap)                   \
    X(test_leds_follow_local_colour)             \
    X(test_present_circle_mask_and_mapping)      \
    X(test_present_leds_drawn_with_gain)         \
    X(test_present_ppm_write)                    \
    X(test_fade)                                 \
    X(test_torus_knot_renders_and_responds)      \
    X(test_torus_transitions)                    \
    X(test_torus_strip_cull_exact)               \
    X(test_tunnel_depth_motion_and_camera)       \
    X(test_tunnel_centre_continuous_across_yaw_wrap) \
    X(test_tunnel_angle_matches_atan2)           \
    X(test_terrain_heightmap_deterministic_varied) \
    X(test_terrain_sky_sun_and_camera)           \
    X(test_terrain_camera_clears_peaks)          \
    X(test_logo_targets_inside_display)          \
    X(test_logo_reads_event_horizon_then_saintcon) \
    X(test_particles_shatter_then_rebuild)       \
    X(test_particles_trails_follow_motion)       \
    X(test_timeline_boundaries)                  \
    X(test_demo_every_scene_renders_with_leds)   \
    X(test_demo_dpad_changes_every_scene)        \
    X(test_demo_seek_matches_playback)           \
    X(test_demo_palette_button)                  \
    X(test_demo_parts_write_disjoint_regions)    \
    X(test_demo_parts_any_schedule_bit_exact)    \
    X(test_pool_two_threads_match_serial)        \
    X(test_memory_budget_in_band)                \
    X(test_boot_choose_dpad_only)                \
    X(test_release_wait_all_buttons)             \
    X(test_menu_select_times_out_to_stock)       \
    X(test_menu_select_moves_restart_the_countdown) \
    X(test_menu_select_centre_confirms)          \
    X(test_menu_select_ignores_other_keys)       \
    X(test_menu_select_keys_down_at_start_are_not_presses)\
    X(test_menu_draw_marks_the_choice)           \
    X(test_menu_draw_release_prompt)             \
    X(test_menu_draw_countdown_arc)              \
    X(test_menu_draw_countdown_touches_only_the_arc)\
    X(test_qspi_single_line_pack)

#define DECLARE(name) void name(void);
EH_TESTS(DECLARE)

static const struct {
    const char *name;
    void (*fn)(void);
} tests[] = {
#define ENTRY(name) {#name, name},
    EH_TESTS(ENTRY)
};

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++)
            puts(tests[i].name);
        return 0;
    }
    int ran = 0, failed_tests = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)
            continue;
        int before = eh_test_failures;
        tests[i].fn();
        ran++;
        if (eh_test_failures != before) {
            failed_tests++;
            fprintf(stderr, "FAIL %s\n", tests[i].name);
        }
    }
    if (ran == 0) {
        fprintf(stderr, "no test named %s\n", argc > 1 ? argv[1] : "(any)");
        return 2;
    }
    printf("%d tests, %d failed, %d failed checks\n", ran, failed_tests, eh_test_failures);
    return failed_tests ? 1 : 0;
}

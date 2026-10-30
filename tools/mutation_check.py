#!/usr/bin/env python3
"""Non-vacuity check: break the code in targeted ways and confirm the test
that covers each behaviour fails.

Copies the source tree (without build/, .git, docs/) into a temporary
directory, applies one mutation at a time, rebuilds eh_tests there, and runs
the named test. The working tree is never modified.

    python3 tools/mutation_check.py          # exit 0 iff every mutant is caught
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

# (description, file, original text, mutated text, test that must fail)
MUTATIONS = [
    ("plasma band ignores y0", "src/core/eh_plasma.c",
     "    for (int y = y0; y < y1; y++) {", "    for (int y = 0; y < y1; y++) {",
     "test_plasma_bands_compose_exactly"),
    ("plasma ignores circle clip", "src/core/eh_plasma.c",
     "const int x0 = clip_circle ? eh_span_x0[y] : 0;", "const int x0 = 0;",
     "test_plasma_circle_clip"),
    ("A/B level-triggered", "src/core/eh_camera.c",
     "    if (pressed & EH_BTN_A)", "    if (buttons & EH_BTN_A)",
     "test_camera_buttons_edge_triggered"),
    ("pitch unclamped", "src/core/eh_camera.c",
     "cam->pitch = eh_clampf(cam->pitch, -EH_CAM_PITCH_MAX, EH_CAM_PITCH_MAX);", "",
     "test_camera_pitch_clamp_and_center_reset"),
    ("raster ignores depth", "src/core/eh_raster.c",
     "                if (zq >= *zp) {\n                    depth_failed++;\n                    continue;\n                }\n", "",
     "test_raster_depth_order_independent"),
    ("raster depth not band-relative", "src/core/eh_raster.c",
     "&t->zb[(y - t->y0) * EH_FB_W + minx + i0]", "&t->zb[y * EH_FB_W + minx + i0]",
     "test_raster_band_and_texture"),
    ("raster row run too short", "src/core/eh_raster.c",
     "i1 = (int)ceilf(hi);", "i1 = (int)floorf(hi) - 1;",
     "test_raster_matches_reference_coverage"),
    ("raster row run starts late", "src/core/eh_raster.c",
     "int i0 = (int)floorf(lo),", "int i0 = (int)ceilf(lo) + 1,",
     "test_raster_matches_reference_coverage"),
    ("strip cull ignores next ring", "src/core/eh_torus.c",
     "ws->ring_ymax[u] > ws->ring_ymax[u1] ? ws->ring_ymax[u] : ws->ring_ymax[u1];",
     "ws->ring_ymax[u];", "test_torus_strip_cull_exact"),
    ("raster miscounts coverage", "src/core/eh_raster.c",
     "            written++;\n", "", "test_raster_coverage_and_culling"),
    ("torus ignores camera", "src/core/eh_torus.c",
     "t * 0.6f + cam->yaw", "t * 0.6f", "test_torus_knot_renders_and_responds"),
    ("torus no dive fade", "src/core/eh_torus.c",
     "    ws->fade_white =\n        local_ms", "    ws->fade_white = 0 &&\n        local_ms",
     "test_torus_transitions"),
    ("tunnel ignores camera", "src/core/eh_tunnel.c",
     "sinf(t * 0.0011f) * 20.0f + sinf(cam->yaw) * 30.0f", "sinf(t * 0.0011f) * 20.0f",
     "test_demo_dpad_changes_every_scene"),
    ("tunnel quadrant unfold swapped", "src/core/eh_tunnel.c",
     "(dy >= 0 ? 511u - a : 512u + a)", "(dy >= 0 ? 512u + a : 511u - a)",
     "test_tunnel_angle_matches_atan2"),
    ("tunnel fold off by one", "src/core/eh_tunnel.c",
     "return d < 0 ? -d - 1 : d;", "return d < 0 ? -d : d;",
     "test_tunnel_angle_matches_atan2"),
    ("terrain ignores pitch", "src/core/eh_terrain.c",
     "HORIZON_Y + cam->pitch * 180.0f", "HORIZON_Y", "test_terrain_sky_sun_and_camera"),
    ("terrain v1 fixed eye height", "src/core/eh_terrain.c",
     "    return eye > ALTITUDE ? eye : ALTITUDE;", "    return 0.0f * eye + ALTITUDE;",
     "test_terrain_camera_clears_peaks"),
    ("terrain envelope not Lipschitz", "src/core/eh_terrain.c",
     "        const float s = j * STEP_S;", "        const float s = t + (j - j0) * STEP_S - WINDOW_S;",
     "test_terrain_camera_clears_peaks"),
    ("terrain sky overdraws land", "src/core/eh_terrain.c",
     "sky_column(ws, rowc, fb, x0 + i, ytop[i], ybuf[i]);",
     "sky_column(ws, rowc, fb, x0 + i, ytop[i], eh_span_x1[x0 + i]);",
     "test_terrain_sky_sun_and_camera"),
    ("particles never rebuild", "src/core/eh_particles.c",
     "    if (tau > delay) {\n        const int k", "    if (0) {\n        const int k",
     "test_particles_shatter_then_rebuild"),
    ("particles without trails", "src/core/eh_particles.c",
     "const float tail_tau = tau > TRAIL_S ? tau - TRAIL_S : 0.0f;", "const float tail_tau = tau;",
     "test_particles_trails_follow_motion"),
    ("particle streak ignores band", "src/core/eh_particles.c",
     "if (y >= y0 && y < y1 && x >= 0 && x < EH_FB_W)", "if (y >= 0 && y < EH_FB_H && x >= 0 && x < EH_FB_W)",
     "test_demo_parts_write_disjoint_regions"),
    ("particles seed from stale fb", "src/core/eh_demo.c",
     "        eh_terrain_render(&d->ws.terrain, fb, EH_SCENE_TERRAIN_MS - 1, EH_SCENE_TERRAIN_MS,\n"
     "                          &d->cam);\n",
     "", "test_demo_seek_matches_playback"),
    ("leds sample centre only", "src/core/eh_leds.c",
     "fb[eh_led_samples[k][s]]", "fb[EH_FB_PIXELS / 2 + EH_FB_W / 2]",
     "test_leds_follow_local_colour"),
    ("leds ignore brightness cap", "src/core/eh_leds.c",
     "(r * max_brightness + 127u) / 255u", "r", "test_leds_solid_and_cap"),
    ("present without circle mask", "src/host/eh_present.c",
     "if (dx * dx + dy * dy > EH_LCD_W * EH_LCD_W)", "if (0 && dx * dx + dy * dy > EH_LCD_W * EH_LCD_W)",
     "test_present_circle_mask_and_mapping"),
    ("timeline off by one", "src/core/eh_demo.c",
     "while (p.local_ms >= scene_ms[p.scene])", "while (p.local_ms > scene_ms[p.scene])",
     "test_timeline_boundaries"),
    ("pool skips the pass barrier", "src/host/eh_pool.c",
     "while (p->finished != gen)", "while (0 && p->finished != gen)",
     "test_pool_two_threads_match_serial"),
    ("over budget", "include/eh/eh_config.h",
     "#define EH_PARTICLE_COUNT 8192", "#define EH_PARTICLE_COUNT 16384",
     "test_memory_budget_in_band"),
    ("boot demo on any RIGHT sample", "device/menu/eh_boot_select.c",
     "        if (!(pressed[i] & EH_DPAD_MASK & EH_DEMO_SELECT_BIT))\n            return EH_BOOT_STOCK;\n    return EH_BOOT_DEMO;",
     "        if (pressed[i] & EH_DEMO_SELECT_BIT)\n            return EH_BOOT_DEMO;\n    return EH_BOOT_STOCK;",
     "test_boot_choose_dpad_only"),
    ("boot select reads A", "device/menu/eh_boot_select.c",
     "EH_DPAD_MASK & EH_DEMO_SELECT_BIT", "(EH_DEMO_SELECT_BIT | EH_BTN_BIT(EH_BADGE_BTN_A_PIN))",
     "test_boot_choose_dpad_only"),
    ("release wait ignores A/B", "device/menu/eh_boot_select.c",
     "if (pressed & EH_ALL_BUTTONS_MASK)", "if (pressed & EH_DPAD_MASK)",
     "test_release_wait_all_buttons"),
    ("release wait not restarted", "device/menu/eh_boot_select.c",
     "        t->released = false;\n        return false;", "        return false;",
     "test_release_wait_all_buttons"),
    ("end screen lines overlap", "src/core/eh_particles.c",
     'emit_text("EVENT HORIZON", 4, 140', 'emit_text("EVENT HORIZON", 4, 192',
     "test_logo_reads_event_horizon_then_saintcon"),
    ("qspi nibble order reversed", "device/demo/eh_qspi_pack.h",
     "(b >> (7 - 4 * half - k))", "(b >> (4 * half + k))", "test_qspi_single_line_pack"),
    ("tunnel yaw linear again", "src/core/eh_tunnel.c",
     "sinf(cam->yaw) * 30.0f", "cam->yaw * 24.0f",
     "test_tunnel_centre_continuous_across_yaw_wrap"),
    ("menu treats held keys as presses", "device/menu/eh_boot_select.c",
     "    m->held = pressed & EH_DPAD_MASK;", "    m->held = pressed & 0u;",
     "test_menu_select_keys_down_at_start_are_not_presses"),
    ("menu move keeps the old deadline", "device/menu/eh_boot_select.c",
     "        m->choice = EH_BOOT_DEMO;\n        m->deadline_us = now_us + EH_MENU_TIMEOUT_US;",
     "        m->choice = EH_BOOT_DEMO;",
     "test_menu_select_moves_restart_the_countdown"),
    ("menu confirms on A", "device/menu/eh_boot_select.c",
     "    if (presses & EH_CONFIRM_BIT)",
     "    if ((presses & EH_CONFIRM_BIT) || (pressed & EH_BTN_BIT(EH_BADGE_BTN_A_PIN)))",
     "test_menu_select_ignores_other_keys"),
    ("menu defaults to the demo", "device/menu/eh_boot_select.c",
     "    m->choice = EH_BOOT_STOCK;\n    m->deadline_us", "    m->choice = EH_BOOT_DEMO;\n    m->deadline_us",
     "test_menu_select_times_out_to_stock"),
    ("menu dims the chosen tile", "device/menu/eh_menu_draw.c",
     "                if (!chosen)\n                    c = dim(c);", "                if (chosen)\n                    c = dim(c);",
     "test_menu_draw_marks_the_choice"),
    ("menu countdown runs backwards", "device/menu/eh_menu_draw.c",
     "a < lit ? colour : EH_MENU_ARC_TRACK", "a >= lit ? colour : EH_MENU_ARC_TRACK",
     "test_menu_draw_countdown_arc"),
    ("menu countdown back to 1 s", "device/menu/eh_boot_select.h",
     "#define EH_MENU_TIMEOUT_US    3000000", "#define EH_MENU_TIMEOUT_US    1000000",
     "test_menu_select_times_out_to_stock"),
    ("menu never prompts release", "device/menu/eh_menu_draw.c",
     "    if (release)\n", "    if (0 && release)\n",
     "test_menu_draw_release_prompt"),
    # Python tests are named "py:FILE:Class.test"; no rebuild is needed.
    ("pack drops stock size check", "device/tools/dualboot_pack.py",
     "    if extent > stock_size:", "    if False:",
     "py:device/tools/test_dualboot_pack.py:PackTest.test_rejects_stock_data_outside_extent"),
    ("pack allows the last sector", "device/tools/dualboot_pack.py",
     "start + size > FLASH_SIZE - SECTOR", "start + size > FLASH_SIZE",
     "py:device/tools/test_dualboot_pack.py:PackTest.test_rejects_partition_in_last_sector"),
    ("pack shifts stock by a sector", "device/tools/dualboot_pack.py",
     "    stock = dump[:extent]", "    stock = dump[SECTOR:extent + SECTOR]",
     "py:device/tools/test_dualboot_pack.py:PackTest.test_stock_is_pure_relocation"),
    ("extractor hashes the whole dump", "device/tools/extract_panel_init.py",
     "hashlib.sha256(image[:STOCK_SLOT])", "hashlib.sha256(image)",
     "py:device/tools/test_extract_panel_init.py:DumpGateTest.test_ignores_flash_past_the_stock_slot"),
    ("extractor skips the firmware hash", "device/tools/extract_panel_init.py",
     "hexdigest() != STOCK_SLOT_SHA256:", "hexdigest() == None:",
     "py:device/tools/test_extract_panel_init.py:DumpGateTest.test_rejects_a_changed_stock_slot"),
    ("extractor takes a partial dump", "device/tools/extract_panel_init.py",
     "    if len(image) != FLASH_SIZE:", "    if False:",
     "py:device/tools/test_extract_panel_init.py:DumpGateTest.test_rejects_a_partial_dump"),
    ("audit fails open on header", "device/tools/image_audit.py",
     "    if missing:\n        raise AuditConfigError", "    if False:\n        raise AuditConfigError",
     "py:device/tools/test_image_audit.py:AuditTest.test_fails_closed_without_forbidden_codes"),
    ("audit skips GPIO aliases", "device/tools/image_audit.py",
     "        hits = literal_hits(image, addr)", "        hits = literal_hits(image, addr, aliases=1)",
     "py:device/tools/test_image_audit.py:AuditTest.test_each_check_fails_on_its_own_trigger"),
    ("pin audit approves any expression", "device/tools/pin_audit.py",
     "        raise Unapproved(f\"pin expression {e!r} is outside the approved grammar\")", "        return set()",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_rejections"),
    ("pin audit trusts table entries", "device/tools/pin_audit.py",
     "        if not re.fullmatch(r\"EH_BADGE_\\w+_PIN\", entry):", "        if False:",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_rejections"),
    ("pin audit ignores loop variable writes", "device/tools/pin_audit.py",
     "            raise Unapproved(f\"loop body assigns its variable {var}\")", "            pass",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_rejections"),
    ("pin audit skips C-SDK sections", "device/tools/pin_audit.py",
     "        text = \"\\n\".join(C_SDK.findall(text))", "        text = \"\"",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_pio_c_sdk_sections_audited"),
    ("pin audit skips C-SDK helpers", "device/tools/pin_audit.py",
     "        audit_calls(path, text, {}, lambda o, spans=spans: not any(lo <= o < hi for lo, hi in spans))",
     "        audit_calls(path, text, {}, lambda o: not path.endswith(\".pio\"))",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_pio_c_sdk_sections_audited"),
    ("pin audit ignores loop bounds", "device/tools/pin_audit.py",
     "            return {p + k for p in self.span(m.group(1)) for k in range(n)}", "            return self.span(m.group(1))",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_ranges_reaching_eeprom_rejected"),
    ("pin audit ignores pin counts", "device/tools/pin_audit.py",
     "        pins = {p + k for p in pins for k in range(int(count))}", "        pins = set(pins)",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_ranges_reaching_eeprom_rejected"),
    ("pin audit drops caller bindings", "device/tools/pin_audit.py",
     "                    bindings[name][param] |= reached", "                    bindings[name][param] |= {99}",
     "py:device/tools/test_pin_audit.py:PinAuditTest.test_c_sdk_parameter_bound_to_caller_range"),
    ("pack misses overlap", "device/tools/dualboot_pack.py",
     "        if a + alen > b:", "        if a + alen > b + alen:",
     "py:device/tools/test_dualboot_pack.py:PackTest.test_rejects_overlapping_partitions"),
]


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="eh-mutants-") as tmp:
        src = pathlib.Path(tmp) / "src"
        build = pathlib.Path(tmp) / "build"
        shutil.copytree(root, src, ignore=shutil.ignore_patterns("build", "build-*", ".git", "docs", ".worktrees",
                                                            "__pycache__"))
        if run(["cmake", "-S", str(src), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release", "-DEH_PREVIEW=OFF"]).returncode:
            sys.exit("configure failed")
        survivors = 0
        for name, rel, old, new, test in MUTATIONS:
            path = src / rel
            original = path.read_text()
            if original.count(old) != 1:
                print(f"{name:30s} SKIPPED: anchor text not found exactly once in {rel}")
                survivors += 1
                continue
            path.write_text(original.replace(old, new))
            if test.startswith("py:"):
                _, script, case = test.split(":")
                built = True
                # -B: a cached .pyc is keyed on source mtime (seconds) and size,
                # so a same-size mutant of the same file would reuse the last one.
                caught = run([sys.executable, "-I", "-B", str(src / script), case]).returncode != 0
            else:
                built = run(["cmake", "--build", str(build), "-j", "8", "--target", "eh_tests"]).returncode == 0
                caught = built and run([str(build / "eh_tests"), test]).returncode != 0
            path.write_text(original)
            status = "caught" if caught else ("BUILD FAILED (fix mutation)" if not built else "SURVIVED")
            survivors += not caught
            print(f"{name:30s} -> {test}: {status}")
        print(f"{len(MUTATIONS) - survivors}/{len(MUTATIONS)} mutants caught")
        sys.exit(1 if survivors else 0)


if __name__ == "__main__":
    main()

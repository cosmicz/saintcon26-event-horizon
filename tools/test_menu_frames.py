#!/usr/bin/env python3
"""Checks eh_menu_frames, the host render of the boot menu countdown.

    python3 -I -B tools/test_menu_frames.py BUILD/eh_menu_frames
"""

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOL = None
W = H = 360
ARC_IN, ARC_OUT = 170, 177  # device/menu/eh_menu_draw.h


def run(*args):
    tmp = tempfile.TemporaryDirectory()
    out = subprocess.run([TOOL, tmp.name, *args], check=True, capture_output=True, text=True).stdout
    return tmp, [json.loads(line) for line in out.splitlines()]


def pixels(path):
    data = pathlib.Path(path).read_bytes()
    header = b"P6\n360 360\n255\n"
    assert data.startswith(header), path
    return data[len(header):]


class MenuFramesTest(unittest.TestCase):
    def test_countdown_from_three_seconds_to_one(self):
        tmp, frames = run()
        with tmp:
            self.assertEqual(len(frames), 61)  # 2 s at 30 fps, both ends
            self.assertEqual(frames[0]["remaining_ms"], 3000)
            self.assertEqual(frames[-1]["remaining_ms"], 1000)
            self.assertTrue(all(f["choice"] == "stock" for f in frames))
            self.assertEqual(sorted(p.name for p in pathlib.Path(tmp.name).iterdir())[-1], "frame_0060.ppm")
            first, last = pixels(f"{tmp.name}/frame_0000.ppm"), pixels(f"{tmp.name}/frame_0060.ppm")
            changed = [i // 3 for i in range(0, len(first), 3) if first[i:i + 3] != last[i:i + 3]]
            self.assertTrue(changed)
            for i in changed:  # only the countdown arc moves
                dx, dy = i % W - W // 2, i // W - H // 2
                self.assertTrue(ARC_IN ** 2 <= dx * dx + dy * dy < ARC_OUT ** 2, (dx, dy))

    def test_right_switches_to_the_demo_and_restarts(self):
        tmp, frames = run("--right-at-ms", "800")
        with tmp:
            at = next(f for f in frames if f["choice"] == "demo")
            self.assertAlmostEqual(at["t_ms"], 800, delta=34)
            self.assertAlmostEqual(at["remaining_ms"], 3000, delta=34)
            self.assertEqual(frames[-1]["choice"], "demo")
            self.assertGreaterEqual(frames[-1]["remaining_ms"], 1000)
            # The switch redraws the tiles and label, not just the arc.
            before = pixels(f"{tmp.name}/frame_{at['frame'] - 1:04d}.ppm")
            after = pixels(f"{tmp.name}/frame_{at['frame']:04d}.ppm")
            off_arc = 0
            for i in range(0, len(before), 3):
                if before[i:i + 3] != after[i:i + 3]:
                    dx, dy = (i // 3) % W - W // 2, (i // 3) // W - H // 2
                    off_arc += not ARC_IN ** 2 <= dx * dx + dy * dy < ARC_OUT ** 2
            self.assertGreater(off_arc, 1000)


if __name__ == "__main__":
    TOOL = sys.argv.pop(1)
    unittest.main()

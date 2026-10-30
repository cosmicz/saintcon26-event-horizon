#!/usr/bin/env python3
"""Before/after evidence for every RAM block the native-360 build spends.

For each quality knob in include/eh/eh_config.h this builds eh_cli with the
knob lowered, records the memory accounting, renders the same frame as the
default build and writes a sheet:

    [ default | ablated ]
    [ default crop x4 | ablated crop x4 ]

It also builds the v1 180x180 reference from Git history and writes the
same kind of sheet for v1 versus native. Everything runs on the host with
the project's own sources; no device is touched. Frames are deterministic,
so the manifest records the framebuffer hash of every frame used.

    python3 tools/ablate.py --out docs/measurements/native-360/ablation
    python3 tools/ablate.py --only torus-env --palette 2 --out out/spectrum
"""

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import io

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ppm_to_png  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
V1_REF = "5889e0d"
CROP, ZOOM = 110, 4  # 110 px squares at 4x fill a 440 px tile

# name, EH_DEFINES, scene key in `eh_cli memory`, frame time, crop origin
# (canvas px), what the reader should look for.
ABLATIONS = [
    ("torus-env", "EH_ENV_SHIFT=6", "torus-plasma", 6000, (176, 150),
     "environment map 128x128 -> 64x64: blurrier, blockier reflections on the chrome tube"),
    ("torus-mesh", "EH_TORUS_NU=96;EH_TORUS_NV=8", "torus-plasma", 6000, (176, 150),
     "mesh 160x16 -> v1 96x8: faceted tube outline and polygonal bends"),
    ("tunnel-tex", "EH_TUNNEL_TEX_SHIFT=6", "tunnel", 20000, (40, 190),
     "wall texture 128x128 -> 64x64: coarser panels and stair-stepped seams"),
    ("terrain-map", "EH_TERRAIN_SHIFT=7", "terrain", 30500, (165, 290),
     "height/colour maps 256 -> 128: near slopes break into blocks twice as large; "
     "loses the finest noise octave and per-sample grain"),
    ("particle-count", "EH_PARTICLE_COUNT=2048", "particles-logo", 48500, (60, 175),
     "8,192 -> v1 2,048 particles: fewer particles leave gaps in the settled lettering"),
    ("particle-trails", "EH_PARTICLE_TRAILS=0", "particles-logo", 45000, (165, 165),
     "trails off: rebuild flight shows dots instead of motion streaks"),
]

# v1 versus native, one frame per scene (same timeline in both builds).
V1_FRAMES = [
    ("torus-plasma", 6000, (176, 150)),
    ("tunnel", 20000, (40, 190)),
    ("terrain", 30500, (165, 290)),
    ("particles-flight", 45000, (165, 165)),
    ("particles-logo", 48500, (60, 175)),
]


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f"command failed: {' '.join(map(str, cmd))}\n{r.stdout}\n{r.stderr}")
    return r.stdout


def build(src, out, defines=""):
    cfg = ["cmake", "-S", str(src), "-B", str(out), "-DCMAKE_BUILD_TYPE=Release", "-DEH_PREVIEW=OFF"]
    if defines:
        cfg.append(f"-DEH_DEFINES={defines}")
    run(cfg)
    run(["cmake", "--build", str(out), "-j", "8", "--target", "eh_cli"])
    return out / "eh_cli"


def palette_holds(palette):
    """Use the existing A button, leaving yaw, pitch and wireframe untouched."""
    if palette not in range(4):
        raise ValueError("palette must be 0..3")
    return [arg for i in range(palette) for arg in ("--hold", f"A:{66*i}-{66*i+33}")]


def frame(cli, tmp, t, palette=0):
    controls = palette_holds(palette)
    d = pathlib.Path(tmp) / f"frames-{abs(hash((str(cli), t, palette)))}"
    line = run([str(cli), "frames", "--out", str(d), "--at", str(t), *controls]).strip().splitlines()[-1]
    info = json.loads(line)
    if info["palette"] != palette or info["yaw"] != 0 or info["pitch"] != 0:
        raise ValueError("comparison controls disagree: palette or camera changed")
    return ppm_to_png.read_ppm(info["file"]), info["fb_fnv1a"]


def write_sheet(path, a, b, origin):
    x, y = origin
    tiles = [a, b, ppm_to_png.crop_zoom(a, x, y, CROP, ZOOM), ppm_to_png.crop_zoom(b, x, y, CROP, ZOOM)]
    ppm_to_png.write_png(str(path), *ppm_to_png.tile(tiles, 2))


def memory(cli):
    return json.loads(run([str(cli), "memory"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", help="comma-separated ablation names, v1, or v1-SCENE (default: all)")
    ap.add_argument("--palette", type=int, choices=range(4), default=0,
                    help="same renderer palette on both sides: 0 horizon, 1 ice, 2 spectrum, 3 ember")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None
    known = {row[0] for row in ABLATIONS} | {"v1"} | {f"v1-{row[0]}" for row in V1_FRAMES}
    if only and not only <= known:
        ap.error("unknown comparison: " + ", ".join(sorted(only - known)))

    with tempfile.TemporaryDirectory(prefix="eh-ablate-") as tmp:
        tmp = pathlib.Path(tmp)
        default_cli = build(ROOT, tmp / "default")
        default_mem = memory(default_cli)
        manifest = {
            "tool": "tools/ablate.py",
            "tool_sha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
            "native_source_ref": run(["git", "-C", str(ROOT), "rev-parse", "HEAD"]).strip(),
            "renderer_files_sha256": {
                p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
                for p in run(["git", "-C", str(ROOT), "ls-files", "src", "include"]).splitlines()},
            "palette": args.palette,
            "palette_name": ("horizon", "ice", "spectrum", "ember")[args.palette],
            "frame_arguments": palette_holds(args.palette),
            "camera": {"yaw": 0, "pitch": 0},
            "scope": "host-rendered frames; memory is sizeof accounting from eh_cli memory",
            "crop": {"size": CROP, "zoom": ZOOM, "origin": "canvas px (LCD at +40,+40)"},
            "default": {"core_ram": default_mem["core_ram_union_peak"],
                        "fraction": default_mem["core_ram_fraction_of_sram"]},
            "ablations": [],
            "v1_vs_native": [],
        }

        for name, defines, scene, t, origin, look in ABLATIONS:
            if only and name not in only:
                continue
            cli = build(ROOT, tmp / name, defines)
            mem = memory(cli)
            a, ha = frame(default_cli, tmp, t, args.palette)
            b, hb = frame(cli, tmp, t, args.palette)
            png = out / f"ablate-{name}.png"
            write_sheet(png, a, b, origin)
            d_scene = default_mem["per_scene_core_ram"][scene]["bytes"]
            a_scene = mem["per_scene_core_ram"][scene]["bytes"]
            manifest["ablations"].append({
                "name": name, "defines": defines, "scene": scene, "t_ms": t,
                "scene_core_ram_default": d_scene, "scene_core_ram_ablated": a_scene,
                "block_bytes": d_scene - a_scene,
                "core_ram_ablated": mem["core_ram_union_peak"],
                "fb_fnv1a_default": ha, "fb_fnv1a_ablated": hb,
                "frames_identical": ha == hb, "look_for": look, "sheet": png.name,
                "sheet_sha256": hashlib.sha256(png.read_bytes()).hexdigest(),
            })
            print(f"{name:16s} {d_scene - a_scene:7d} B  {png}")

        if not only or "v1" in only or any(n.startswith("v1-") for n in only):
            v1_src = tmp / "v1-src"
            v1_src.mkdir()
            blob = subprocess.run(["git", "-C", str(ROOT), "archive", V1_REF], capture_output=True, check=True).stdout
            with tarfile.open(fileobj=io.BytesIO(blob)) as tf:
                tf.extractall(v1_src, filter="data")
            v1_cli = build(v1_src, tmp / "v1")
            v1_mem = memory(v1_cli)
            manifest["v1"] = {"ref": V1_REF, "core_ram": v1_mem["core_ram_estimate"],
                              "fraction": v1_mem["core_ram_fraction_of_sram"]}
            for name, t, origin in V1_FRAMES:
                if only and "v1" not in only and f"v1-{name}" not in only:
                    continue
                a, ha = frame(v1_cli, tmp, t, args.palette)
                b, hb = frame(default_cli, tmp, t, args.palette)
                png = out / f"v1-vs-native-{name}.png"
                write_sheet(png, a, b, origin)
                manifest["v1_vs_native"].append({"name": name, "t_ms": t, "fb_fnv1a_v1": ha,
                                                 "fb_fnv1a_native": hb, "sheet": png.name,
                                                 "sheet_sha256": hashlib.sha256(png.read_bytes()).hexdigest()})
                print(f"v1 vs native {name:16s} {png}")

        (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()

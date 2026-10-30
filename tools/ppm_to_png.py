#!/usr/bin/env python3
"""Convert binary PPM (P6) frames to PNG, optionally tiled into one sheet.

Standard library only, so evidence images can be regenerated anywhere:

    python3 tools/ppm_to_png.py frame.ppm [...]            # frame.png beside each
    python3 tools/ppm_to_png.py --sheet out.png --cols 2 a.ppm b.ppm ...

Sheets place frames left to right, top to bottom, with a 4 px gap.
"""

import argparse
import struct
import zlib

GAP = 4
GAP_RGB = b"\x10\x10\x14"


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or fields[3] != b"255":
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = int(fields[1]), int(fields[2])
    px = data[pos + 1:pos + 1 + w * h * 3]
    if len(px) != w * h * 3:
        raise ValueError(f"{path}: truncated")
    return w, h, px


def write_png(path, w, h, px):
    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def crop_zoom(frame, x, y, size, zoom):
    """Square crop at (x, y), scaled up by nearest neighbour."""
    w, h, px = frame
    if x < 0 or y < 0 or x + size > w or y + size > h:
        raise ValueError("crop outside frame")
    out = bytearray()
    for cy in range(size * zoom):
        row = (y + cy // zoom) * w
        for cx in range(size * zoom):
            o = (row + x + cx // zoom) * 3
            out += px[o:o + 3]
    return size * zoom, size * zoom, bytes(out)


def sheet(paths, cols):
    return tile([read_ppm(p) for p in paths], cols)


def tile(frames, cols):
    w, h = frames[0][0], frames[0][1]
    if any(f[0] != w or f[1] != h for f in frames):
        raise ValueError("sheet frames must share one size")
    rows = (len(frames) + cols - 1) // cols
    sw, sh = cols * w + (cols - 1) * GAP, rows * h + (rows - 1) * GAP
    out = bytearray(GAP_RGB * (sw * sh))
    for i, (_, _, px) in enumerate(frames):
        ox, oy = (i % cols) * (w + GAP), (i // cols) * (h + GAP)
        for y in range(h):
            o = ((oy + y) * sw + ox) * 3
            out[o:o + w * 3] = px[y * w * 3:(y + 1) * w * 3]
    return sw, sh, bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--sheet", help="write all inputs tiled into this PNG")
    ap.add_argument("--cols", type=int, default=2)
    ap.add_argument("inputs", nargs="+")
    args = ap.parse_args()
    if args.sheet:
        write_png(args.sheet, *sheet(args.inputs, args.cols))
    else:
        for p in args.inputs:
            write_png(p[:-4] + ".png" if p.endswith(".ppm") else p + ".png", *read_ppm(p))


if __name__ == "__main__":
    main()

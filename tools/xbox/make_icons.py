#!/usr/bin/env python3
"""The Xbox dashboard's two pictures: the title image (128x128) beside the
game and the save image (64x64) beside its save.

They are made from the Dreamcast port's cover art (cover.png in the
doom64-dc checkout setup.py fetches into extern/), cropped square and box
filtered down. Nothing of it is committed: the BMPs land in
Platform/xbox/gen/ (gitignored), and RXDK's bundler turns them into the DXT1
.xbx the dashboard needs (Platform/xbox/titleimage.rdf, saveimage.rdf).

    python tools/xbox/make_icons.py [cover.png]
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(ROOT, "extern", "doom64-dc", "cover.png")
OUT = os.path.join(ROOT, "Platform", "xbox", "gen")


def read_png(path):
    """8-bit RGB or RGBA, non-interlaced. Returns (w, h, bpp, rows)."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("make_icons: %s is not a PNG" % path)
    p, idat, hdr = 8, [], None
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        c = d[p + 8:p + 8 + n]
        p += 12 + n
        if t == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", c)
        elif t == b"IDAT":
            idat.append(c)
        elif t == b"IEND":
            break
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or ctype not in (2, 6) or interlace != 0:
        raise SystemExit("make_icons: %s is not 8-bit RGB(A) non-interlaced" % path)
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    st = w * bpp
    rows, prev, i = [], bytes(st), 0
    for _ in range(h):
        flt = raw[i]
        line = bytearray(raw[i + 1:i + 1 + st])
        i += 1 + st
        if flt == 1:
            for x in range(bpp, st):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif flt == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif flt == 3:
            for x in range(st):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif flt == 4:
            for x in range(st):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line))
        prev = rows[-1]
    return w, h, bpp, rows


def box_scale(w, h, bpp, rows, size):
    """The centred square of the picture, averaged down to size x size RGB,
    alpha composited over black."""
    side = min(w, h)
    x0, y0 = (w - side) // 2, (h - side) // 2
    out = []
    for oy in range(size):
        ya, yb = y0 + oy * side // size, y0 + (oy + 1) * side // size
        line = []
        for ox in range(size):
            xa, xb = x0 + ox * side // size, x0 + (ox + 1) * side // size
            r = g = b = n = 0
            for yy in range(ya, max(yb, ya + 1)):
                row = rows[yy]
                for xx in range(xa, max(xb, xa + 1)):
                    o = xx * bpp
                    a = row[o + 3] if bpp == 4 else 255
                    r += row[o] * a
                    g += row[o + 1] * a
                    b += row[o + 2] * a
                    n += 255
            line.append((r // n, g // n, b // n))
        out.append(line)
    return out


def write_bmp(path, pix):
    h, w = len(pix), len(pix[0])
    pad = (4 - (w * 3) % 4) % 4
    data = bytearray()
    for row in reversed(pix):
        for r, g, b in row:
            data += bytes((b, g, r))
        data += b"\0" * pad
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(hdr + info + data)
    os.replace(tmp, path)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SOURCE
    if not os.path.isfile(src):
        raise SystemExit("make_icons: %s is missing (python setup.py fetches it)" % src)
    os.makedirs(OUT, exist_ok=True)
    outs = [(os.path.join(OUT, "titleimage.bmp"), 128), (os.path.join(OUT, "saveimage.bmp"), 64)]
    stamp = os.path.getmtime(src)
    if all(os.path.isfile(p) and os.path.getmtime(p) >= stamp for p, _ in outs):
        print("make_icons: up to date")
        return
    w, h, bpp, rows = read_png(src)
    for path, size in outs:
        write_bmp(path, box_scale(w, h, bpp, rows, size))
    print("make_icons: %s -> titleimage.bmp (128x128), saveimage.bmp (64x64)" % os.path.basename(src))


if __name__ == "__main__":
    main()

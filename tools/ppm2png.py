#!/usr/bin/env python3
"""Convert binary PPM (P6) frames written by `facial_rigging --render-frames` to PNG (stdlib only)."""
import struct, sys, zlib

def ppm_to_png(src, dst):
    data = open(src, 'rb').read()
    parts = data.split(maxsplit=4)
    assert parts[0] == b'P6', 'not a P6 ppm'
    w, h, maxv = int(parts[1]), int(parts[2]), int(parts[3])
    pix = data[len(data) - w * h * 3:]
    raw = b''.join(b'\x00' + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(tag, body):
        c = tag + body
        return struct.pack('>I', len(body)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('usage: ppm2png.py in.ppm [in2.ppm ...]  (writes .png next to each)'); sys.exit(1)
    for f in sys.argv[1:]:
        out = f.rsplit('.', 1)[0] + '.png'; ppm_to_png(f, out); print(out)

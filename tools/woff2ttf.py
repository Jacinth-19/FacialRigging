#!/usr/bin/env python3
"""Unpack a WOFF (v1) font into a plain TTF/OTF (stdlib only). Used at build time to turn the
Material Icons Round webfont from third_party/material-icons into something stb_truetype loads."""
import struct, sys, zlib

def woff2ttf(src, dst):
    d = open(src, 'rb').read()
    sig, flavor, length, num, _res, _tot, *_ = struct.unpack('>4sIIHHI', d[:20])
    assert sig == b'wOFF', 'not a WOFF v1 file'
    tables = []
    for i in range(num):
        tag, off, comp, orig, _cs = struct.unpack('>4sIIII', d[44 + 20 * i:64 + 20 * i])
        data = d[off:off + comp]
        if comp != orig: data = zlib.decompress(data)
        assert len(data) == orig
        tables.append((tag, data))
    tables.sort(key=lambda t: t[0])
    es = 0
    while (1 << (es + 1)) <= num: es += 1
    sr = (1 << es) * 16
    out = bytearray(struct.pack('>IHHHH', flavor, num, sr, es, num * 16 - sr))
    off = 12 + 16 * num
    body = bytearray()
    for tag, data in tables:
        pad = (4 - len(data) % 4) % 4
        cs = sum(struct.unpack('>%dI' % ((len(data) + pad) // 4), data + b'\0' * pad)) & 0xffffffff
        out += struct.pack('>4sIII', tag, cs, off + len(body), len(data))
        body += data + b'\0' * pad
    open(dst, 'wb').write(bytes(out) + bytes(body))

if __name__ == '__main__':
    woff2ttf(sys.argv[1], sys.argv[2]); print('wrote', sys.argv[2])

#!/usr/bin/env python3
"""Build assets/models/ict_face/ from a checkout of ICT-FaceKit (MIT, USC ICT).

    git clone --depth 1 --filter=blob:none --sparse https://github.com/ICT-VGL/ICT-FaceKit
    (cd ICT-FaceKit && git sparse-checkout set --no-cone '/*' '!/FaceXModel/identity*')
    python3 tools/prepare_ict_facekit.py ICT-FaceKit/FaceXModel assets/models/ict_face

Outputs
  ict_face.obj        neutral head with named `g` groups (Face, EyebrowL/R, EyeL/R, Eyelashes,
                      TeethUpper/Lower, GumsUpper/Lower, Tongue, EyeShadow, BackHead)
  ict_face.fbs        all 53 ARKit-style blendshapes as sparse deltas (FRBS binary, ~9 MB)
  parts.json          per-part statistics (for docs / tests)
Only stdlib is used (no numpy).
"""
import json, os, struct, sys
from collections import defaultdict, deque

def read_obj(path):
    v, vt, faces, mtl = [], [], [], None
    for line in open(path):
        if line.startswith('v '):
            v.append(tuple(map(float, line.split()[1:4])))
        elif line.startswith('vt '):
            p = line.split(); vt.append((float(p[1]), float(p[2])))
        elif line.startswith('usemtl'):
            mtl = line.split()[1]
        elif line.startswith('f '):
            corners = []
            for tok in line.split()[1:]:
                a = tok.split('/')
                corners.append((int(a[0]) - 1, int(a[1]) - 1 if len(a) > 1 and a[1] else -1))
            faces.append((mtl, corners))
    return v, vt, faces

def connected_components(faces):
    """Split a face list into components by shared vertices. Returns list of face-index lists."""
    vert_to_faces = defaultdict(list)
    for fi, (_, corners) in enumerate(faces):
        for vi, _ in corners: vert_to_faces[vi].append(fi)
    seen = [False] * len(faces); comps = []
    for start in range(len(faces)):
        if seen[start]: continue
        comp = []; q = deque([start]); seen[start] = True
        while q:
            fi = q.popleft(); comp.append(fi)
            for vi, _ in faces[fi][1]:
                for nf in vert_to_faces[vi]:
                    if not seen[nf]: seen[nf] = True; q.append(nf)
        comps.append(comp)
    return comps

def centroid_y(verts, faces, idxs):
    ys = [verts[vi][1] for fi in idxs for vi, _ in faces[fi][1]]
    return sum(ys) / len(ys)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    verts, uvs, faces = read_obj(os.path.join(src, 'generic_neutral_mesh.obj'))
    names = json.load(open(os.path.join(src, 'vertex_indices.json')))['expressions']
    print(f'{len(verts)} verts, {len(faces)} faces, {len(names)} expressions')

    # ---- blendshape deltas --------------------------------------------------------------------
    shapes = {}
    for n in names:
        tv = [tuple(map(float, l.split()[1:4])) for l in open(os.path.join(src, n + '.obj')) if l.startswith('v ')]
        assert len(tv) == len(verts), n
        d = {}
        for i, (a, b) in enumerate(zip(verts, tv)):
            dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
            if abs(dx) + abs(dy) + abs(dz) > 1e-5: d[i] = (dx, dy, dz)
        shapes[n] = d
    with open(os.path.join(dst, 'ict_face.fbs'), 'wb') as f:
        f.write(b'FRBS'); f.write(struct.pack('<III', 1, len(verts), len(names)))
        for n in names:
            d = shapes[n]; idx = sorted(d)
            nb = n.encode(); f.write(struct.pack('<H', len(nb)) + nb + struct.pack('<I', len(idx)))
            f.write(struct.pack('<%dI' % len(idx), *idx))
            f.write(struct.pack('<%df' % (3 * len(idx)), *[c for i in idx for c in d[i]]))

    # ---- part assignment ----------------------------------------------------------------------
    by_mtl = defaultdict(list)
    for fi, (m, _) in enumerate(faces): by_mtl[m].append(fi)
    part_of = {}  # face index -> part name

    def assign(idxs, name):
        for fi in idxs: part_of[fi] = name

    assign(by_mtl['M_BackHead'], 'BackHead')
    assign(by_mtl['M_ScleraLeft'] + by_mtl['M_IrisLeft'], 'EyeL')
    assign(by_mtl['M_ScleraRight'] + by_mtl['M_IrisRight'], 'EyeR')
    assign(by_mtl['M_EyeLashes'], 'Eyelashes')
    assign(by_mtl['M_LacrimalFluid'] + by_mtl['M_EyeBlend'] + by_mtl['M_EyeOcclusion'], 'EyeShadow')

    # teeth: connected components, upper vs lower by centroid height
    sub = [(fi, faces[fi]) for fi in by_mtl['M_Teeth']]
    comps = [[sub[i][0] for i in c] for c in connected_components([f for _, f in sub])]
    ys = sorted(centroid_y(verts, faces, c) for c in comps); mid = (ys[0] + ys[-1]) / 2
    for c in comps: assign(c, 'TeethUpper' if centroid_y(verts, faces, c) >= mid else 'TeethLower')
    print('teeth components:', len(comps), 'split at y =', round(mid, 2))

    # gums + tongue are one welded mesh: split per face with the jawOpen delta. Vertices that
    # travel with the jaw belong to the lower gums / tongue; the tongue is the sub-part that
    # lies inside the dental arch (|x| small, behind the lower teeth front) and moves the most.
    jaw = shapes['jawOpen']
    gt = by_mtl['M_GumsTongue']
    moved = lambda vi: jaw.get(vi, (0, 0, 0))[1]  # vertical displacement (negative = down)
    lower = [fi for fi in gt if sum(moved(vi) for vi, _ in faces[fi][1]) / len(faces[fi][1]) < -0.3]
    upper = [fi for fi in gt if fi not in set(lower)]
    assign(upper, 'GumsUpper')
    # tongue: among lower faces, those whose vertices also move noticeably in the tongue shapes
    # is not available in ARKit set, so use geometry: tongue lies above the lower gum floor and
    # centred (|x| < 2.2 cm), z between 1 and 8 cm.
    def is_tongue(fi):
        cs = [verts[vi] for vi, _ in faces[fi][1]]
        cx = sum(c[0] for c in cs) / len(cs); cy = sum(c[1] for c in cs) / len(cs); cz = sum(c[2] for c in cs) / len(cs)
        return abs(cx) < 2.3 and 1.0 < cz < 8.6 and cy > -4.6
    tongue = [fi for fi in lower if is_tongue(fi)]
    comps = connected_components([faces[fi] for fi in tongue])
    big = max(comps, key=len)
    tongue = [tongue[i] for i in big]
    assign([fi for fi in lower if fi not in set(tongue)], 'GumsLower')
    assign(tongue, 'Tongue')
    print('gums/tongue split: upper', len(upper), 'lower', len(lower) - len(tongue), 'tongue', len(tongue))

    # eyebrows: face-skin faces whose vertices are all strongly moved by the brow shapes
    face_faces = by_mtl['M_Face']
    for side in ('L', 'R'):
        mag = defaultdict(float)
        for sn in ('browInnerUp_' + side, 'browOuterUp_' + side, 'browDown_' + side):
            for i, (dx, dy, dz) in shapes[sn].items(): mag[i] = max(mag[i], abs(dy))
        peak = max(mag.values())
        strong = {i for i, m in mag.items() if m > 0.45 * peak}
        brow = [fi for fi in face_faces if all(vi in strong for vi, _ in faces[fi][1])]
        # keep only the largest connected patch (drops stray forehead triangles)
        comps = connected_components([faces[fi] for fi in brow])
        best = max(comps, key=len)
        assign([brow[i] for i in best], 'Eyebrow' + side)
    assign([fi for fi in face_faces if fi not in part_of], 'Face')

    order = ['Face', 'EyebrowL', 'EyebrowR', 'EyeL', 'EyeR', 'Eyelashes', 'EyeShadow', 'TeethUpper', 'TeethLower', 'GumsUpper', 'GumsLower', 'Tongue', 'BackHead']
    stats = {}
    with open(os.path.join(dst, 'ict_face.obj'), 'w') as f:
        f.write('# ICT-FaceKit generic neutral head (MIT, USC Institute for Creative Technologies)\n')
        f.write('# regrouped into named parts by FacialRigging/tools/prepare_ict_facekit.py\n')
        f.write('# units: cm, Y-up, face looks down +Z\n')
        for x, y, z in verts: f.write(f'v {x:.5f} {y:.5f} {z:.5f}\n')
        for u, v in uvs: f.write(f'vt {u:.5f} {v:.5f}\n')
        for part in order:
            idxs = [fi for fi in range(len(faces)) if part_of.get(fi) == part]
            if not idxs: continue
            vs = {vi for fi in idxs for vi, _ in faces[fi][1]}
            xs = [verts[i][0] for i in vs]; ys = [verts[i][1] for i in vs]; zs = [verts[i][2] for i in vs]
            stats[part] = {'faces': len(idxs), 'verts': len(vs), 'bbox': [[min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]]}
            f.write(f'g {part}\n')
            for fi in idxs:
                f.write('f ' + ' '.join(f'{vi + 1}/{ti + 1}' if ti >= 0 else f'{vi + 1}' for vi, ti in faces[fi][1]) + '\n')
    json.dump({'source': 'https://github.com/ICT-VGL/ICT-FaceKit', 'license': 'MIT', 'parts': stats, 'blendshapes': names}, open(os.path.join(dst, 'parts.json'), 'w'), indent=1)
    for p, s in stats.items(): print(f"{p:12s} faces={s['faces']:6d} verts={s['verts']:6d} y[{s['bbox'][0][1]:6.2f},{s['bbox'][1][1]:6.2f}]")

if __name__ == '__main__':
    main()

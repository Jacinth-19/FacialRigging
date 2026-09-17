# Head models

Real scanned heads fetched from Alec Jacobson's
[common-3d-test-models](https://github.com/alecjacobson/common-3d-test-models) collection
(`data/*.obj`). They are here so the tool can be exercised on real topology instead of the
procedural head. Use `--model assets/models/<name>.obj` with `fr_cli` or `facial_rigging`.

| File | Verts / tris | Origin | Notes |
|------|--------------|--------|-------|
| `max-planck.obj` | 50 077 / 99 991 | Max Planck bust, MPI Informatik (via Princeton/SUGCON test sets) | Y-up, mm units |
| `nefertiti.obj`  | 49 971 / 99 938 | Nefertiti bust scan, Ägyptisches Museum Berlin (via Thingiverse) | **Z-up** – auto-detected and rotated (`--up y` to override) |
| `igea.obj`       | 134 345 / 268 686 | Igea artec/Cyberware scan | metres (tiny coordinates); normalised on load |

Licensing: the upstream collection lists the licence for all three as *"missing / unknown"*.
They are widely used research test models, but **treat them as research-only** and replace
them with properly licensed assets before shipping anything. The loader normalises every
model into a unit box, so any other Y-up OBJ head with a closed-ish mouth works the same way.

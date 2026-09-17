# Implementation status vs. design

| Design area | Implemented | Notes / next steps |
|---|---|---|
| Control-point placement (ray picking, gizmos) | ✅ `src/core/raycast.*`, `src/app/application.cpp` | Drag in camera plane; axis-constrained drag could be added |
| Bindings: bone / blendshape / free-form | ✅ `src/rig/rig.*`, `rbf_deformer.*` | Weight painting UI not yet built (weights are procedural) |
| Skinning + blendshapes | ✅ CPU and GPU (vertex shader, RGB32F delta texture) | Runs on GL 3.3 core **and** GL ES 3.0; compute-shader path pending |
| Default face rig | ✅ Head/Jaw bones, 8 canonical shapes; **authored shapes** via `.fbs` (53 ICT/ARKit) merged into canonical names; **mesh parts** (OBJ groups) drive rigid binding of teeth/gums/tongue/eyes/brows | Weight painting UI still procedural |
| WAV import | ✅ PCM 8/16/24/32 + float | MP3/FLAC via libsndfile/dr_libs later |
| Features: MFCC, pitch, RMS, onsets, centroid | ✅ `src/audio/features.*` | Essentia optional |
| Viseme mapping | ✅ rule-based + phoneme dictionary + `MlVisemeMapper` with **trained weights** `assets/models/viseme_mlp.frvm` (full TIMIT, official TEST split, `fr_train_visemes`: 77.5 % frame acc vs 34.7 % baseline) | `.frvm` runs without LibTorch; TorchScript `.pt` needs `-DFR_WITH_TORCH=ON` |
| Baked clips, playback, scrub | ✅ `src/anim/*`, GUI panel | Speaker playback of the clip not wired; visual-only preview |
| glTF 2.0 export | ✅ GLB / glTF+bin with skin, morph targets, animations | Verified with pygltflib |
| FBX export | ✅ Assimp 5.4.3 writer (default, `src/export/assimp_fbx_exporter.cpp`), FBX SDK still optional | Binary FBX 7.4 with mesh, skin, 8 morph targets, jaw rotation + morph-weight curves; verified by Assimp re-import in tests (see `third_party/patches`) |
| Variations | ✅ `parseVariation` | |
| Arena agent integration | ✅ `tools/arena_task.yaml`, `tools/run_arena_task.py` | |
| Tests | ✅ 32 Catch2 cases (33 with LibTorch) | FBX round-trip, trained mapper sanity, live pipeline on the test-signal device, OBJ parts / FRBS round-trip, ICT rig binding |
| Cross-platform | Linux verified, incl. **headless rendering** via `--headless` (GLFW null platform + EGL pbuffer, SwiftShader/Mesa) | Windows/macOS untested |
| Live microphone input | ✅ `src/audio/live_capture.*` (PortAudio, static submodule) + “Live Microphone” panel / `--live` | ALSA host API compiled in from the `alsa-lib` submodule headers; built-in **test-signal device (-2)** exercises the whole live path without hardware (`fr_cli --live-test -2 2`, unit test). Sandbox has no `libasound.so.2`/devices → real hardware needed to hear audio |
| ML lip-sync | ✅ `MlVisemeMapper` behind `VisemeMapper` (`--mapper ml [--model-pt x.pt]`) | LibTorch is not a submodule (binary dist): point `TORCH_ROOT` at a LibTorch or `torch` wheel dir |
| UI | ✅ AccuRIG-style shell (`src/ui/theme.*`, `panels.cpp`): Inter font, **Material Icons** (submodule, unpacked from WOFF at build time), dark/lime theme, left 5-step wizard incl. **Check Model orientation step** with working **Force Symmetry**, right property page, export modal | |
| Test model with separated parts | ✅ `assets/models/ict_face` (ICT-FaceKit: 13 parts, 53 shapes) | `tools/prepare_ict_facekit.py` regenerates from the upstream repo |
| Audio encryption at rest | ❌ | Only needed once recordings are persisted |

## Running here (sandbox, no GPU / no X11)

```
cmake -S . -B build -G Ninja && cmake --build build
tools/run_headless.sh                                  # SwiftShader GL ES 3.0 -> out/frame_XX.ppm
python3 tools/ppm2png.py out/frame_*.ppm
```

![Nefertiti](images/headless_nefertiti.png)

Known gaps: blendshape weights animate in FBX only through the Assimp patch (validated with
Assimp's own importer, not yet in Maya/Blender); the live mic path is untested with a real
device; the ML mapper's weights are hand-initialised, not trained.

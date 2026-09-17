# Implementation status vs. design

| Design area | Implemented | Notes / next steps |
|---|---|---|
| Control-point placement (ray picking, gizmos) | ✅ `src/core/raycast.*`, `src/app/application.cpp` | Drag in camera plane; axis-constrained drag could be added |
| Bindings: bone / blendshape / free-form | ✅ `src/rig/rig.*`, `rbf_deformer.*` | Weight painting UI not yet built (weights are procedural) |
| Skinning + blendshapes | ✅ CPU and GPU (vertex shader, RGB32F delta texture) | Runs on GL 3.3 core **and** GL ES 3.0; compute-shader path pending |
| Default face rig | ✅ Head/Jaw bones, 8 procedural shapes | Import of authored shapes (OBJ targets) is a natural extension |
| WAV import | ✅ PCM 8/16/24/32 + float | MP3/FLAC via libsndfile/dr_libs later |
| Features: MFCC, pitch, RMS, onsets, centroid | ✅ `src/audio/features.*` | Essentia optional |
| Viseme mapping | ✅ rule-based + phoneme dictionary + `MlVisemeMapper` (LibTorch, `-DFR_WITH_TORCH=ON`) | Built-in hand-initialised MLP works today; train/export real weights with `tools/export_torchscript_mapper.py` |
| Baked clips, playback, scrub | ✅ `src/anim/*`, GUI panel | Speaker playback of the clip not wired; visual-only preview |
| glTF 2.0 export | ✅ GLB / glTF+bin with skin, morph targets, animations | Verified with pygltflib |
| FBX export | ✅ Assimp 5.4.3 writer (default, `src/export/assimp_fbx_exporter.cpp`), FBX SDK still optional | Binary FBX 7.4 with mesh, skin, 8 morph targets, jaw rotation + morph-weight curves; verified by Assimp re-import in tests (see `third_party/patches`) |
| Variations | ✅ `parseVariation` | |
| Arena agent integration | ✅ `tools/arena_task.yaml`, `tools/run_arena_task.py` | |
| Tests | ✅ 25 Catch2 cases (26 with LibTorch) | Includes FBX round-trip, ML mapper, live-capture API |
| Cross-platform | Linux verified, incl. **headless rendering** via `--headless` (GLFW null platform + EGL pbuffer, SwiftShader/Mesa) | Windows/macOS untested |
| Live microphone input | ✅ `src/audio/live_capture.*` (PortAudio, static submodule) + “Live Microphone” panel / `--live` | Ring buffer on the callback thread, per-hop analysis on the main thread. This sandbox has no ALSA headers/devices, so it builds with the OSS host API and reports “no input devices” here – needs a real machine to hear audio |
| ML lip-sync | ✅ `MlVisemeMapper` behind `VisemeMapper` (`--mapper ml [--model-pt x.pt]`) | LibTorch is not a submodule (binary dist): point `TORCH_ROOT` at a LibTorch or `torch` wheel dir |
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

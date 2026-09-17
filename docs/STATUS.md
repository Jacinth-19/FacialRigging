# Implementation status vs. design

| Design area | Implemented | Notes / next steps |
|---|---|---|
| Control-point placement (ray picking, gizmos) | ✅ `src/core/raycast.*`, `src/app/application.cpp` | Drag in camera plane; axis-constrained drag could be added |
| Bindings: bone / blendshape / free-form | ✅ `src/rig/rig.*`, `rbf_deformer.*` | Weight painting UI not yet built (weights are procedural) |
| Skinning + blendshapes | ✅ CPU and GPU (vertex shader, texture buffer deltas) | Compute-shader path pending |
| Default face rig | ✅ Head/Jaw bones, 8 procedural shapes | Import of authored shapes (OBJ targets) is a natural extension |
| WAV import | ✅ PCM 8/16/24/32 + float | MP3/FLAC via libsndfile/dr_libs later |
| Features: MFCC, pitch, RMS, onsets, centroid | ✅ `src/audio/features.*` | Essentia optional |
| Viseme mapping | ✅ rule-based + phoneme dictionary | HMM / neural mappers via `VisemeMapper` interface |
| Baked clips, playback, scrub | ✅ `src/anim/*`, GUI panel | Audio playback (PortAudio) not wired; visual-only preview |
| glTF 2.0 export | ✅ GLB / glTF+bin with skin, morph targets, animations | Verified with pygltflib |
| FBX export | ⚙️ `src/export/fbx_exporter.cpp` compiled only with the SDK | Needs SDK on the build machine to be validated |
| Variations | ✅ `parseVariation` | |
| Arena agent integration | ✅ `tools/arena_task.yaml`, `tools/run_arena_task.py` | |
| Tests | ✅ 21 Catch2 cases | Add FBX round-trip once SDK available |
| Cross-platform | Linux verified (headless build + tests) | Windows/macOS untested |
| Live microphone input | ❌ | PortAudio callback thread |
| ML lip-sync | ❌ | LibTorch model implementing `VisemeMapper` |
| Audio encryption at rest | ❌ | Only needed once recordings are persisted |

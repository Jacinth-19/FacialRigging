# FacialRigging

Interactive, audio-driven facial animation tool in C++17 / OpenGL 3.3 with control-point rigging,
GPU skinning + blendshapes, rule-based **and trained** lip-sync viseme mapping, live microphone
input, and glTF/FBX export. Ships with an ICT-FaceKit head (separated brows/eyes/teeth/tongue,
53 blendshapes), an AccuRIG-style step-wizard UI, a headless CLI and an Arena-agent task runner.

![Check Model step](docs/images/ui_check_model.png)


## Build

```bash
git clone --recursive <repo>            # or: git submodule update --init
cmake -S . -B build -G Ninja            # -DFR_BUILD_APP=OFF for headless-only
cmake --build build
ctest --test-dir build                  # 31 unit tests
```

Dependencies are vendored as submodules (GLFW, Dear ImGui, glm, Catch2, **Assimp**, **PortAudio**,
**alsa-lib** headers, **Material Icons**; see `third_party/patches` for two small local patches).
`python3` is needed at build time (it unpacks the Material Icons webfont and generates the icon
codepoint header from the submodule). On Linux the GUI needs X11 dev headers
(`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`) for a window; without them
the app still builds (GLFW null platform) and runs **headless** (see below). PortAudio is built
against the vendored **alsa-lib** submodule headers (`cmake/FindALSA.cmake`), so the ALSA host API
is always compiled in; at run time it needs `libasound.so.2` (any Linux desktop has it) and falls
back to OSS otherwise. `fr_cli --list-devices` prints the host APIs that were compiled/found.

| CMake option | Default | Effect |
|---|---|---|
| `FR_WITH_ASSIMP` | ON | Real FBX export via Assimp (binary FBX 7.4: mesh, skin, morph targets, bone + morph-weight animation) |
| `FR_WITH_PORTAUDIO` | ON | Live microphone capture (`--live`, “Live Microphone” panel) |
| `FR_WITH_TORCH` + `TORCH_ROOT=<dir>` | OFF | `MlVisemeMapper` (LibTorch TorchScript). `TORCH_ROOT` may be a LibTorch dist or a `torch` pip package dir; the C++ ABI is matched automatically |
| `FR_WITH_FBX_SDK` + `FBX_SDK_ROOT` | OFF | Autodesk FBX SDK writer (takes precedence over Assimp) |

Export selection: FBX SDK → Assimp → glTF (`.glb` fallback only when neither FBX writer is built).

## Run

```bash
build/facial_rigging                                   # procedural head, empty audio
build/facial_rigging --model assets/sample_head.obj --audio assets/sample_speech.wav --generate
build/facial_rigging --model assets/models/nefertiti.obj --generate     # real scanned head (see assets/models/README.md)
build/fr_cli --output out/scene --format fbx --variations 2       # headless CLI, real FBX
build/fr_cli --model assets/models/igea.obj --mapper ml --output out/igea --format fbx   # LibTorch mapper (FR_WITH_TORCH)
build/facial_rigging --model assets/models/ict_face/ict_face.obj --generate   # ICT-FaceKit head, 53 authored shapes
build/fr_cli --list-devices                                        # PortAudio inputs (+ built-in test-signal device -2)
build/fr_cli --live-test -2 2                                      # live pipeline on the synthetic mic for 2 s
build/fr_train_visemes --data /tmp/timit --out assets/models/viseme_mlp.frvm  # retrain the viseme MLP
python3 tools/run_arena_task.py tools/arena_task.yaml            # agent task
```

### Headless / software GL (no display, no GPU)

```bash
tools/run_headless.sh                                   # SwiftShader (bundled, x86_64 Linux) -> out/frame_XX.ppm
tools/run_headless.sh --model assets/models/igea.obj --render-frames 8 --frame-pattern out/igea_%02d.ppm
FR_GL_LIB_DIR=/usr/lib/x86_64-linux-gnu tools/run_headless.sh     # Mesa llvmpipe instead (libegl-mesa0 + libgles2)
python3 tools/ppm2png.py out/*.ppm
```

`--headless` starts GLFW's null platform with an EGL pbuffer and an OpenGL ES 3.0 context; the
renderer and Dear ImGui run unchanged (shaders are written for GL 3.3 core *and* ES 3.0).
`--render-frames N` steps through the generated clip and dumps N frames, which is how the
screenshots in `docs/images/` were produced inside a CPU-only container.

### UI (AccuRIG-style workflow)

The shell follows Reallusion AccuRIG's layout: dark chrome, lime accent, Inter font
(`assets/fonts`), **Material Icons** (Google, Apache-2.0, `third_party/material-icons`; merged
into every ImGui font so `ICON_MD_*` strings render inline), a **left step column**, the viewport
with a vertical tool strip, and a **right property page** for the active step. Steps:

| # | Step | What it does |
|---|---|---|
| 1 | **Load Face** | OBJ path or bundled heads (ICT-FaceKit, scans, procedural); `<model>.fbs` blendshapes are picked up automatically |
| 2 | **Check Model** | Orientation fix *before* rigging: rotate ±90°/180° about X/Y/Z, mirror, auto-detect Z-up, centre-line slider (viewport shows the line), ✓/⚠ checks for "Y up / +Z front / centred", **Force Symmetry** (dragging BrowL also moves BrowR mirrored). **Rig Face** button proceeds |
| 3 | **Face Rig** | Tabs: Handles (control points: add/move/bind), Blendshapes (canonical + authored), Bones, Parts (which part follows the Jaw) |
| 4 | **Lip-sync** | Audio file or Microphone tab, rule-based vs trained-MLP mapper, generator settings, **Generate Animation** |
| 5 | **Check Animation** | Transport, curves, export path/variations, **Export…** modal (FBX / glb / glTF / pose) and log |

Keys: **Q** orbit, **W** add control point, **E** move point, **Space** play/pause, **Ctrl+Z/Y**
undo/redo. `--step N` opens a given step, `--ui-scale F` scales the UI.

| Face Rig | Check Animation |
|---|---|
| ![](docs/images/ui_face_rig.png) | ![](docs/images/ui_check_animation.png) |

### Test model: ICT-FaceKit head

`assets/models/ict_face/` is the ICT-FaceKit generic neutral (MIT-style licence, see
`LICENSE-ICT-FaceKit.txt`) exported by `tools/prepare_ict_facekit.py` as an OBJ with **13 `g`
groups** (Face, EyebrowL/R, EyeL/R, Eyelashes, EyeShadow, TeethUpper/Lower, GumsUpper/Lower,
Tongue, BackHead) plus `ict_face.fbs`, a compact sparse container with all **53 expression
blendshapes**. The rig binds lower teeth/gums/tongue rigidly to the Jaw bone, upper teeth/eyes/brows
to the head, and merges the ARKit-style shapes (`jawOpen`, `mouthSmile_L/R`, `browInnerUp`, …) into
the canonical shapes the generator drives; every source shape is also exposed by name.

### Trained viseme mapper

`assets/models/viseme_mlp.frvm` is **trained** (not hand-set) by `fr_train_visemes` on the
**full TIMIT corpus** (official split: 3696 TRAIN / 1344 TEST utterances after dropping SA1/SA2,
462 + 168 speakers), features from the app's own `FeatureExtractor` (17-dim frame vector ×
7-frame context), MLP 119-128-128-9 with Adam, class-balanced cross-entropy. Frame accuracy on
the official TEST set **77.5 %** (majority-class baseline 34.7 %); confusion matrix in
`assets/models/viseme_mlp.train.log`. The trainer also accepts flat `<speaker>/<utt>.wav+.phn`
subsets (then it holds out 20 % of speakers). The `.frvm` format is plain C++ so the trained
model runs in every build; `FR_WITH_TORCH` additionally allows TorchScript `.pt` models.

## Layout

```
src/core    mesh, OBJ I/O, ray casting, camera        src/render  GL loader, shaders, renderers
src/rig     skeleton, blendshapes, control points, RBF src/ui      ImGui panels
src/audio   WAV, FFT, MFCC/pitch/onsets, visemes       src/app     pipeline, CLI, GUI app
src/anim    clips/curves, lip-sync generator           tools/      Arena task + runner
src/export  glTF writer, FBX SDK writer                tests/      Catch2
```

Docs: [Design](docs/DESIGN.md) · [Status vs design](docs/STATUS.md) · [Agent integration](docs/AGENT.md)

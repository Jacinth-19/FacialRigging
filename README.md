# FacialRigging

Interactive, audio-driven facial animation tool in C++17 / OpenGL 3.3 with control-point rigging,
GPU skinning + blendshapes, a rule-based lip-sync generator, and glTF/FBX export. Includes a
headless CLI and an Arena-agent task runner.


## Build

```bash
git clone --recursive <repo>            # or: git submodule update --init
cmake -S . -B build -G Ninja            # -DFR_BUILD_APP=OFF for headless-only
cmake --build build
ctest --test-dir build                  # 25 unit tests
```

Dependencies are vendored as submodules (GLFW, Dear ImGui, glm, Catch2, **Assimp**, **PortAudio**;
see `third_party/patches` for two small local patches). On Linux the GUI needs X11 dev headers
(`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`) for a window; without them
the app still builds (GLFW null platform) and runs **headless** (see below). PortAudio uses ALSA
when `libasound2-dev` is present, else OSS.

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
build/fr_cli --list-devices                                        # PortAudio inputs
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

GUI: **Q** orbit, **W** add control point (click on mesh), **E** move point (drag handle),
**Space** play/pause, **Ctrl+Z/Y** undo/redo. Panels: Control Points, Rig (blendshape sliders,
bone pose), Audio & Animation (waveform, lip-sync settings, scrub, curves), Export.

## Layout

```
src/core    mesh, OBJ I/O, ray casting, camera        src/render  GL loader, shaders, renderers
src/rig     skeleton, blendshapes, control points, RBF src/ui      ImGui panels
src/audio   WAV, FFT, MFCC/pitch/onsets, visemes       src/app     pipeline, CLI, GUI app
src/anim    clips/curves, lip-sync generator           tools/      Arena task + runner
src/export  glTF writer, FBX SDK writer                tests/      Catch2
```

Docs: [Design](docs/DESIGN.md) · [Status vs design](docs/STATUS.md) · [Agent integration](docs/AGENT.md)

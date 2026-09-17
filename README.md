# FacialRigging

Interactive, audio-driven facial animation tool in C++17 / OpenGL 3.3 with control-point rigging,
GPU skinning + blendshapes, a rule-based lip-sync generator, and glTF/FBX export. Includes a
headless CLI and an Arena-agent task runner.


## Build

```bash
git clone --recursive <repo>            # or: git submodule update --init
cmake -S . -B build -G Ninja            # -DFR_BUILD_APP=OFF for headless-only
cmake --build build
ctest --test-dir build                  # 21 unit tests
```

Dependencies are vendored as submodules (GLFW, Dear ImGui, glm, Catch2). On Linux the GUI needs
X11 dev headers (`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`); without
them the app still builds (GLFW null platform) and `fr_cli`/tests work.

Optional FBX export: `-DFR_WITH_FBX_SDK=ON -DFBX_SDK_ROOT=/path/to/FBX_SDK`. Without the SDK,
`.fbx` requests fall back to `.glb` (glTF 2.0 with skin, morph targets and animation).

## Run

```bash
build/facial_rigging                                   # procedural head, empty audio
build/facial_rigging --model assets/sample_head.obj --audio assets/sample_speech.wav --generate
build/fr_cli --output out/scene --format glb --variations 2       # headless
python3 tools/run_arena_task.py tools/arena_task.yaml            # agent task
```

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

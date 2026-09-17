# Arena AI agentic-mode integration

The application exposes a headless surface so an agent can drive it without a GUI:

* `build/fr_cli` – full pipeline (model → rig → audio → animation → export + variations).
* `build/facial_rigging --export <path>` – same, through the GUI binary (opens a window, exports, quits).
* `tools/run_arena_task.py <task.yaml>` – translates a step-based task file into a CLI run and prints a JSON summary.

## Prompt template

```
Task: Use FacialRigging to create and export animated face files.
Steps:
1. Build the project (cmake -S . -B build && cmake --build build).
2. Load 3D face model from "assets/sample_head.obj" (or omit for the procedural head).
3. Apply the default face rig (Head/Jaw bones, 8 blendshapes, 8 control points).
4. Import audio "assets/sample_speech.wav".
5. Run the audio-to-animation process (rule-based viseme mapper, 30 fps).
6. Export the result as "out/scene.fbx" (falls back to .glb when the FBX SDK is absent).
7. Repeat with 2 variation settings ("Increase smile", "Raise eyebrows").
Metadata: application=fr_cli, modelPath=assets/sample_head.obj, audioFile=assets/sample_speech.wav,
          outputPattern=out/scene, variations=2, exportFormat=FBX
```

## Task file (`tools/arena_task.yaml`)

```yaml
agent_name: FaceAnimator
task: Export animated face from speech
app: build/fr_cli
steps:
  - load_model: assets/sample_head.obj
  - rig_points: {method: default_face_rig}
  - import_audio: assets/sample_speech.wav
  - generate_animation: {model: rule_based_viseme, fps: 30, intensity: 1.0}
  - export: {output: out/scene, format: fbx}
  - generate_variations:
      changes: [Increase smile, Raise eyebrows, intensity=1.4]
metadata: {project: FaceProject, author: TestUser}
```

Run it:

```bash
python3 tools/run_arena_task.py tools/arena_task.yaml
# {"ok": true, "files": ["out/scene.glb", "out/scene_var1_increase_smile.glb", ...], "log": [...]}
```

## Variation grammar

| Text | Effect |
|---|---|
| `Increase smile` / anything containing "smile" | `MouthSmile += 0.35` |
| `Raise eyebrows` / "brow" | `BrowRaise += 0.4` |
| `subtle`, `less`, `calm` | all blend curves × 0.6 |
| `exaggerated`, `more`, `intense` | all blend curves × 1.4 |
| `intensity=1.3` | all blend curves × 1.3 |
| `smile+=0.2`, `brow+=0.1` | explicit offsets |
| `smooth=3` | box-filter radius 3 frames |

## Direct CLI equivalents

```bash
build/fr_cli --model assets/sample_head.obj --audio assets/sample_speech.wav \
             --output out/scene --format fbx \
             --variation "Increase smile" --variation "Raise eyebrows"
build/fr_cli --dump-features > features.csv      # per-frame acoustic features
```

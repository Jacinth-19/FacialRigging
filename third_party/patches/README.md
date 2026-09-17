# Local patches to submodules

Submodules are pinned to upstream tags, plus these small patches (already applied in the
checked-out trees; re-apply with `git -C third_party/<lib> apply ../patches/<file>` after a
fresh `git submodule update`):

| Patch | Submodule | Why |
|-------|-----------|-----|
| `assimp-fbx-exporter.patch` | assimp v5.4.3 | (1) keyframe times were truncated to whole seconds (`int64(ticks/tps)*SECOND`) which produced non-monotonic curves that every importer rejects; now sub-second precise. (2) writes blendshape weight animation (`AnimationCurveNode` → `BlendShapeChannel.DeformPercent`), which upstream omits. |
| `glfw-null-platform-egl-pbuffer.patch` | glfw 3.4 | With `GLFW_PLATFORM_NULL` + `GLFW_EGL_CONTEXT_API`, create an EGL **pbuffer** surface (and accept pbuffer configs) instead of failing on a null native window. Enables fully headless rendering with SwiftShader / Mesa. |

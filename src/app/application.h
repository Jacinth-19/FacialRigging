#pragma once
#include "app/pipeline.h"
#include "audio/live_capture.h"
#include "core/camera.h"
#include "core/raycast.h"
#include "render/gizmo_renderer.h"
#include "render/mesh_renderer.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

namespace fr {

struct UndoState { std::vector<ControlPoint> controlPoints; std::vector<float> blendWeights; };

/// Interactive tool: viewport + ImGui panels. Owns a Pipeline (rig/audio/clip) and edits it.
class Application {
public:
    struct Options {
        std::string modelPath, audioPath, shaderDir, assetDir, iconFontPath;
        int width = 1440, height = 900;
        bool autoGenerate = false;        ///< run lipsync right after start
        std::string exportOnStart;        ///< if set: export to this path and quit (agent mode)
        std::vector<std::string> variations;
        bool headless = false;            ///< GLFW null platform + EGL pbuffer (SwiftShader/Mesa) - no window
        bool useGLES = false;             ///< request an OpenGL ES 3.0 context (implied by headless)
        int renderFrames = 0;             ///< >0: render N frames of the clip to PPM files and quit
        std::string framePattern = "out/frame_%03d.ppm";
        std::string mapper = "rules";     ///< "rules" | "ml"
        std::string modelPt;              ///< TorchScript for the ML mapper
        std::string upAxis = "auto";      ///< auto|y|z for imported OBJ
        bool live = false;                ///< start microphone capture on launch
        float uiScale = 1.0f;             ///< extra UI scale on top of the monitor content scale
        int startStep = -1;               ///< wizard step to open (0..4), -1 = automatic
        std::string emotion;              ///< performance-layer emotion preset for generation
        float emotionAmount = 0.8f;
        float headMotion = -1.0f, gazeMotion = -1.0f; ///< <0 = keep defaults
        int shadeMode = 0;                ///< 0 lit, 1 normals, 2 bone weights, 3 shape influence, 4 displacement
        float gazeYaw = 0.0f, gazePitch = 0.0f; ///< initial eye pose (deg) when the model has eyeballs
        int msaa = 4;                     ///< viewport anti-aliasing samples (0 = off); clamped to GL_MAX_SAMPLES
    };
    explicit Application(Options o) : opts_(std::move(o)) {}
    int run();

    // --- state shared with UI panels -------------------------------------------------
    Pipeline pipe;
    OrbitCamera camera;
    MeshRenderer meshRenderer;
    GizmoRenderer gizmos;
    enum class Tool { Orbit, AddPoint, MovePoint };
    Tool tool = Tool::Orbit;
    BindingType newPointBinding = BindingType::FreeForm;
    int newPointShape = 0;
    float newPointRadius = 0.15f;
    int selectedPoint = -1;
    bool showBones = true, showPoints = true, showLabels = true;
    // playback
    bool playing = false; float playTime = 0.0f; bool loop = true;
    // status
    std::string status, exportPath = "out/scene.glb";
    std::vector<UndoState> undo_, redo_;
    void pushUndo();
    void undo(); void redo();
    void reuploadMesh();
    void loadModel(const std::string& path);
    void loadAudio(const std::string& path);
    void generate();
    void exportNow(const std::string& path, const std::vector<std::string>& variations);
    const Options& options() const { return opts_; }
    /// Reads back the current framebuffer (RGB8) and writes a binary PPM.
    bool saveFrame(const std::string& path) const;
    // live microphone
    LiveCapture live;
    bool liveEnabled = false; std::string liveError; std::vector<float> liveWave; VisemeFrame liveViseme;
    void toggleLive(int device = -1);
    glm::ivec2 viewportSize() const { return fbSize_; }
    int msaaSamples = 4;                  ///< requested; change at runtime (View menu), applied next frame
    int msaaActive() const { return msaaActive_; }
    int msaaMax() const { return msaaMax_; }
    /// Projects a world point to window pixels (for labels).
    bool project(const glm::vec3& world, glm::vec2& px) const;

private:
    Options opts_;
    GLFWwindow* window_ = nullptr;
    glm::ivec2 fbSize_{1, 1};
    glm::mat4 view_{1.0f}, proj_{1.0f};
    glm::dvec2 lastMouse_{0, 0};
    bool dragging_ = false; glm::vec3 dragPlaneN_{0, 0, 1}; glm::vec3 dragStartWorld_{0}; glm::vec3 dragStartOffset_{0};
    double lastFrameTime_ = 0.0;

    // Offscreen multisampled target for the 3D viewport; resolved (blit) into the default
    // framebuffer before the UI is drawn. Works on desktop GL and GL ES 3.0 (SwiftShader), where
    // multisampled default framebuffers are unavailable.
    GLuint msaaFbo_ = 0, msaaColor_ = 0, msaaDepth_ = 0; glm::ivec2 msaaSize_{0, 0}; int msaaActive_ = 0, msaaMax_ = 0;
    bool ensureMsaaTarget();
    void destroyMsaaTarget();

    bool initWindow();
    void frame();
    void handleViewportInput();
    void drawScene();
    void drawGizmos();
    Ray mouseRay() const;
    int pickControlPoint(const Ray& r, float pixelRadius) const;
};

} // namespace fr

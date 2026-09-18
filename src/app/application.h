#pragma once
#include "app/pipeline.h"
#include "rig/rig_tools.h"
#include "audio/live_capture.h"
#include "export/arkit_livelink.h"
#include "core/camera.h"
#include "core/raycast.h"
#include "render/gizmo_renderer.h"
#include "render/mesh_renderer.h"
#include "render/video_export.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

namespace fr {

/// Full rig-state snapshot: skeleton pose + structure, skin weights, blendshapes (sparse), handles,
/// correctives and the clip. The mesh itself is shared (never edited in place) so a snapshot costs
/// roughly skin (32 B/vertex) + shapes; the stack is trimmed by an approximate byte budget.
struct UndoState {
    std::string label;
    Skeleton skeleton; std::vector<VertexInfluence> skin; std::vector<BlendShape> blendShapes;
    std::vector<ControlPoint> controlPoints; std::vector<CombinationShape> combinations; bool skinFirst = true;
    AnimationClip clip;
    bool meshChanged = false;   ///< the mesh (vertex positions) changed too - stored in `mesh`
    std::vector<glm::vec3> meshPositions;
    size_t bytes() const;
};

/// Interactive tool: viewport + ImGui panels. Owns a Pipeline (rig/audio/clip) and edits it.
class Application {
public:
    struct Options {
        std::string modelPath, audioPath, shaderDir, assetDir, iconFontPath, projectPath;
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
        int rigTab = -1;                  ///< Face Rig tab to open (0 handles, 1 weights, 2 correctives, 3 shapes, 4 bones)
        int paintBone = -1;
        int keysDemoChannel = 0;          ///< --keys-demo: channel index to show (0 = JawOpen; larger = bone axes)
        glm::vec3 lookAt{0.0f}; bool haveLookAt = false;   ///< --look-at x y z: camera target (mesh space)
        float zoom = 1.0f;                ///< camera distance multiplier at start (<1 = closer)
        bool clean = false;               ///< hide handles / bones / labels (review renders)
        std::string videoOut; float videoOrbit = 0.0f; int videoW = 1280, videoH = 720; float videoSeconds = -1.0f;   ///< --video out.mp4 [--orbit deg] [--video-size WxH] [--video-seconds s]
        bool keysDemo = false;            ///< seed a few key-layer keys on JawOpen + Head X and open the timeline in key mode (screenshots/tests)
        bool paintDemo = false;           ///< with paintBone: apply a scripted brush stroke across the cheek (headless demo/test)               ///< select the paint tool on this bone at start (screenshots / demos)
        std::string emotion;              ///< performance-layer emotion preset for generation
        float emotionAmount = 0.8f;
        std::string transcript;
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
    enum class Tool { Orbit, AddPoint, MovePoint, PaintWeights };
    WeightBrush brush;                 ///< paint-weights tool settings
    VertexAdjacency adjacency;         ///< built on model load (smooth brush)
    MirrorMap mirrorMap;               ///< built on model load (mirror weights / bake L-R)
    bool brushHover = false; glm::vec3 brushPos{0}, brushNormal{0, 0, 1};
    int brushStrokeChanges = 0;
    void applyBrushDab(const glm::vec3& centre);
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
    void pushUndo(const char* label = "edit", bool meshChanged = false);
    void undo(); void redo();
    bool canUndo() const { return !undo_.empty(); } bool canRedo() const { return !redo_.empty(); }
    const char* undoLabel() const { return undo_.empty() ? "" : undo_.back().label.c_str(); }
    const char* redoLabel() const { return redo_.empty() ? "" : redo_.back().label.c_str(); }
    void reuploadWeights();            ///< skin weights only (after painting)
    void reuploadMesh();
    void loadModel(const std::string& path);
    void loadAudio(const std::string& path);
    void generate();
    void exportNow(const std::string& path, const std::vector<std::string>& variations);
    bool projectLoadedOnStart = false;   ///< --project succeeded (UI marks all steps done, jumps to the animation page)
    /// Turntable / playback video (see render/video_export.h). Renders the scene without UI or gizmos
    /// (handles/bones only when `withGizmos`), restores the rig pose afterwards. Blocks while encoding.
    /// Camera bookmarks: 6 presets (front, 3/4 L/R, side L/R, top) + 4 user slots (Ctrl+F1..F4 store, F1..F4 recall). Smoothly animated.
    struct CamPose { float yaw = 0, pitch = 0, distance = 0; glm::vec3 target{0}; bool set = false; };
    CamPose userCams[4];
    void goToPreset(int preset);                 ///< 0 front 1 3/4 left 2 3/4 right 3 left 4 right 5 top 6 back
    void storeUserCam(int slot); bool recallUserCam(int slot);
    void animateCameraTo(const CamPose& p, float seconds = 0.35f);
    CamPose camFrom_, camTo_; float camAnimT_ = 1.0f, camAnimDur_ = 0.35f;
    bool renderVideo(const std::string& path, const VideoSettings& settings, bool withGizmos = false, const std::string& imageSequencePattern = "");
    VideoSettings videoSettings; bool videoWithGizmos = false; float videoProgress = -1.0f;   ///< GUI state
    bool saveProject(const std::string& path);
    bool loadProject(const std::string& path);
    // ARKit Live Link streaming (item: mocap-style UDP output). Sends a frame whenever the rig pose changes during playback / live mic / manual posing.
    bool liveLinkStart(const std::string& host, int port);
    void liveLinkStop();
    bool liveLinkActive() const { return liveLink_.isOpen(); }
    uint64_t liveLinkFrames() const { return liveLink_.framesSent(); }
    int liveLinkMapped() const { return arkitMap_.mappedCount(); }
    void liveLinkTick();
    ArkitMapping arkitMap_; LiveLinkSender liveLink_; uint32_t liveLinkFrame_ = 0; double liveLinkNext_ = 0.0;
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
    glm::vec3 lastBrush_{0}; size_t adjacencyFor_ = 0;
    int pickControlPoint(const Ray& r, float pixelRadius) const;
};

} // namespace fr

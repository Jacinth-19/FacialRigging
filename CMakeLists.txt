cmake_minimum_required(VERSION 3.20)
project(FacialRigging VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FR_BUILD_APP     "Build the interactive OpenGL/ImGui application" ON)
option(FR_BUILD_TESTS   "Build unit tests (Catch2)" ON)
option(FR_WITH_FBX_SDK  "Link the Autodesk FBX SDK (set FBX_SDK_ROOT)" OFF)
option(FR_WITH_ASSIMP   "Build the Assimp-based FBX exporter" ON)
option(FR_WITH_PORTAUDIO "Build live microphone capture with PortAudio" ON)
option(FR_WITH_TORCH    "Build the LibTorch ML viseme mapper (set Torch_DIR or TORCH_ROOT)" OFF)
set(TORCH_ROOT "" CACHE PATH "Root of a LibTorch / torch python package (contains lib/ and include/)")

# The C++ ABI must match LibTorch for *every* target (assimp, portaudio, ours), so decide it here.
if(FR_WITH_TORCH AND TORCH_ROOT AND NOT DEFINED FR_TORCH_CXX11_ABI)
  file(STRINGS ${TORCH_ROOT}/share/cmake/Torch/TorchConfig.cmake _abi REGEX "GLIBCXX_USE_CXX11_ABI=([01])")
  if(_abi MATCHES "GLIBCXX_USE_CXX11_ABI=([01])")
    set(FR_TORCH_CXX11_ABI ${CMAKE_MATCH_1})
  else()
    set(FR_TORCH_CXX11_ABI 1)
  endif()
endif()
if(FR_WITH_TORCH AND TORCH_ROOT)
  add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=${FR_TORCH_CXX11_ABI})
endif()
set(FBX_SDK_ROOT "" CACHE PATH "Root of an installed Autodesk FBX SDK")

# Warning flags applied only to first-party targets (not to vendored third_party code).
add_library(fr_warnings INTERFACE)
if(MSVC)
  target_compile_options(fr_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(fr_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- third party
add_subdirectory(third_party/glm SYSTEM)

# ---------------------------------------------------------------- core library (headless)
add_library(fr_core STATIC
  src/core/mesh.cpp
  src/core/obj_io.cpp
  src/core/raycast.cpp
  src/core/camera.cpp
  src/rig/rig.cpp
  src/rig/rig_tools.cpp
  src/rig/blendshape_io.cpp
  src/rig/rbf_deformer.cpp
  src/audio/wav_io.cpp
  src/audio/fft.cpp
  src/audio/features.cpp
  src/audio/viseme_mapper.cpp
  src/audio/ml_viseme_mapper.cpp
  src/audio/live_capture.cpp
  src/anim/animation_clip.cpp
  src/anim/lipsync_generator.cpp
  src/export/gltf_exporter.cpp
  src/export/json_clip_exporter.cpp
  src/export/arkit_livelink.cpp
  src/anim/coarticulation.cpp
  src/anim/idle_motion.cpp
  src/audio/g2p.cpp
  src/audio/phoneme_aligner.cpp
  src/export/exporter_factory.cpp
  src/app/pipeline.cpp
  src/app/project.cpp
  src/anim/key_layer.cpp
)
target_include_directories(fr_core PUBLIC src)
target_link_libraries(fr_core PUBLIC glm::glm PRIVATE fr_warnings)
target_compile_definitions(fr_core PUBLIC GLM_ENABLE_EXPERIMENTAL FR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

if(FR_WITH_FBX_SDK)
  if(NOT FBX_SDK_ROOT)
    message(FATAL_ERROR "FR_WITH_FBX_SDK=ON requires FBX_SDK_ROOT")
  endif()
  find_path(FBX_INCLUDE_DIR fbxsdk.h HINTS ${FBX_SDK_ROOT}/include REQUIRED)
  find_library(FBX_LIBRARY NAMES fbxsdk libfbxsdk
    HINTS ${FBX_SDK_ROOT}/lib ${FBX_SDK_ROOT}/lib/gcc/x64/release
          ${FBX_SDK_ROOT}/lib/vs2019/x64/release ${FBX_SDK_ROOT}/lib/clang/release REQUIRED)
  target_sources(fr_core PRIVATE src/export/fbx_exporter.cpp)
  target_include_directories(fr_core PRIVATE ${FBX_INCLUDE_DIR})
  target_link_libraries(fr_core PUBLIC ${FBX_LIBRARY})
  target_compile_definitions(fr_core PUBLIC FR_HAVE_FBX_SDK=1)
  if(UNIX AND NOT APPLE)
    target_link_libraries(fr_core PUBLIC xml2 z dl pthread)
  endif()
else()
  target_sources(fr_core PRIVATE src/export/fbx_exporter_stub.cpp)
  target_compile_definitions(fr_core PUBLIC FR_HAVE_FBX_SDK=0)
endif()

if(FR_WITH_ASSIMP)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_FBX_EXPORTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
  set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/assimp SYSTEM EXCLUDE_FROM_ALL)
  target_sources(fr_core PRIVATE src/export/assimp_fbx_exporter.cpp src/core/scene_import.cpp)
  target_link_libraries(fr_core PUBLIC assimp::assimp)
  target_compile_definitions(fr_core PUBLIC FR_HAVE_ASSIMP=1)
else()
  target_sources(fr_core PRIVATE src/export/assimp_fbx_exporter_stub.cpp src/core/scene_import_stub.cpp)
  target_compile_definitions(fr_core PUBLIC FR_HAVE_ASSIMP=0)
endif()

if(FR_WITH_PORTAUDIO)
  set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PA_ENABLE_DEBUG_OUTPUT OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    # ALSA backend is always compiled, using the vendored alsa-lib headers (third_party/alsa-lib)
    # and PortAudio's dlopen mode: libasound.so.2 is loaded at runtime if present, so no
    # libasound2-dev is needed to build and the binary still works on machines without ALSA.
    set(FR_ALSA_GEN ${CMAKE_BINARY_DIR}/alsa-include)
    set(FR_ALSA_SRC ${CMAKE_SOURCE_DIR}/third_party/alsa-lib/include)
    if(EXISTS ${FR_ALSA_SRC}/asoundlib-head.h)
      file(MAKE_DIRECTORY ${FR_ALSA_GEN}/alsa)
      file(GLOB _alsa_headers ${FR_ALSA_SRC}/*.h)
      foreach(h ${_alsa_headers})
        get_filename_component(_n ${h} NAME)
        configure_file(${h} ${FR_ALSA_GEN}/alsa/${_n} COPYONLY)
      endforeach()
      file(READ ${FR_ALSA_SRC}/asoundlib-head.h _head)
      file(READ ${FR_ALSA_SRC}/asoundlib-tail.h _tail)
      file(WRITE ${FR_ALSA_GEN}/alsa/asoundlib.h "${_head}
#include <alsa/asoundef.h>
#include <alsa/version.h>
#include <alsa/global.h>
#include <alsa/input.h>
#include <alsa/output.h>
#include <alsa/error.h>
#include <alsa/conf.h>
#include <alsa/pcm.h>
#include <alsa/rawmidi.h>
#include <alsa/ump.h>
#include <alsa/timer.h>
#include <alsa/hwdep.h>
#include <alsa/control.h>
#include <alsa/mixer.h>
#include <alsa/seq_event.h>
#include <alsa/seq.h>
#include <alsa/seqmid.h>
#include <alsa/seq_midi_event.h>
${_tail}")
      file(WRITE ${FR_ALSA_GEN}/alsa/version.h "/* generated by FacialRigging CMake from alsa-lib configure.ac */
#define SND_LIB_MAJOR 1
#define SND_LIB_MINOR 2
#define SND_LIB_SUBMINOR 16
#define SND_LIB_EXTRAVER 1000000
#define SND_LIB_VER(maj, min, sub) (((maj)<<16)|((min)<<8)|(sub))
#define SND_LIB_VERSION SND_LIB_VER(SND_LIB_MAJOR, SND_LIB_MINOR, SND_LIB_SUBMINOR)
#define SND_LIB_VERSION_STR \"1.2.16.1\"
")
      list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)   # our FindALSA.cmake
      set(PA_USE_ALSA ON CACHE BOOL "" FORCE)
      set(PA_ALSA_DYNAMIC ON CACHE BOOL "" FORCE)
      message(STATUS "PortAudio: ALSA backend from vendored alsa-lib headers (dlopen libasound.so.2 at runtime)")
    else()
      message(WARNING "third_party/alsa-lib is missing (git submodule update --init) - PortAudio built without ALSA")
      set(PA_USE_ALSA OFF CACHE BOOL "" FORCE)
    endif()
    set(PA_USE_OSS ON CACHE BOOL "" FORCE)
    set(PA_USE_JACK OFF CACHE BOOL "" FORCE)
  endif()
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  add_subdirectory(third_party/portaudio SYSTEM EXCLUDE_FROM_ALL)
  unset(CMAKE_POLICY_VERSION_MINIMUM)
  target_link_libraries(fr_core PUBLIC portaudio_static)
  target_compile_definitions(fr_core PUBLIC FR_HAVE_PORTAUDIO=1)
else()
  target_compile_definitions(fr_core PUBLIC FR_HAVE_PORTAUDIO=0)
endif()

if(FR_WITH_TORCH)
  # Two ways to point at LibTorch: a proper LibTorch dist (Torch_DIR / CMAKE_PREFIX_PATH) or a
  # `torch` python package directory via TORCH_ROOT. The latter is linked manually against
  # libtorch_cpu + libc10 only, so CPU wheels that were built with CUDA still work without a GPU.
  if(TORCH_ROOT)
    find_library(FR_TORCH_CPU torch_cpu HINTS ${TORCH_ROOT}/lib REQUIRED)
    find_library(FR_C10 c10 HINTS ${TORCH_ROOT}/lib REQUIRED)
    add_library(fr_torch INTERFACE)
    target_include_directories(fr_torch SYSTEM INTERFACE ${TORCH_ROOT}/include ${TORCH_ROOT}/include/torch/csrc/api/include)
    target_link_libraries(fr_torch INTERFACE ${FR_TORCH_CPU} ${FR_C10})
    target_link_options(fr_torch INTERFACE "-Wl,-rpath,${TORCH_ROOT}/lib")
    # Match the wheel's C++ ABI (PyPI wheels < 2.5 use the old ABI; LibTorch cxx11 dists use the new one).
    if(NOT DEFINED FR_TORCH_CXX11_ABI)
      file(STRINGS ${TORCH_ROOT}/share/cmake/Torch/TorchConfig.cmake _abi REGEX "GLIBCXX_USE_CXX11_ABI=([01])")
      if(_abi MATCHES "GLIBCXX_USE_CXX11_ABI=([01])")
        set(FR_TORCH_CXX11_ABI ${CMAKE_MATCH_1})
      else()
        set(FR_TORCH_CXX11_ABI 1)
      endif()
    endif()
    message(STATUS "LibTorch from TORCH_ROOT=${TORCH_ROOT} (cxx11 ABI ${FR_TORCH_CXX11_ABI})")
    target_link_libraries(fr_core PUBLIC fr_torch)
  else()
    find_package(Torch REQUIRED)
    target_link_libraries(fr_core PUBLIC ${TORCH_LIBRARIES})
    target_compile_options(fr_core PUBLIC ${TORCH_CXX_FLAGS})
  endif()
  target_compile_definitions(fr_core PUBLIC FR_HAVE_TORCH=1)
else()
  target_compile_definitions(fr_core PUBLIC FR_HAVE_TORCH=0)
endif()

# ---------------------------------------------------------------- headless CLI (always built)
add_executable(fr_cli src/app/main_cli.cpp)
target_link_libraries(fr_cli PRIVATE fr_core fr_warnings)

# ---------------------------------------------------------------- interactive app
if(FR_BUILD_APP)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    # Auto-disable window-system backends whose dev headers are missing so the
    # project still configures (null platform) on minimal CI boxes.
    find_path(FR_X11_INC X11/Xlib.h)
    find_program(FR_WAYLAND_SCANNER wayland-scanner)
    if(NOT FR_X11_INC)
      message(WARNING "X11 headers not found - building GLFW without X11 (install xorg-dev / libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev for a usable window)")
      set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    endif()
    if(NOT FR_WAYLAND_SCANNER)
      set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
  endif()
  add_subdirectory(third_party/glfw SYSTEM)

  set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
  add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp src/render/imgui_impl_opengl3_custom.cpp src/render/gl.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)
  target_include_directories(imgui PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)

  # Material Icons (google/material-design-icons via the marella/material-icons mirror submodule):
  # unpack the TrueType webfont into the build tree and generate the codepoint header.
  set(FR_ICON_FONT ${CMAKE_BINARY_DIR}/fonts/MaterialIcons-Regular.ttf)
  add_custom_command(OUTPUT ${FR_ICON_FONT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/fonts
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/woff2ttf.py ${CMAKE_SOURCE_DIR}/third_party/material-icons/iconfont/material-icons.woff ${FR_ICON_FONT}
    DEPENDS ${CMAKE_SOURCE_DIR}/third_party/material-icons/iconfont/material-icons.woff ${CMAKE_SOURCE_DIR}/tools/woff2ttf.py
    COMMENT "Unpacking Material Icons font")
  add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/src/ui/icons_material.h
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/gen_icon_header.py ${CMAKE_SOURCE_DIR}/third_party/material-icons/_data/codepoints.json ${CMAKE_SOURCE_DIR}/src/ui/icons_material.h
    DEPENDS ${CMAKE_SOURCE_DIR}/third_party/material-icons/_data/codepoints.json ${CMAKE_SOURCE_DIR}/tools/gen_icon_header.py
    COMMENT "Generating Material Icons codepoint header")
  add_custom_target(fr_icon_font DEPENDS ${FR_ICON_FONT} ${CMAKE_SOURCE_DIR}/src/ui/icons_material.h)

  add_executable(facial_rigging
    src/app/main.cpp
    src/app/application.cpp
    src/render/shader.cpp
    src/render/mesh_renderer.cpp
    src/render/video_export.cpp
    src/render/reference.cpp
    src/render/gizmo_renderer.cpp
    src/ui/panels.cpp
    src/ui/theme.cpp)
  add_dependencies(facial_rigging fr_icon_font)
  target_link_libraries(facial_rigging PRIVATE fr_core imgui glfw fr_warnings)
  target_include_directories(facial_rigging PRIVATE third_party/stb)
  target_compile_definitions(facial_rigging PRIVATE
    FR_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    FR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
    FR_ICON_FONT="${FR_ICON_FONT}")
  if(APPLE)
    target_link_libraries(facial_rigging PRIVATE "-framework OpenGL")
  endif()
endif()

# ---------------------------------------------------------------- tests
if(FR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(third_party/Catch2 SYSTEM)
  add_executable(fr_tests
    tests/test_mesh_raycast.cpp
    tests/test_rig.cpp
    tests/test_audio.cpp
    tests/test_anim_export.cpp
    tests/test_ml_live.cpp
    tests/test_parts.cpp
    tests/test_features.cpp
    tests/test_speech_model.cpp
    tests/test_rig_tools.cpp
    tests/test_scene_import.cpp
    tests/test_arkit.cpp
    tests/test_project.cpp
    tests/test_key_layer.cpp)
  target_link_libraries(fr_tests PRIVATE fr_core Catch2::Catch2WithMain fr_warnings)
  target_compile_definitions(fr_tests PRIVATE FR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
endif()

# --- viseme model trainer (TIMIT-style phone alignments -> assets/models/viseme_mlp.frvm)
option(FR_BUILD_TRAINER "Build the fr_train_visemes tool" ON)
if(FR_BUILD_TRAINER)
  add_executable(fr_train_visemes tools/train_visemes.cpp)
  target_link_libraries(fr_train_visemes PRIVATE fr_core fr_warnings)
  add_executable(fr_eval_alignment tools/eval_alignment.cpp)
  target_link_libraries(fr_eval_alignment PRIVATE fr_core fr_warnings)
  target_compile_definitions(fr_eval_alignment PRIVATE FR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/Catch2/extras)
  include(Catch)
  catch_discover_tests(fr_tests)
endif()

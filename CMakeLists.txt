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
  src/rig/rbf_deformer.cpp
  src/audio/wav_io.cpp
  src/audio/fft.cpp
  src/audio/features.cpp
  src/audio/viseme_mapper.cpp
  src/anim/animation_clip.cpp
  src/anim/lipsync_generator.cpp
  src/export/gltf_exporter.cpp
  src/export/exporter_factory.cpp
  src/app/pipeline.cpp
)
target_include_directories(fr_core PUBLIC src)
target_link_libraries(fr_core PUBLIC glm::glm PRIVATE fr_warnings)
target_compile_definitions(fr_core PUBLIC GLM_ENABLE_EXPERIMENTAL)

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
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)
  target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)

  add_executable(facial_rigging
    src/app/main.cpp
    src/app/application.cpp
    src/render/gl.cpp
    src/render/shader.cpp
    src/render/mesh_renderer.cpp
    src/render/gizmo_renderer.cpp
    src/ui/panels.cpp)
  target_link_libraries(facial_rigging PRIVATE fr_core imgui glfw fr_warnings)
  target_compile_definitions(facial_rigging PRIVATE
    FR_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    FR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
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
    tests/test_anim_export.cpp)
  target_link_libraries(fr_tests PRIVATE fr_core Catch2::Catch2WithMain fr_warnings)
  target_compile_definitions(fr_tests PRIVATE FR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/Catch2/extras)
  include(Catch)
  catch_discover_tests(fr_tests)
endif()

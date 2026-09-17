#!/usr/bin/env bash
# Run the interactive app without a display, using a software GL ES 3.0 driver.
# Default driver: the SwiftShader libEGL/libGLESv2 shipped in tools/swiftshader (x86_64 Linux).
# To use Mesa instead: export FR_GL_LIB_DIR=/path/containing/libEGL.so.1 (e.g. /usr/lib/x86_64-linux-gnu
# with libegl-mesa0 + libgles2 installed; LIBGL_ALWAYS_SOFTWARE=1 selects llvmpipe).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
BIN="${FR_BIN:-$ROOT/build/facial_rigging}"
LIBDIR="${FR_GL_LIB_DIR:-$HERE/swiftshader}"
export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBGL_ALWAYS_SOFTWARE=1
if [ $# -eq 0 ]; then
  set -- --headless --render-frames 6 --frame-pattern "$ROOT/out/frame_%02d.ppm" --model "$ROOT/assets/models/max-planck.obj"
fi
mkdir -p "$ROOT/out"
exec "$BIN" --headless "$@"

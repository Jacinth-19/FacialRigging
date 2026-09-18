#!/usr/bin/env bash
# One-shot environment restore: build tools, pinned submodules, local patches, configure, build, test.
# Idempotent - safe to re-run after a sandbox wipe (`tools/bootstrap.sh`) or on a fresh clone.
#
#   tools/bootstrap.sh              # restore + build + test
#   tools/bootstrap.sh --no-test    # skip ctest
#   tools/bootstrap.sh --timit      # additionally fetch the TIMIT corpus into data/timit (1.3 GB, gitignored)
#   tools/bootstrap.sh --torch      # additionally fetch LibTorch (CPU wheel) into third_party/libtorch (gitignored)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
RUN_TESTS=1 WANT_TIMIT=0 WANT_TORCH=0 WANT_CREMA=0
for a in "$@"; do case "$a" in --no-test) RUN_TESTS=0;; --timit) WANT_TIMIT=1;; --torch) WANT_TORCH=1;; --crema) WANT_CREMA=1;; esac; done
log() { printf '\033[1;32m[bootstrap]\033[0m %s\n' "$*"; }

# --- 1. build tools (cmake/ninja via pip when the system has none) ------------------------------
export PATH="$HOME/.local/bin:$PATH"
if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
  log "installing cmake + ninja into ~/.local (pip)"
  pip install --user -q --break-system-packages cmake ninja 2>/dev/null || pip install --user -q cmake ninja
fi
command -v g++ >/dev/null || { echo "g++ not found - install build-essential"; exit 1; }
command -v python3 >/dev/null || { echo "python3 not found"; exit 1; }

# --- 2. submodules at the pinned commits (shallow, tolerant of an empty checkout) ---------------
log "restoring submodules"
git submodule sync -q
if ! git submodule update --init --depth 1 2>/dev/null; then
  # --depth 1 fails when the pinned commit is not a branch tip; fetch the exact commit instead.
  git submodule foreach -q --recursive 'true' >/dev/null 2>&1 || true
  while read -r _sha path _rest; do
    sha=${_sha#-}; sha=${sha#+}
    url=$(git config -f .gitmodules "submodule.$path.url")
    if [ ! -e "$path/.git" ]; then
      rm -rf "$path"; git clone -q --no-checkout "$url" "$path"
    fi
    (cd "$path" && (git cat-file -e "$sha" 2>/dev/null || git fetch -q --depth 1 origin "$sha" || git fetch -q origin) && git checkout -q -f "$sha")
  done < <(git submodule status)
fi

# --- 3. local patches (see third_party/patches/README.md) --------------------------------------
apply_patch() { # apply_patch <submodule> <patch>
  if git -C "third_party/$1" apply --check "../patches/$2" 2>/dev/null; then
    git -C "third_party/$1" apply "../patches/$2"; log "patched $1 ($2)"
  elif git -C "third_party/$1" apply --check -R "../patches/$2" 2>/dev/null; then
    : # already applied
  else
    echo "WARNING: could not apply $2 to $1"; fi
}
apply_patch assimp assimp-fbx-exporter.patch
apply_patch glfw glfw-null-platform-egl-pbuffer.patch

# --- 4. optional big downloads (gitignored) ----------------------------------------------------
if [ "$WANT_TIMIT" = 1 ] && [ ! -d data/timit/TRAIN ]; then
  log "fetching TIMIT (tqrx-s/TIMIT mirror, sparse, ~1.3 GB)"
  mkdir -p data; rm -rf data/.timit_tmp
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/tqrx-s/TIMIT.git data/.timit_tmp
  git -C data/.timit_tmp sparse-checkout set data
  mv data/.timit_tmp/data data/timit; rm -rf data/.timit_tmp
fi
if [ ! -f data/models/shape_predictor_68_face_landmarks.dat ]; then
  log "fetching dlib 68-point shape predictor (99 MB) for auto-landmarking"
  mkdir -p data/models; rm -rf data/.lm_tmp
  if git clone -q --depth 1 https://github.com/italojs/facial-landmarks-recognition data/.lm_tmp 2>/dev/null; then
    mv data/.lm_tmp/shape_predictor_68_face_landmarks.dat data/models/; rm -rf data/.lm_tmp
  else log "  (download failed - auto-landmarking will fall back to proportional guesses)"; fi
fi
if [ "$WANT_CREMA" = 1 ] && [ ! -d data/crema_d ]; then
  log "fetching CREMA-D audio (plain-blob mirror hallowshaw/Speech-Emotion-Recognition-with-MFCC, ~600 MB)"
  mkdir -p data; rm -rf data/.crema_tmp
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/hallowshaw/Speech-Emotion-Recognition-with-MFCC data/.crema_tmp
  git -C data/.crema_tmp sparse-checkout set dataset/cremad/AudioWAV
  mv data/.crema_tmp/dataset/cremad/AudioWAV data/crema_d; rm -rf data/.crema_tmp
fi
if [ "$WANT_TORCH" = 1 ] && [ ! -d third_party/libtorch/torch ]; then
  log "fetching LibTorch (torch 2.2.2 CPU wheel)"
  mkdir -p /tmp/fr_torch && pip download -q torch==2.2.2 --no-deps -d /tmp/fr_torch --index-url https://download.pytorch.org/whl/cpu
  mkdir -p third_party/libtorch && (cd third_party/libtorch && unzip -oq /tmp/fr_torch/torch-*.whl 'torch/*')
fi

# --- 5. configure + build + test --------------------------------------------------------------
TORCH_ARGS=()
[ -d third_party/libtorch/torch ] && TORCH_ARGS=(-DFR_WITH_TORCH=ON -DTORCH_ROOT="$ROOT/third_party/libtorch/torch")
log "configuring"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev "${TORCH_ARGS[@]}" 2>&1 | grep -E "^-- (PortAudio|Assimp|Torch|FBX|FR)" || true
log "building"
cmake --build build 2>&1 | grep -E "error|FAILED|warning: .*fr_" || true
[ -x build/fr_cli ] || { echo "build failed"; exit 1; }
if [ "$RUN_TESTS" = 1 ]; then log "testing"; ctest --test-dir build --output-on-failure 2>&1 | tail -3; fi
log "ready:  build/facial_rigging --model assets/models/ict_face/ict_face.obj --generate"
log "        tools/run_headless.sh --model assets/models/ict_face/ict_face.obj --render-frames 4"

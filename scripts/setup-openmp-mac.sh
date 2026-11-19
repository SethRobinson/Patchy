#!/usr/bin/env bash
# One-time (idempotent) build of the OpenMP runtime the G'MIC engine links on macOS
# (docs/gmic.md, "Threads"): LLVM's libomp from the pinned LLVM release source, built for
# Patchy's deployment target as a dylib, into .deps (gitignored). The app bundle carries it in
# Contents/Frameworks. Not a static library: a statically linked libomp never unregisters a
# thread that ends, and the next thread to enter a critical section crashes (the engine runs
# each filter on a thread of its own). Apple's compiler
# ships no OpenMP runtime, and Homebrew's libomp is built for the build host's macOS, newer
# than Patchy's minimum. Both downloads are checked against the SHA-256 pinned below (the
# digests GitHub publishes for the release assets); a file that fails is deleted, never used.
#
#   bash scripts/setup-openmp-mac.sh            # provisions <main checkout>/.deps
#   bash scripts/setup-openmp-mac.sh /some/deps  # explicit deps folder
# Rerunning is a fast no-op once the library is there.
set -euo pipefail

# Keep in sync with cmake/patchy_gmic.cmake (PATCHY_LIBOMP_VERSION) and the mac-release
# preset's CMAKE_OSX_DEPLOYMENT_TARGET.
LLVM_VERSION="21.1.8"
DEPLOYMENT_TARGET="12.0"
OPENMP_SHA256="856b023748b41ac7b2c83fd8e9f765ff48a4df2fe6777d2811ef7c7ed8f2f977"
CMAKE_SHA256="85735f20fd8c81ecb0a09abb0c267018475420e93b65050cc5b7634eab744de9"
RELEASE_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION"

[[ "$(uname -s)" == "Darwin" ]] || { echo "setup-openmp-mac: macOS only" >&2; exit 1; }

if [[ $# -ge 1 ]]; then
  DEPS_DIR="$1"
else
  COMMON_DIR="$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)"
  MAIN_CHECKOUT="$(dirname "$COMMON_DIR")"
  [[ -f "$MAIN_CHECKOUT/CMakeLists.txt" ]] || { echo "main checkout not found at $MAIN_CHECKOUT; pass a deps folder" >&2; exit 1; }
  DEPS_DIR="$MAIN_CHECKOUT/.deps"
fi
mkdir -p "$DEPS_DIR"
DEST="$DEPS_DIR/libomp-$LLVM_VERSION-macos$DEPLOYMENT_TARGET"
if [[ -f "$DEST/lib/libomp.dylib" && -f "$DEST/include/omp.h" && -s "$DEST/LICENSE.TXT" ]]; then
  echo "libomp $LLVM_VERSION for macOS $DEPLOYMENT_TARGET is in place: $DEST"
  exit 0
fi
# An earlier static build in the same folder is replaced.
if [[ -d "$DEST" ]]; then
  rm -rf "${DEPS_DIR:?}/libomp-$LLVM_VERSION-macos$DEPLOYMENT_TARGET"
fi
rm -rf "${DEPS_DIR:?}/libomp-$LLVM_VERSION-macos$DEPLOYMENT_TARGET.partial"

# The pip-installed cmake and ninja from scripts/remote/setup-mac.sh, when present.
export PATH="$HOME/.patchy-tools/bin:$PATH"
command -v cmake >/dev/null && command -v ninja >/dev/null || { echo "setup-openmp-mac: needs cmake and ninja on PATH" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/patchy-libomp.XXXXXX")"
trap 'rm -rf "${WORK:?}"' EXIT

fetch_verified() {
  local name="$1" expected="$2"
  echo "downloading $RELEASE_URL/$name"
  curl -fL --retry 3 -o "$WORK/$name" "$RELEASE_URL/$name"
  local actual
  actual="$(shasum -a 256 "$WORK/$name" | cut -d' ' -f1)"
  if [[ "$actual" != "$expected" ]]; then
    rm -f "$WORK/$name"
    echo "checksum mismatch for $name: expected $expected, got $actual" >&2
    exit 1
  fi
}
fetch_verified "openmp-$LLVM_VERSION.src.tar.xz" "$OPENMP_SHA256"
fetch_verified "cmake-$LLVM_VERSION.src.tar.xz" "$CMAKE_SHA256"

# A standalone libomp build finds LLVM's shared CMake modules in a sibling "cmake" folder.
tar -xJf "$WORK/openmp-$LLVM_VERSION.src.tar.xz" -C "$WORK"
tar -xJf "$WORK/cmake-$LLVM_VERSION.src.tar.xz" -C "$WORK"
mv "$WORK/openmp-$LLVM_VERSION.src" "$WORK/openmp"
mv "$WORK/cmake-$LLVM_VERSION.src" "$WORK/cmake"

cmake -S "$WORK/openmp" -B "$WORK/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
  -DCMAKE_INSTALL_PREFIX="$WORK/install" \
  -DLIBOMP_ENABLE_SHARED=ON \
  -DLIBOMP_OMPT_SUPPORT=OFF \
  -DOPENMP_ENABLE_LIBOMPTARGET=OFF \
  -DOPENMP_ENABLE_OMPT_TOOLS=OFF \
  -DLIBOMP_INSTALL_ALIASES=OFF
nice -n 10 cmake --build "$WORK/build"
cmake --install "$WORK/build"

[[ -f "$WORK/install/lib/libomp.dylib" && -f "$WORK/install/include/omp.h" ]] || { echo "setup-openmp-mac: the build produced no libomp.dylib/omp.h" >&2; exit 1; }
# The app finds it through its rpath (Contents/Frameworks); check the name it is linked by.
[[ "$(otool -D "$WORK/install/lib/libomp.dylib" | tail -n 1)" == "@rpath/libomp.dylib" ]] || {
  echo "setup-openmp-mac: unexpected install name: $(otool -D "$WORK/install/lib/libomp.dylib" | tail -n 1)" >&2
  exit 1
}
mkdir -p "$DEST.partial/lib" "$DEST.partial/include"
cp -L "$WORK/install/lib/libomp.dylib" "$DEST.partial/lib/libomp.dylib"
cp "$WORK/install/include/omp.h" "$DEST.partial/include/"
cp "$WORK/openmp/LICENSE.TXT" "$DEST.partial/LICENSE.TXT"
mv "$DEST.partial" "$DEST"
echo "libomp $LLVM_VERSION for macOS $DEPLOYMENT_TARGET installed: $DEST"

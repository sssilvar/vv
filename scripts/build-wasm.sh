#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
vtk_commit=23f0a095621e91bbdbeace8451e22b950c8e5f46
jobs=${VV_BUILD_JOBS:-4}
if [[ ! "$jobs" =~ ^[1-9][0-9]?$ ]]; then
  echo "VV_BUILD_JOBS must be between 1 and 99" >&2
  exit 1
fi
if ! command -v emcmake >/dev/null; then
  echo "Install Emscripten and activate its environment before building." >&2
  exit 1
fi
if [[ -z "${EMSDK_PYTHON:-}" ]]; then
  for candidate in python3.14 python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "$candidate" >/dev/null && "$candidate" -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then
      export EMSDK_PYTHON
      EMSDK_PYTHON=$(command -v "$candidate")
      break
    fi
  done
fi

vtk_dir=${VV_VTK_WASM_DIR:-"$root/.cache/vtk-wasm-build"}
if [[ -z "${VV_VTK_WASM_DIR:-}" ]]; then
  vtk_source="$root/.cache/vtk-wasm-src"
  if [[ ! -d "$vtk_source/.git" ]]; then
    git clone --depth 1 --branch v9.7.0 https://gitlab.kitware.com/vtk/vtk.git "$vtk_source"
  fi
  if [[ "$(git -C "$vtk_source" rev-parse HEAD)" != "$vtk_commit" ]]; then
    echo "Unexpected VTK revision in $vtk_source; expected $vtk_commit." >&2
    exit 1
  fi
  emcmake cmake -S "$vtk_source" -B "$vtk_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DVTK_BUILD_TESTING=OFF -DVTK_BUILD_EXAMPLES=OFF -DVTK_ENABLE_WRAPPING=OFF \
    -DVTK_GROUP_ENABLE_StandAlone=DONT_WANT -DVTK_GROUP_ENABLE_Rendering=DONT_WANT \
    -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES \
    -DVTK_MODULE_ENABLE_VTK_FiltersSources=YES \
    -DVTK_MODULE_ENABLE_VTK_CommonColor=YES \
    -DVTK_MODULE_ENABLE_VTK_FiltersCore=YES -DVTK_WEBASSEMBLY_THREADS=OFF
  cmake --build "$vtk_dir" --parallel "$jobs"
fi

emcmake cmake -S "$root" -B "$root/build/wasm" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DVV_VISUALIZATION_ONLY=ON -DVTK_DIR="$vtk_dir"
cmake --build "$root/build/wasm" --parallel "$jobs"
mkdir -p "$root/web/src/wasm"
cp "$root/build/wasm/src/visualization/vv.js" "$root/web/src/wasm/vv.js"
cp "$root/build/wasm/src/visualization/vv.wasm" "$root/web/src/wasm/vv.wasm"

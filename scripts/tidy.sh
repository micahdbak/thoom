#!/usr/bin/env bash
set -euo pipefail

CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

if [ ! -f build/compile_commands.json ]; then
  echo "build/compile_commands.json not found." >&2
  echo "Run cmake -S . -B build first." >&2
  exit 1
fi

status=0

git ls-files -z 'src/*.cc' 'tools/*.cc' |
  xargs -0 "${CLANG_TIDY}" -p build --quiet --fix \
    --extra-arg=-std=c++20 || status=1

# clang-tidy reports only on the main file, so headers need their own pass.
# Headers have no compile command, so supply C++ mode and include paths.
git ls-files -z 'include/*.h' 'src/*.h' 'tools/*.h' |
  xargs -0 "${CLANG_TIDY}" -p build --quiet --fix \
    --extra-arg=-xc++ \
    --extra-arg=-std=c++20 \
    --extra-arg=-I"${PWD}/include" \
    --extra-arg=-I"${PWD}/src" \
    --extra-arg=-I"${PWD}/tools/map_editor" \
    --extra-arg=-I"${PWD}/deps/SDL/include" || status=1

exit "${status}"

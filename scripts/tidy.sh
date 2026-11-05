#!/usr/bin/env bash
set -euo pipefail

CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

if [ ! -f build/compile_commands.json ]; then
  echo "build/compile_commands.json not found." >&2
  echo "Run cmake -S . -B build first." >&2
  exit 1
fi

status=0

git ls-files -z 'src/*.cc' |
  xargs -0 "${CLANG_TIDY}" -p build \
    --checks='-*,misc-include-cleaner' \
    --warnings-as-errors='misc-include-cleaner' || status=1

# clang-tidy reports only on the main file, so headers need their own pass.
# Headers have no compile command, so supply C++ mode and include paths.
git ls-files -z 'include/*.h' 'src/*.h' |
  xargs -0 "${CLANG_TIDY}" -p build \
    --checks='-*,misc-include-cleaner' \
    --warnings-as-errors='misc-include-cleaner' \
    --extra-arg=-xc++ \
    --extra-arg=-std=c++20 \
    --extra-arg=-I"${PWD}/include" \
    --extra-arg=-I"${PWD}/src" \
    --extra-arg=-I"${PWD}/deps/SDL/include" || status=1

if [ "${status}" -ne 0 ]; then
  echo "IWYU violations found." >&2
fi

exit "${status}"

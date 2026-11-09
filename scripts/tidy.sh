#!/usr/bin/env bash
set -euo pipefail

CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

if [ ! -f build/compile_commands.json ]; then
  echo "build/compile_commands.json not found." >&2
  echo "Run cmake -S . -B build first." >&2
  exit 1
fi

status=0

# tidy <clang-tidy args...> -- <git pathspec...>
#
# The argument split keeps empty file lists from running clang-tidy with no
# input, which it treats as an error.
tidy() {
  local args=()
  while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do
    args+=("$1")
    shift
  done
  shift

  local files=()
  mapfile -d '' files < <(git ls-files -z "$@")
  if [ "${#files[@]}" -eq 0 ]; then
    return 0
  fi

  "${CLANG_TIDY}" -p build --quiet --fix "${args[@]}" "${files[@]}" ||
    status=1
}

# The compile database carries the include paths but no standard, because the
# compiler's default is already C++20. Force it for clang-tidy, whose default
# is older.
tidy --extra-arg=-std=c++20 -- \
  'src/*.cc' 'test/*.cc' 'tools/*.cc' 'src/*.c' 'tools/*.c'

# clang-tidy reports only on the main file, so headers need their own pass.
# Headers have no compile command, so supply the language and include paths.
# The public headers are C and every other header is C++, so the language is
# chosen by directory.
CXX_HEADER_ARGS=(
  --extra-arg=-xc++
  --extra-arg=-std=c++20
  --extra-arg=-I"${PWD}/include"
  --extra-arg=-I"${PWD}/src"
  --extra-arg=-I"${PWD}/tools/map_editor"
  --extra-arg=-I"${PWD}/deps/SDL/include"
)
tidy "${CXX_HEADER_ARGS[@]}" -- 'src/*.h' 'test/*.h' 'tools/*.h'

C_HEADER_ARGS=(
  --extra-arg=-xc
  --extra-arg=-std=c17
  --extra-arg=-I"${PWD}/include"
  --extra-arg=-I"${PWD}/src"
)
tidy "${C_HEADER_ARGS[@]}" -- 'include/*.h'

exit "${status}"

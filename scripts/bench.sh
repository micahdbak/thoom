#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [ -z "${THOOM_SIGNALLING_URL:-}" ]; then
  export THOOM_SIGNALLING_URL=ws://localhost:8080/sc/

  if ! (exec 3<>/dev/tcp/localhost/8080) 2>/dev/null; then
    echo "bench.sh: no signalling server on localhost:8080" >&2
    echo "start one with: (cd ../server && go run .)" >&2
    exit 1
  fi
fi

./build/bin/switch_bench "$@"

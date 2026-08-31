#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
HOST="${1:-72.62.76.118}"
GPU="${2:-0}"
NAME="$(hostname 2>/dev/null || echo linux)"

if [[ ! -x ./VanitySearchKang ]]; then
  echo "Building Linux worker (needs nvcc + g++)..."
  make -j"$(nproc)"
fi

echo "Worker: $(pwd)/VanitySearchKang"
echo "Pool:   ${HOST}:17403   gpu ${GPU}"
exec ./VanitySearchKang -pool "${HOST}:17403" -gpuId "${GPU}" -worker "${NAME}"

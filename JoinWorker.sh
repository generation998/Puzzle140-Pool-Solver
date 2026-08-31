#!/usr/bin/env bash
# Join the puzzle-140 pool on every visible NVIDIA GPU.
#   ./JoinWorker.sh                  # all GPUs on default pool
#   ./JoinWorker.sh 72.62.76.118    # all GPUs
#   ./JoinWorker.sh HOST 2          # only GPU 2
#   GPUS=0,3 ./JoinWorker.sh        # only those indices
set -uo pipefail
cd "$(dirname "$0")"
HOST="${1:-72.62.76.118}"
PIN="${2:-}"
NAME="$(hostname 2>/dev/null || echo linux)"

if [[ ! -x ./VanitySearchKang ]]; then
  echo "Building Linux worker (needs nvcc + g++)..."
  make -j"$(nproc)"
fi

list_gpus() {
  if [[ -n "${GPUS:-}" ]]; then
    echo "${GPUS}" | tr ',' '\n' | tr -d ' ' | grep -E '^[0-9]+$'
    return
  fi
  if [[ -n "${PIN}" ]]; then
    echo "${PIN}"
    return
  fi
  if [[ -n "${CUDA_VISIBLE_DEVICES:-}" ]]; then
    local i=0
    local part
    IFS=',' read -ra parts <<< "${CUDA_VISIBLE_DEVICES}"
    for part in "${parts[@]}"; do
      part="${part// /}"
      [[ -z "${part}" ]] && continue
      echo "${i}"
      i=$((i + 1))
    done
    return
  fi
  if command -v nvidia-smi >/dev/null 2>&1; then
    local ids
    ids="$(nvidia-smi --query-gpu=index --format=csv,noheader,nounits 2>/dev/null \
      | tr -d ' ' | grep -E '^[0-9]+$' || true)"
    if [[ -n "${ids}" ]]; then
      echo "${ids}"
      return
    fi
  fi
  echo 0
}

mapfile -t GPU_IDS < <(list_gpus)
if [[ ${#GPU_IDS[@]} -eq 0 ]]; then
  GPU_IDS=(0)
fi

echo "Worker: $(pwd)/VanitySearchKang"
echo "Pool:   ${HOST}:17403"
echo "GPUs:   ${GPU_IDS[*]}  (${#GPU_IDS[@]} process(es))"

PIDS=()
stop_all() {
  local p
  for p in "${PIDS[@]:-}"; do
    kill "$p" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap stop_all INT TERM

for g in "${GPU_IDS[@]}"; do
  wname="${NAME}-gpu${g}"
  echo "Starting ${wname}"
  ./VanitySearchKang -pool "${HOST}:17403" -gpuId "${g}" -worker "${wname}" &
  PIDS+=("$!")
done

fail=0
for p in "${PIDS[@]}"; do
  if ! wait "$p"; then
    fail=1
  fi
done
exit "${fail}"

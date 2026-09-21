#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

GEM5_BINARY="${GEM5_BINARY:-build/X86/gem5.opt}"
DISK_IMAGE="${DISK_IMAGE:-/home/esj/gem5_resource/ligra2.img}"
KERNEL_PATH="${KERNEL_PATH:-ext/linux/linux/vmlinux}"
OUTPUT_DIR="${1:-/tmp/flex_cxl_booting_$(date +%y%m%d_%H%M%S)}"
BOOT_SCRIPT="configs/boot/booting.rcS"
HOST_SCRIPTS="${BOOT_SCRIPT},${BOOT_SCRIPT},${BOOT_SCRIPT},${BOOT_SCRIPT}"

for required_file in "$GEM5_BINARY" "$DISK_IMAGE" "$KERNEL_PATH" "$BOOT_SCRIPT"; do
    if [[ ! -e "$required_file" ]]; then
        echo "error: missing required file: $required_file" >&2
        exit 1
    fi
done

mkdir -p "$OUTPUT_DIR"

command=(
    "$GEM5_BINARY"
    -d "$OUTPUT_DIR"
    configs/fs.py
    --disk "$DISK_IMAGE"
    --kernel "$KERNEL_PATH"
    --cpu-type=X86O3CPU
    --cxl-mem-size 8GB
    --mem-size 3GB
    --ruby
    --cxl-mode
    --num-hosts 4
    --switch-on-m5-exit
    --switch-start-cpu-type=X86AtomicSimpleCPU
    --host-scripts "$HOST_SCRIPTS"
    --defer-non-trigger-host-scripts
    --resetstats-on-m5-exit-switch
    --host-cxl-ratios 1,1,1,1
    --num-dirs 2
    --routing-mode-type round-robin
    --priority-limit 8
    --priority-host-ratios 5,5,5,5
    --CXL-clock 500MHz
)

printf 'Output directory: %s\n' "$OUTPUT_DIR"
printf 'Command:'
printf ' %q' "${command[@]}"
printf '\n'

set +e
"${command[@]}" 2>&1 | tee "$OUTPUT_DIR/tmux.log"
status=${PIPESTATUS[0]}
set -e

printf 'gem5 exited with status %d\n' "$status"
exit "$status"

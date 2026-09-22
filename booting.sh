#!/bin/bash

set -euo pipefail

REPO_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "${REPO_DIR}"

GEM5_BINARY=${GEM5_BINARY:-build/X86/gem5.opt}
CONFIG_SCRIPT=configs/fs.py
DISK_IMAGE=${DISK_IMAGE:-/home/esj/gem5_resource/ligra2.img}
KERNEL_PATH=${KERNEL_PATH:-ext/linux/linux/vmlinux}
RUN_SCRIPT=configs/boot/cxl_boot_test.rcS

OUTPUT_DIR=""
PCIE_LANE=8
CXL_MEM_SIZE=${CXL_MEM_SIZE:-8GB}
MEM_SIZE=${MEM_SIZE:-3GB}

usage() {
    cat <<EOF
Usage: $0 [options]

  -d, --output-dir DIR     gem5 output directory
  --pcie-lane N            CXL link width: 1, 2, 4, 8, or 16 (default: 8)
  --disk-image PATH        guest disk image
  --kernel PATH            guest Linux kernel
  --cxl-mem-size SIZE      CXL memory size (default: ${CXL_MEM_SIZE})
  --mem-size SIZE          host DRAM size (default: ${MEM_SIZE})
  -h, --help               show this help

The physical CXL link clock is derived from the lane width:
x8 = 500MHz and x16 = 1GHz. CXL controllers always run at 1GHz.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--output-dir)
            OUTPUT_DIR=$2
            shift 2
            ;;
        --pcie-lane)
            PCIE_LANE=$2
            shift 2
            ;;
        --disk-image)
            DISK_IMAGE=$2
            shift 2
            ;;
        --kernel)
            KERNEL_PATH=$2
            shift 2
            ;;
        --cxl-mem-size)
            CXL_MEM_SIZE=$2
            shift 2
            ;;
        --mem-size)
            MEM_SIZE=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "${PCIE_LANE}" in
    1|2|4|8|16) ;;
    *)
        echo "--pcie-lane must be one of: 1, 2, 4, 8, 16" >&2
        exit 2
        ;;
esac

if [[ -z "${OUTPUT_DIR}" ]]; then
    OUTPUT_DIR="cxl-boot-x${PCIE_LANE}"
fi

for required_file in "${GEM5_BINARY}" "${DISK_IMAGE}" \
    "${KERNEL_PATH}" "${RUN_SCRIPT}"; do
    if [[ ! -e "${required_file}" ]]; then
        echo "Missing required file: ${required_file}" >&2
        exit 1
    fi
done

echo "Running CXL boot test with build/X86/gem5.opt"
echo "PCIe lanes: x${PCIE_LANE}"
echo "Output: ${OUTPUT_DIR}"

exec "${GEM5_BINARY}" \
    -d "${OUTPUT_DIR}" \
    "${CONFIG_SCRIPT}" \
    --disk "${DISK_IMAGE}" \
    --kernel "${KERNEL_PATH}" \
    --cpu-type=X86O3CPU \
    --switch-on-m5-exit \
    --switch-start-cpu-type=X86AtomicSimpleCPU \
    --resetstats-on-m5-exit-switch \
    --script "${RUN_SCRIPT}" \
    --cxl-mem-size "${CXL_MEM_SIZE}" \
    --mem-size "${MEM_SIZE}" \
    --pcie-lane "${PCIE_LANE}" \
    --ruby \
    --cxl-mode \
    --num-cpus 1 \
    --num-dirs 2

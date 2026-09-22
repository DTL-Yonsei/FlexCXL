#!/bin/bash

set -euo pipefail

REPO_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "${REPO_DIR}"

GEM5_BINARY=${GEM5_BINARY:-build/X86/gem5.opt}
CONFIG_SCRIPT=configs/fs.py
DISK_IMAGE=${DISK_IMAGE:-/home/esj/gem5_resource/ligra2.img}
KERNEL_PATH=${KERNEL_PATH:-ext/linux/linux/vmlinux}
RUN_SCRIPT=configs/boot/stream_cxl_kernel.rcS
 KERNEL_COMMAND_LINE=${KERNEL_COMMAND_LINE:-"earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/sda1 no_timer_check idle=nomwait"}
#KERNEL_COMMAND_LINE=${KERNEL_COMMAND_LINE:-"earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/sda1  nolapic_timer "}

OUTPUT_DIR=${OUTPUT_DIR:-/tmp/flexcxl_stream_12_kvm_replay_fix}
NUM_CPUS=48
PCIE_LANE=8
CXL_MEM_SIZE=${CXL_MEM_SIZE:-8GB}
MEM_SIZE=${MEM_SIZE:-3GB}
DEBUG_FLAGS=${DEBUG_FLAGS:-}
DEBUG_FILE=${DEBUG_FILE:-cxl_ctrl_debug.log}

usage() {
    cat <<EOF
Usage: $0 [options]

  -d, --output-dir DIR     gem5 output directory
  --num-cpus N             KVM/O3 CPU count (default: ${NUM_CPUS})
  --pcie-lane N            CXL link width: 1, 2, 4, 8, or 16 (default: 8)
  --disk-image PATH        guest disk image
  --kernel PATH            guest Linux kernel
  --command-line STRING    guest kernel command line
  --cxl-mem-size SIZE      CXL memory size (default: ${CXL_MEM_SIZE})
  --mem-size SIZE          host DRAM size (default: ${MEM_SIZE})
  --debug-cxl              enable the verbose CXL_ctrl debug log
  --debug-flags FLAGS      enable the specified gem5 debug flags
  --debug-file FILE        debug log inside output dir (default: ${DEBUG_FILE})
  --no-debug               disable gem5 debug logging
  -h, --help               show this help

The guest boots with X86KvmCPU and switches all CPUs to X86O3CPU at
the first m5 exit in configs/boot/stream_cxl_kernel.rcS. The script
uses build/X86/gem5.opt and invokes it through sudo when needed.

The CXL link clock is derived from the lane width:
x8 = 500MHz and x16 = 1GHz. CXL controllers always run at 1GHz.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--output-dir)
            OUTPUT_DIR=$2
            shift 2
            ;;
        --num-cpus)
            NUM_CPUS=$2
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
        --command-line)
            KERNEL_COMMAND_LINE=$2
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
        --debug-flags)
            DEBUG_FLAGS=$2
            shift 2
            ;;
        --debug-cxl)
            DEBUG_FLAGS=CXL_ctrl
            shift
            ;;
        --debug-file)
            DEBUG_FILE=$2
            shift 2
            ;;
        --no-debug)
            DEBUG_FLAGS=
            shift
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

if ! [[ "${NUM_CPUS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "--num-cpus must be a positive integer" >&2
    exit 2
fi

for required_file in "${GEM5_BINARY}" "${DISK_IMAGE}" \
    "${KERNEL_PATH}" "${RUN_SCRIPT}"; do
    if [[ ! -e "${required_file}" ]]; then
        echo "Missing required file: ${required_file}" >&2
        exit 1
    fi
done

if [[ ! -e /dev/kvm ]]; then
    echo "ERROR: /dev/kvm is not available on this host." >&2
    echo "Load/enable the host KVM module before running this script." >&2
    exit 1
fi

run_prefix=()
if [[ ${EUID} -ne 0 ]]; then
    run_prefix=(sudo)
fi

echo "Running CXL STREAM with KVM boot and O3 measurement"
echo "CPUs: ${NUM_CPUS}"
echo "PCIe lanes: x${PCIE_LANE}"
echo "Output: ${OUTPUT_DIR}"

debug_args=()
if [[ -n "${DEBUG_FLAGS}" ]]; then
    debug_args+=("--debug-flags=${DEBUG_FLAGS}")
    debug_args+=("--debug-file=${DEBUG_FILE}")
    echo "Debug flags: ${DEBUG_FLAGS}"
    echo "Debug log: ${OUTPUT_DIR}/${DEBUG_FILE}"
else
    echo "Debug flags: disabled"
fi

exec "${run_prefix[@]}" "${GEM5_BINARY}" \
    "${debug_args[@]}" \
    -d "${OUTPUT_DIR}" \
    "${CONFIG_SCRIPT}" \
    --disk "${DISK_IMAGE}" \
    --kernel "${KERNEL_PATH}" \
    --command-line "${KERNEL_COMMAND_LINE}" \
    --cpu-type=X86O3CPU \
    --switch-on-m5-exit \
    --switch-start-cpu-type=X86KvmCPU \
    --resetstats-on-m5-exit-switch \
    --script "${RUN_SCRIPT}" \
    --cxl-mem-size "${CXL_MEM_SIZE}" \
    --mem-size "${MEM_SIZE}" \
    --pcie-lane "${PCIE_LANE}" \
    --ruby \
    --cxl-mode \
    --num-cpus "${NUM_CPUS}" \
    --num-dirs 2 \
    --param 'system.CXL_ctrl.max_queue_size=48' \
    --param 'system.CXL_ctrl2.max_queue_size=48' \
    --param 'system.CXL_ctrl.validation_outstanding_window_capacity=-1' \
    --param 'system.CXL_ctrl2.validation_outstanding_window_capacity=-1' \
    --param 'system.switch.req_size=48' \
    --param 'system.switch.resp_size=48'

#!/bin/bash

# Define the gem5 binary path
GEM5_BINARY="build/X86/gem5.opt"

# Define the configuration script
CONFIG_SCRIPT="configs/fs.py"

# Define the disk image path
DISK_IMAGE="/home/esj/gem5_resource/ligra2.img"

# Define the kernel path
KERNEL_PATH="ext/linux/linux/vmlinux"

# Define CPU type
CPU_TYPE="X86O3CPU"
SWITCH_START_CPU_TYPE="X86AtomicSimpleCPU"

# Define memory sizes
CXL_MEM_SIZE="8GB"
MEM_SIZE="3GB"

# Define the guest runscript. The first m5 exit switches to CPU_TYPE.
RUN_SCRIPT="configs/boot/my_experiment.rcS"

# Added: optional CXL mixed read/write compose toggle (default OFF).
ENABLE_CXL_MIXED_RW_COMPOSE="false"
ENABLE_CXL_MEM_AS_SYSTEM_RAM="false"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--output-dir)
            OUTPUT_DIR="$2"
            shift # past argument
            shift # past value
            ;;
        --enable-cxl-mixed-rw-compose)
            ENABLE_CXL_MIXED_RW_COMPOSE="true"
            shift # past argument
            ;;
        --disable-cxl-mixed-rw-compose)
            ENABLE_CXL_MIXED_RW_COMPOSE="false"
            shift # past argument
            ;;
        --cxl-mem-as-system-ram|--enable-cxl-mem-as-system-ram)
            ENABLE_CXL_MEM_AS_SYSTEM_RAM="true"
            shift # past argument
            ;;
        --disable-cxl-mem-as-system-ram)
            ENABLE_CXL_MEM_AS_SYSTEM_RAM="false"
            shift # past argument
            ;;
        *)
            echo "Unknown option $1"
            exit 1
            ;;
    esac
done

# Set default output directory if not provided
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="cxl"
fi

# Added: build optional fs.py arguments.
EXTRA_FS_ARGS=()
if [ "$ENABLE_CXL_MIXED_RW_COMPOSE" = "true" ]; then
    EXTRA_FS_ARGS+=(--enable-cxl-mixed-rw-compose)
fi
if [ "$ENABLE_CXL_MEM_AS_SYSTEM_RAM" = "true" ]; then
    EXTRA_FS_ARGS+=(--cxl-mem-as-system-ram)
fi

# Run gem5 with specified parameters
sudo $GEM5_BINARY \
     -d $OUTPUT_DIR \
     $CONFIG_SCRIPT \
    --disk $DISK_IMAGE \
    --kernel $KERNEL_PATH \
    --cpu-type=$CPU_TYPE \
    --switch-on-m5-exit \
    --switch-start-cpu-type=$SWITCH_START_CPU_TYPE \
    --resetstats-on-m5-exit-switch \
    --script $RUN_SCRIPT \
    --cxl-mem-size $CXL_MEM_SIZE \
    --mem-size $MEM_SIZE \
    --ruby \
    --cxl-mode \
    --num-cpus 1 \
    --num-dirs 2 \
    "${EXTRA_FS_ARGS[@]}"

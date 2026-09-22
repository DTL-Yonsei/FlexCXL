#!/bin/bash

# Define the gem5 binary path
#GEM5_BINARY="build/X86/gem5.opt"
GEM5_BINARY="build/X86/gem5.fast"

# Define the debug flags
# DEBUG_FLAGS="CXLDRAMsim3,CXL_CRC,CXL_CRC_check,PciBridge2,CXL_ctrl,CXL_Decoder,CXL_Deframer,CXL_Encoder,CXL_Framer,,CXL_Unpacking,CXL_Decoder"
#DEBUG_FLAGS="CXLDRAMsim3,CXL_CRC_check,PciBridge2,CXL_ctrl"
#DEBUG_FLAGS="CXLSequencer,RubyNetwork,RubyGenerated,RubySequencer,RubySlicc,RubyQueue,RubyPort,PciBridge2,CXL_ctrl,CXLDRAMsim3,NoncoherentXBar"
#DEBUG_FLAGS="AddrRanges,CXLSequencer,PciBridge2,CXL_ctrl,CXLDRAMsim3"
#DEBUG_FLAGS="PciBridge2,CXLDRAMsim3,CXLSequencer,NoncoherentXBar,MemCtrl"
#DEBUG_FLAGS="CXLDRAMsim3,PciBridge2,CXLSequencer,CXL_ctrl,NoncoherentXBar,CXL_Encoder,CXL_Unpacking,RubyNetwork,RubyGenerated"
DEBUG_FLAGS="CXLDRAMsim3,PciBridge2,CXL_ctrl,CXLHomeAgent"
# Define the configuration script
CONFIG_SCRIPT="configs/fs.py"

# Define the disk image path
DISK_IMAGE="/home/esj/gem5_resource/ligra2.img"
# Define the kernel path
KERNEL_PATH="ext/linux/linux/vmlinux"

# Define CPU type
CPU_TYPE="X86O3CPU"
# SWITCH_START_CPU_TYPE="X86KvmCPU"
SWITCH_START_CPU_TYPE="X86AtomicSimpleCPU"

# Define memory sizes
CXL_MEM_SIZE="2GB"
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
$GEM5_BINARY \
     -d $OUTPUT_DIR \
     $CONFIG_SCRIPT \
    --disk $DISK_IMAGE \
    --kernel $KERNEL_PATH \
    --cpu-type=$CPU_TYPE \
    --switch-on-m5-exit \
    --switch-start-cpu-type=$SWITCH_START_CPU_TYPE \
    --resetstats-on-m5-exit-switch \
    --cxl-mem-size $CXL_MEM_SIZE \
    --mem-size $MEM_SIZE \
    --script $RUN_SCRIPT \
    --ruby \
    --cxl-mode \
    --num-cpus 1 \
    --num-dirs 2 \
    "${EXTRA_FS_ARGS[@]}"

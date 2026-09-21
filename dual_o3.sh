#!/bin/bash

# Define the gem5 binary path
GEM5_BINARY="build/X86/gem5.opt"

# Define the debug flags
#DEBUG_FLAGS="CXLDRAMsim3,CXL_CRC,CXL_CRC_check,PciBridge2,CXL_ctrl,CXL_Decoder,CXL_Deframer,CXL_Encoder,CXL_Framer,CXL_Packing,CXL_Unpacking,CXL_Decoder"
#DEBUG_FLAGS="CXLDRAMsim3,CXL_CRC_check,PciBridge2,CXL_ctrl"
#DEBUG_FLAGS="ProtocolTrace,CXLSequencer,RubyNetwork,RubyGenerated,RubySequencer,RubySlicc,RubyPort,RubyQueue,PciBridge2,CXL_ctrl,CXLDRAMsim3,NoncoherentXBar"
#DEBUG_FLAGS="CXLSequencer,RubyGenerated,RubySequencer,RubyPort,RubyQueue,PciBridge2,CXL_ctrl,CXLDRAMsim3,NoncoherentXBar"
#DEBUG_FLAGS="ProtocolTrace,RubyGenerated,RubyNetwork,CXLSequencer,PciBridge2,CXL_ctrl,CXLDRAMsim3"
#DEBUG_FLAGS="AddrRanges,CXLSequencer,PciBridge2,CXL_ctrl,CXLDRAMsim3"
DEBUG_FLAGS="PciBridge2"

# Define the configuration script
CONFIG_SCRIPT="configs/fs.py"

# Define the disk image path
DISK_IMAGE="../gem5_resource/ligra2.img"

# Define the kernel path
KERNEL_PATH="../gem5_resource/linux/vmlinux"

# Define CPU type
CPU_TYPE="X86O3CPU"

# Define memory sizes
CXL_MEM_SIZE="2GB"
MEM_SIZE="3GB"

HOST1_CXL_RATIO="5"

# Define fast-forward ticks
FAST_FORWARD_TICKS="50000000000"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--output-dir)
            OUTPUT_DIR="$2"
            shift # past argument
            shift # past value
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

# Run gem5 with specified parameters
sudo $GEM5_BINARY \
    -d $OUTPUT_DIR \
    $CONFIG_SCRIPT \
    --disk $DISK_IMAGE \
    --kernel $KERNEL_PATH \
    --cpu-type=$CPU_TYPE \
    --cxl-mem-size $CXL_MEM_SIZE \
    --mem-size $MEM_SIZE \
    --ruby \
    --cxl-mode \
    --dual \
    --fast-forward $FAST_FORWARD_TICKS \
    --host1-cxl-ratio $HOST1_CXL_RATIO \
    --num-dirs 2 \
    --routing-mode-type priority \
    --priority-limit 8
    # --debug-flags $DEBUG_FLAGS \

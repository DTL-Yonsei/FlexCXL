#!/bin/bash

# Define the gem5 binary path
GEM5_BINARY="build/X86/gem5.fast"

# Define the debug flags
#DEBUG_FLAGS="CXLDRAMsim3,CXL_CRC,CXL_CRC_check,PciBridge2,CXL_ctrl,CXL_Decoder,CXL_Deframer,CXL_Encoder,CXL_Framer,CXL_Packing,CXL_Unpacking,CXL_Decoder"
#DEBUG_FLAGS="CXLDRAMsim3,CXL_CRC_check,PciBridge2,CXL_ctrl"
#DEBUG_FLAGS="ProtocolTrace,CXLSequencer,RubyNetwork,RubyGenerated,RubySequencer,RubySlicc,RubyPort,RubyQueue,PciBridge2,CXL_ctrl,CXLDRAMsim3,NoncoherentXBar"
#DEBUG_FLAGS="CXLSequencer,RubyGenerated,RubySequencer,RubyPort,RubyQueue,PciBridge2,CXL_ctrl,CXLDRAMsim3,NoncoherentXBar"
#DEBUG_FLAGS="ProtocolTrace,RubyGenerated,RubyNetwork,CXLSequencer,PciBridge2,CXL_ctrl,CXLDRAMsim3"
#DEBUG_FLAGS="AddrRanges,CXLSequencer,PciBridge2,CXL_ctrl,CXLDRAMsim3"
DEBUG_FLAGS="CXL_ctrl,AddrRanges,CXLSequencer,PciBridge2,CXLDRAMsim3"

# Define the configuration script
CONFIG_SCRIPT="configs/fs.py"

# Define the disk image path
DISK_IMAGE="/home/esj/gem5_resource/ligra2.img"

# Define the kernel path
KERNEL_PATH="/home/esj/gem5_resource/linux/vmlinux"

# Define CPU type
CPU_TYPE="X86O3CPU"

# Define memory sizes
CXL_MEM_SIZE="2GB"
MEM_SIZE="3GB"

HOST1_CXL_RATIO="4,2,2,2"
NUM_HOSTS="4"

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
        -n|--num-hosts)
            NUM_HOSTS="$2"
            shift # past argument
            shift # past value
            ;;
        -r|--host-cxl-ratios)
            HOST1_CXL_RATIO="$2"
            shift # past argument
            shift # past value
            ;;
        *)
            echo "Unknown option $1"
            exit 1
            ;;
    esac
done

if ! [[ "$NUM_HOSTS" =~ ^[0-9]+$ ]] || [ "$NUM_HOSTS" -lt 1 ]; then
    echo "Invalid --num-hosts value: $NUM_HOSTS (must be an integer >= 1)"
    exit 1
fi

# Set default output directory if not provided
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="cxl"
fi

HOST_CXL_RATIO_ARGS=""
HOST1_CXL_RATIO_ARGS=""
if [[ "$HOST1_CXL_RATIO" == *,* ]]; then
    HOST_CXL_RATIO_ARGS="--host-cxl-ratios $HOST1_CXL_RATIO"
else
    HOST1_CXL_RATIO_ARGS="--host1-cxl-ratio $HOST1_CXL_RATIO"
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
    --num-hosts $NUM_HOSTS \
    --fast-forward $FAST_FORWARD_TICKS \
    $HOST1_CXL_RATIO_ARGS \
    $HOST_CXL_RATIO_ARGS \
    --num-dirs 2 \
    --routing-mode-type round-robin \
    --priority-limit 8 \
    --CXL-clock 250MHz
    #--num-cpus 1 \
    #--debug-flags $DEBUG_FLAGS \

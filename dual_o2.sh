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
DEBUG_FLAGS="CXL_ctrl,PciBridge2,CXLDRAMsim3"

# Define the configuration script
CONFIG_SCRIPT="configs/fs.py"

# Define the disk image path
DISK_IMAGE="/home/esj/gem5_resource/ligra2.img"

# Define the kernel path
KERNEL_PATH="/home/esj/gem5_resource/linux/vmlinux"

# Define CPU type
CPU_TYPE="X86O3CPU"
SWITCH_START_CPU_TYPE="X86AtomicSimpleCPU"
RESETSTATS_ON_SWITCH=1

# Define memory sizes
CXL_MEM_SIZE="2GB"
MEM_SIZE="3GB"

HOST1_CXL_RATIO="4,2,2,2"
NUM_HOSTS="4"
HOST_SCRIPTS=""

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
        --num-hosts=*)
            NUM_HOSTS="${1#*=}"
            shift # past argument
            ;;
        -r|--host-cxl-ratios)
            HOST1_CXL_RATIO="$2"
            shift # past argument
            shift # past value
            ;;
        --host-cxl-ratios=*)
            HOST1_CXL_RATIO="${1#*=}"
            shift # past argument
            ;;
        -c|--cpu-type)
            CPU_TYPE="$2"
            shift # past argument
            shift # past value
            ;;
        --cpu-type=*)
            CPU_TYPE="${1#*=}"
            shift # past argument
            ;;
        -s|--host-scripts)
            HOST_SCRIPTS="$2"
            shift # past argument
            shift # past value
            ;;
        --host-scripts=*)
            HOST_SCRIPTS="${1#*=}"
            shift # past argument
            ;;
        --switch-start-cpu-type)
            SWITCH_START_CPU_TYPE="$2"
            shift # past argument
            shift # past value
            ;;
        --switch-start-cpu-type=*)
            SWITCH_START_CPU_TYPE="${1#*=}"
            shift # past argument
            ;;
        --no-resetstats-on-switch)
            RESETSTATS_ON_SWITCH=0
            shift # past argument
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

if [ -z "$HOST_SCRIPTS" ]; then
    echo "Missing --host-scripts."
    echo "Example: --host-scripts host0.rcS,host1.rcS,host2.rcS,host3.rcS"
    exit 1
fi

HOST_CXL_RATIO_ARGS=""
HOST1_CXL_RATIO_ARGS=""
if [[ "$HOST1_CXL_RATIO" == *,* ]]; then
    HOST_CXL_RATIO_ARGS="--host-cxl-ratios $HOST1_CXL_RATIO"
else
    HOST1_CXL_RATIO_ARGS="--host1-cxl-ratio $HOST1_CXL_RATIO"
fi

SWITCH_ARGS=(
    "--switch-on-m5-exit"
    "--switch-start-cpu-type=$SWITCH_START_CPU_TYPE"
    "--host-scripts"
    "$HOST_SCRIPTS"
)

if [ "$RESETSTATS_ON_SWITCH" -eq 1 ]; then
    SWITCH_ARGS+=("--resetstats-on-m5-exit-switch")
fi

# Run gem5 with specified parameters
sudo $GEM5_BINARY \
    -d $OUTPUT_DIR \
    --debug-flags $DEBUG_FLAGS \
    $CONFIG_SCRIPT \
    --disk $DISK_IMAGE \
    --kernel $KERNEL_PATH \
    --cpu-type=$CPU_TYPE \
    --cxl-mem-size $CXL_MEM_SIZE \
    --mem-size $MEM_SIZE \
    --ruby \
    --cxl-mode \
    --num-hosts $NUM_HOSTS \
    "${SWITCH_ARGS[@]}" \
    $HOST1_CXL_RATIO_ARGS \
    $HOST_CXL_RATIO_ARGS \
    --num-dirs 2 \
    --routing-mode-type round-robin \
    --priority-limit 8 \
    --CXL-clock 250MHz \
    #--num-cpus 1 \
    #

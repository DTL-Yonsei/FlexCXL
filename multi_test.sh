#!/usr/bin/env bash

set -euo pipefail

GEM5_BINARY="build/X86/gem5.opt"
CONFIG_SCRIPT="configs/fs.py"
DISK_IMAGE="/home/esj/gem5_resource/ligra2.img"
KERNEL_PATH="ext/linux/linux/vmlinux"

CPU_TYPE="X86O3CPU"
SWITCH_START_CPU_TYPE="X86AtomicSimpleCPU"
# SWITCH_START_CPU_TYPE="X86KvmCPU"
CPU_SWITCH=1
RESETSTATS_ON_SWITCH=1
DEFER_NON_TRIGGER_HOST_SCRIPTS=1

CXL_MEM_SIZE="8GB"
CXL_ERROR_RATE=""
MEM_SIZE="3GB"
CXL_CLOCK="250MHz"
NUM_HOSTS=4
NUM_DIRS=2
PRIORITY_LIMIT=8

# Current configs/fs.py normalizes these weights to an integer sum of 10.
# For four hosts, 1,1,1,1 means "near-equal"; check gem5's resolved windows.
HOST_CXL_RATIOS="1,1,1,1"

# DEBUG_FLAGS="CXL_ctrl,PciBridge2,CXLDRAMsim3"
DEBUG_FLAGS=""
USE_SUDO=1
DRY_RUN=0
SKIP_EXISTING=0
KEEP_GOING=0
INTERRUPTED=0

OUT_ROOT="multi_results"
RUN_SET="balanced"
WORKLOADS_CSV="a"
# POLICIES_CSV="rr,wrr-victim,wrr-aggressor"
POLICIES_CSV="rr"
POLICIES_SET=0
CUSTOM_PRIORITY_HOST_RATIOS=""

DEFAULT_HOST_SCRIPTS="host0.rcS,host1.rcS,host2.rcS,host3.rcS"
HOST_SCRIPTS_OVERRIDE=""

YCSB_A_SCRIPT="configs/boot/workloada_th4.rcS"
YCSB_B_SCRIPT="configs/boot/workloadb_th4.rcS"
YCSB_C_SCRIPT="configs/boot/workloadc_th4.rcS"
STREAM_SCRIPT="configs/boot/stream.rcS"

BALANCED_HOST_SCRIPTS_A=""
BALANCED_HOST_SCRIPTS_B=""
BALANCED_HOST_SCRIPTS_C=""
AGGRESSOR_HOST_SCRIPTS_A=""
AGGRESSOR_HOST_SCRIPTS_B=""
AGGRESSOR_HOST_SCRIPTS_C=""

EXTRA_GEM5_ARGS=()
EXTRA_CONFIG_ARGS=()

STAT_PATTERNS=(
    request_enqueue_count
    request_dequeue_count
    request_grant_count
    request_full_events
    request_blocked_cycles
    request_wait_ns
    request_wait_avg_ns
    request_wait_cycles
    request_wait_avg_cycles
    request_queue_occupancy_avg
    request_queue_occupancy_max
    response_enqueue_count
    response_dequeue_count
    response_grant_count
    response_full_events
    response_blocked_cycles
    response_wait_ns
    response_wait_avg_ns
    response_wait_cycles
    response_wait_avg_cycles
    response_queue_occupancy_avg
    response_queue_occupancy_max
    controller_req_queue_occupancy_avg
    controller_req_queue_occupancy_max
    controller_resp_queue_occupancy_avg
    controller_resp_queue_occupancy_max
    replay_buffer_host_occupancy_avg
    replay_buffer_host_occupancy_max
    replay_buffer_device_occupancy_avg
    replay_buffer_device_occupancy_max
    retry_buffer_req_occupancy_avg
    retry_buffer_req_occupancy_max
    retry_buffer_resp_occupancy_avg
    retry_buffer_resp_occupancy_max
    stall_req_cycles
    stall_resp_cycles
    stall_req_retry_cycles
    stall_resp_retry_cycles
    data_flit_req_read
    data_flit_req_write
    data_flit_resp_read
    data_flit_resp_write
    control_flit_req_read
    control_flit_req_write
    control_flit_resp_read
    control_flit_resp_write
)

usage() {
    cat <<'EOF'
Usage: ./multi_test.sh [options]

Runs the 4-host FlexCXL multi-host experiment matrix:
  balanced:          H0/H1/H2/H3 run the same YCSB workload
  aggressor-victim: H0 runs YCSB, H1/H2/H3 run STREAM

Default matrix:
  workloads: a,b,c
  policies:  rr, wrr-victim(7:1:1:1), wrr-aggressor(1:3:3:3)

Important:
  HOST_SCRIPTS are late-bound. The default placeholder is:
    host0.rcS,host1.rcS,host2.rcS,host3.rcS

Core options:
  --out-root DIR                 Output root directory (default: multi_results)
  --run-set all|balanced|aggressor
  --workloads a,b,c              Comma-separated workload list
  --policies rr,wrr-victim,wrr-aggressor,custom
  --priority-host-ratios CSV     Custom WRR weights, e.g. 2,4,1,3
  --host-scripts CSV             Exact per-host rcS CSV used for every run
  --dry-run                      Print commands and metadata, do not run gem5
  --skip-existing                Skip runs with an existing stats.txt
  --keep-going                   Continue matrix after a failed gem5 run
  --sudo / --no-sudo             Enable or disable sudo (default: --sudo)
  --wait-all-hosts, --host-script-barrier
                                 Wait until all hosts reach the script barrier,
                                 then release the host scripts together (default)
  --no-wait-all-hosts, --no-host-script-barrier
                                 Run each host script as soon as that guest reaches rcS
  --defer-non-trigger-host-scripts
                                 Legacy alias for --wait-all-hosts

Workload script options:
  --ycsb-a FILE                  Default: configs/boot/workloada_th4.rcS
  --ycsb-b FILE                  Default: configs/boot/workloadb_th4.rcS
  --ycsb-c FILE                  Default: configs/boot/workloadc_th4.rcS
  --stream-script FILE           Default: configs/boot/stream.rcS

Per-matrix exact script options:
  --balanced-host-scripts-a CSV
  --balanced-host-scripts-b CSV
  --balanced-host-scripts-c CSV
  --aggressor-host-scripts-a CSV
  --aggressor-host-scripts-b CSV
  --aggressor-host-scripts-c CSV

gem5 options:
  --gem5-binary FILE             Default: build/X86/gem5.opt
  --config-script FILE           Default: configs/fs.py
  --disk FILE                    Default: /home/esj/gem5_resource/ligra2.img
  --kernel FILE                  Default: /home/esj/gem5_resource/linux/vmlinux
  --cpu-type TYPE                Default: X86O3CPU
  --switch-start-cpu-type TYPE   Default: X86AtomicSimpleCPU
  --no-cpu-switch                Keep --switch-start-cpu-type for the run and only release deferred scripts
  --mem-size SIZE                Default: 3GB
  --cxl-mem-size SIZE            Default: 8GB
  --cxl-error-rate RATE          Override CXL CRC error probability (0..1)
  --host-cxl-ratios CSV          Default: 1,1,1,1
  --cxl-clock CLOCK              Default: 250MHz
  --debug-flags FLAGS            Default: CXL_ctrl,PciBridge2,CXLDRAMsim3
  --no-debug-flags               Do not pass --debug-flags
  --extra-gem5-arg ARG           Extra argument before configs/fs.py
  --extra-config-arg ARG         Extra argument after configs/fs.py

Examples:
  ./multi_test.sh --dry-run --no-sudo
  ./multi_test.sh --host-scripts host0.rcS,host1.rcS,host2.rcS,host3.rcS
  ./multi_test.sh --policies custom --priority-host-ratios 2,4,1,3
  ./multi_test.sh --ycsb-a ycsb_a.rcS --ycsb-b ycsb_b.rcS \
      --ycsb-c ycsb_c.rcS --stream-script stream.rcS
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

handle_interrupt() {
    local signal="$1"
    if [[ "$INTERRUPTED" -eq 0 ]]; then
        echo "error: interrupted by ${signal}; stopping multi_test.sh" >&2
    fi
    INTERRUPTED=1
}

abort_if_interrupted() {
    if [[ "$INTERRUPTED" -eq 1 ]]; then
        echo "error: interrupted; not starting another gem5 run" >&2
        exit 130
    fi
}

is_interrupt_status() {
    local status_code="$1"
    [[ "$status_code" -eq 130 || "$status_code" -eq 143 ]]
}

trap 'handle_interrupt INT' INT
trap 'handle_interrupt TERM' TERM

trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "$value"
}

quote_command() {
    printf '%q ' "$@"
    printf '\n'
}

repeat_four() {
    local script="$1"
    printf '%s,%s,%s,%s' "$script" "$script" "$script" "$script"
}

normalize_workload() {
    local workload
    workload="$(trim "$1")"
    case "$workload" in
        a|A|workloada|workloadA|WorkloadA) printf 'a' ;;
        b|B|workloadb|workloadB|WorkloadB) printf 'b' ;;
        c|C|workloadc|workloadC|WorkloadC) printf 'c' ;;
        *) die "unknown workload '$1' (expected a,b,c)" ;;
    esac
}

workload_label() {
    case "$1" in
        a) printf 'workloada' ;;
        b) printf 'workloadb' ;;
        c) printf 'workloadc' ;;
        *) die "unknown normalized workload '$1'" ;;
    esac
}

ycsb_script_for() {
    case "$1" in
        a) printf '%s' "$YCSB_A_SCRIPT" ;;
        b) printf '%s' "$YCSB_B_SCRIPT" ;;
        c) printf '%s' "$YCSB_C_SCRIPT" ;;
        *) die "unknown normalized workload '$1'" ;;
    esac
}

balanced_exact_scripts_for() {
    case "$1" in
        a) printf '%s' "$BALANCED_HOST_SCRIPTS_A" ;;
        b) printf '%s' "$BALANCED_HOST_SCRIPTS_B" ;;
        c) printf '%s' "$BALANCED_HOST_SCRIPTS_C" ;;
        *) die "unknown normalized workload '$1'" ;;
    esac
}

aggressor_exact_scripts_for() {
    case "$1" in
        a) printf '%s' "$AGGRESSOR_HOST_SCRIPTS_A" ;;
        b) printf '%s' "$AGGRESSOR_HOST_SCRIPTS_B" ;;
        c) printf '%s' "$AGGRESSOR_HOST_SCRIPTS_C" ;;
        *) die "unknown normalized workload '$1'" ;;
    esac
}

resolve_host_scripts() {
    local experiment_set="$1"
    local workload="$2"
    local exact=""
    local ycsb_script=""

    if [[ -n "$HOST_SCRIPTS_OVERRIDE" ]]; then
        printf '%s' "$HOST_SCRIPTS_OVERRIDE"
        return
    fi

    if [[ "$experiment_set" == "balanced" ]]; then
        exact="$(balanced_exact_scripts_for "$workload")"
        if [[ -n "$exact" ]]; then
            printf '%s' "$exact"
            return
        fi

        ycsb_script="$(ycsb_script_for "$workload")"
        if [[ -n "$ycsb_script" ]]; then
            repeat_four "$ycsb_script"
            return
        fi

        printf '%s' "$DEFAULT_HOST_SCRIPTS"
        return
    fi

    if [[ "$experiment_set" == "aggressor" ]]; then
        exact="$(aggressor_exact_scripts_for "$workload")"
        if [[ -n "$exact" ]]; then
            printf '%s' "$exact"
            return
        fi

        ycsb_script="$(ycsb_script_for "$workload")"
        if [[ -n "$ycsb_script" && -n "$STREAM_SCRIPT" ]]; then
            printf '%s,%s,%s,%s' \
                "$ycsb_script" "$STREAM_SCRIPT" "$STREAM_SCRIPT" "$STREAM_SCRIPT"
            return
        fi

        printf '%s' "$DEFAULT_HOST_SCRIPTS"
        return
    fi

    die "unknown experiment set '$experiment_set'"
}

priority_label_from_csv() {
    local csv="$1"
    printf '%s' "${csv//,/:}"
}

validate_priority_csv() {
    local csv="$1"
    local weights=()
    local weight
    local sum=0

    IFS=',' read -r -a weights <<< "$csv"
    [[ "${#weights[@]}" -eq "$NUM_HOSTS" ]] || \
        die "priority CSV must contain $NUM_HOSTS entries, got ${#weights[@]}: $csv"

    for weight in "${weights[@]}"; do
        weight="$(trim "$weight")"
        [[ "$weight" =~ ^[0-9]+$ ]] || \
            die "priority weights must be non-negative integers: $csv"
        sum=$((sum + weight))
    done

    [[ "$sum" -gt 0 ]] || die "priority weights must sum to > 0: $csv"
}

policy_args() {
    local policy
    policy="$(trim "$1")"
    case "$policy" in
        rr|RR|round-robin|5:5:5:5|5,5,5,5)
            POLICY_NAME="rr"
            ROUTING_MODE="round-robin"
            PRIORITY_HOST_RATIOS="5,5,5,5"
            PRIORITY_LABEL="5:5:5:5"
            ;;
        wrr-victim|victim|victim-favored|7:1:1:1|7,1,1,1)
            POLICY_NAME="wrr_victim"
            ROUTING_MODE="priority"
            PRIORITY_HOST_RATIOS="7,1,1,1"
            PRIORITY_LABEL="7:1:1:1"
            ;;
        wrr-aggressor|aggressor|aggressor-favored|1:7:7:7|1,7,7,7)
            POLICY_NAME="wrr_aggressor"
            ROUTING_MODE="priority"
            PRIORITY_HOST_RATIOS="1,7,7,7"
            PRIORITY_LABEL="1:7:7:7"
            ;;
        custom|priority|wrr-custom)
            [[ -n "$CUSTOM_PRIORITY_HOST_RATIOS" ]] || \
                die "--policies custom requires --priority-host-ratios CSV"
            validate_priority_csv "$CUSTOM_PRIORITY_HOST_RATIOS"
            POLICY_NAME="custom"
            ROUTING_MODE="priority"
            PRIORITY_HOST_RATIOS="$CUSTOM_PRIORITY_HOST_RATIOS"
            PRIORITY_LABEL="$(priority_label_from_csv "$CUSTOM_PRIORITY_HOST_RATIOS")"
            ;;
        *)
            die "unknown policy '$1'"
            ;;
    esac
}

validate_host_script_count() {
    local csv="$1"
    local count
    count="$(awk -F, '{ print NF }' <<<"$csv")"
    [[ "$count" -eq "$NUM_HOSTS" ]] || \
        die "host script CSV must contain $NUM_HOSTS entries, got $count: $csv"
}

prepare_summary() {
    mkdir -p "$OUT_ROOT"
    local summary="$OUT_ROOT/runs.tsv"
    if [[ ! -e "$summary" ]]; then
        printf 'status\texperiment_set\tworkload\tpolicy\trouting_mode\tpriority_host_ratios\thost_scripts\toutput_dir\tcommand\n' \
            > "$summary"
    fi
}

append_summary() {
    local status="$1"
    local experiment_set="$2"
    local workload="$3"
    local policy="$4"
    local routing_mode="$5"
    local priority="$6"
    local host_scripts="$7"
    local output_dir="$8"
    local command_line="$9"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$status" "$experiment_set" "$workload" "$policy" "$routing_mode" \
        "$priority" "$host_scripts" "$output_dir" "$command_line" \
        >> "$OUT_ROOT/runs.tsv"
}

write_metadata() {
    local output_dir="$1"
    local experiment_set="$2"
    local workload="$3"
    local policy="$4"
    local routing_mode="$5"
    local priority="$6"
    local host_scripts="$7"
    local command_line="$8"

    {
        printf 'key\tvalue\n'
        printf 'experiment_set\t%s\n' "$experiment_set"
        printf 'workload\t%s\n' "$workload"
        printf 'routing_scheme\t%s\n' "$routing_mode"
        printf 'policy\t%s\n' "$policy"
        printf 'priority_host_ratios\t%s\n' "$priority"
        printf 'host_scripts\t%s\n' "$host_scripts"
        printf 'num_hosts\t%s\n' "$NUM_HOSTS"
        printf 'host_cxl_ratios\t%s\n' "$HOST_CXL_RATIOS"
        printf 'host_cxl_ratios_note\t%s\n' \
            "configs/fs.py normalizes to integer sum 10; inspect gem5 resolved host CXL windows"
        printf 'cxl_mem_size\t%s\n' "$CXL_MEM_SIZE"
        printf 'cxl_error_rate\t%s\n' "$CXL_ERROR_RATE"
        printf 'mem_size\t%s\n' "$MEM_SIZE"
        printf 'cxl_clock\t%s\n' "$CXL_CLOCK"
        printf 'cpu_type\t%s\n' "$CPU_TYPE"
        printf 'switch_start_cpu_type\t%s\n' "$SWITCH_START_CPU_TYPE"
        printf 'cpu_switch\t%s\n' "$CPU_SWITCH"
        printf 'resetstats_on_m5_exit_switch\t%s\n' "$RESETSTATS_ON_SWITCH"
        printf 'host_script_barrier\t%s\n' "$DEFER_NON_TRIGGER_HOST_SCRIPTS"
        printf 'defer_non_trigger_host_scripts\t%s\n' "$DEFER_NON_TRIGGER_HOST_SCRIPTS"
        printf 'command\t%s\n' "$command_line"
    } > "$output_dir/experiment.tsv"
}

extract_stats() {
    local output_dir="$1"
    local stats_file="$output_dir/stats.txt"
    local extract_file="$output_dir/stats_extract.tsv"
    local patterns

    patterns="$(IFS=,; printf '%s' "${STAT_PATTERNS[*]}")"

    if [[ ! -s "$stats_file" ]]; then
        printf 'stat\tvalue\n' > "$extract_file"
        printf 'missing_stats_txt\t%s\n' "$stats_file" >> "$extract_file"
        return 0
    fi

    awk -v patterns="$patterns" '
        BEGIN {
            split(patterns, pat, ",")
            print "stat\tvalue"
        }
        /^[A-Za-z0-9_.:-]+[[:space:]]+[-+0-9.eE]+/ {
            for (i in pat) {
                if (index($1, pat[i]) > 0) {
                    print $1 "\t" $2
                    break
                }
            }
        }
    ' "$stats_file" > "$extract_file"
}

extract_workload() {
    local output_dir="$1"
    local extract_file="$output_dir/workload_extract.tsv"
    local regex

    regex='(Throughput|throughput|KTPS|Kops|READ.*(p99|99)|UPDATE.*(p99|99)|99thPercentileLatency|Copy:|Scale:|Add:|Triad:)'

    printf 'file\tline\n' > "$extract_file"
    while IFS= read -r file; do
        while IFS= read -r line; do
            printf '%s\t%s\n' "${file#$output_dir/}" "$line" >> "$extract_file"
        done < <(grep -E "$regex" "$file" || true)
    done < <(
        find "$output_dir" -maxdepth 2 -type f \
            \( -name '*pc.com*' -o -name 'simout' -o -name '*.log' -o -name '*.out' \) \
            2>/dev/null
    )
}

extract_run() {
    local output_dir="$1"
    extract_stats "$output_dir"
    extract_workload "$output_dir"
}

build_command() {
    local output_dir="$1"
    local routing_mode="$2"
    local priority="$3"
    local host_scripts="$4"

    CMD=()
    if [[ "$USE_SUDO" -eq 1 ]]; then
        CMD+=(sudo)
    fi

    CMD+=("$GEM5_BINARY" -d "$output_dir")
    if [[ -n "$DEBUG_FLAGS" ]]; then
        CMD+=(--debug-flags "$DEBUG_FLAGS")
    fi
    CMD+=("${EXTRA_GEM5_ARGS[@]}")
    CMD+=("$CONFIG_SCRIPT")
    CMD+=(--disk "$DISK_IMAGE")
    CMD+=(--kernel "$KERNEL_PATH")
    if [[ "$CPU_SWITCH" -eq 1 ]]; then
        CMD+=(--cpu-type="$CPU_TYPE")
    else
        CMD+=(--cpu-type="$SWITCH_START_CPU_TYPE")
    fi
    CMD+=(--cxl-mem-size "$CXL_MEM_SIZE")
    if [[ -n "$CXL_ERROR_RATE" ]]; then
        CMD+=(--cxl-error-rate "$CXL_ERROR_RATE")
    fi
    CMD+=(--mem-size "$MEM_SIZE")
    CMD+=(--ruby)
    CMD+=(--cxl-mode)
    CMD+=(--num-hosts "$NUM_HOSTS")
    if [[ "$CPU_SWITCH" -eq 1 ]]; then
        CMD+=(--switch-on-m5-exit)
        CMD+=(--switch-start-cpu-type="$SWITCH_START_CPU_TYPE")
    fi
    CMD+=(--host-scripts "$host_scripts")
    if [[ "$DEFER_NON_TRIGGER_HOST_SCRIPTS" -eq 1 ]]; then
        CMD+=(--defer-non-trigger-host-scripts)
        if [[ "$CPU_SWITCH" -ne 1 ]]; then
            CMD+=(--defer-host-scripts-without-cpu-switch)
        fi
    fi
    if [[ "$CPU_SWITCH" -eq 1 && "$RESETSTATS_ON_SWITCH" -eq 1 ]]; then
        CMD+=(--resetstats-on-m5-exit-switch)
    fi
    CMD+=(--host-cxl-ratios "$HOST_CXL_RATIOS")
    CMD+=(--num-dirs "$NUM_DIRS")
    CMD+=(--routing-mode-type "$routing_mode")
    CMD+=(--priority-limit "$PRIORITY_LIMIT")
    CMD+=(--priority-host-ratios "$priority")
    CMD+=(--CXL-clock "$CXL_CLOCK")
    CMD+=("${EXTRA_CONFIG_ARGS[@]}")
}

run_one() {
    local experiment_set="$1"
    local workload="$2"
    local policy="$3"
    local host_scripts="$4"
    local workload_name
    local output_dir
    local command_line
    local status
    local status_code

    workload_name="$(workload_label "$workload")"
    policy_args "$policy"
    validate_host_script_count "$host_scripts"

    output_dir="$OUT_ROOT/${experiment_set}_${workload_name}_${POLICY_NAME}"
    mkdir -p "$output_dir"

    build_command "$output_dir" "$ROUTING_MODE" "$PRIORITY_HOST_RATIOS" "$host_scripts"
    command_line="$(quote_command "${CMD[@]}")"
    write_metadata "$output_dir" "$experiment_set" "$workload_name" "$POLICY_NAME" \
        "$ROUTING_MODE" "$PRIORITY_LABEL" "$host_scripts" "$command_line"

    printf '%s\n' "$command_line" > "$output_dir/run_command.txt"

    if [[ "$SKIP_EXISTING" -eq 1 && -s "$output_dir/stats.txt" ]]; then
        echo "[skip] $output_dir has stats.txt"
        extract_run "$output_dir"
        append_summary "skipped" "$experiment_set" "$workload_name" "$POLICY_NAME" \
            "$ROUTING_MODE" "$PRIORITY_LABEL" "$host_scripts" "$output_dir" "$command_line"
        return 0
    fi

    if [[ "$DRY_RUN" -eq 1 ]]; then
        echo "[dry-run] $experiment_set $workload_name $POLICY_NAME"
        echo "          host_scripts=$host_scripts"
        echo "          out=$output_dir"
        echo "          $command_line"
        append_summary "dry-run" "$experiment_set" "$workload_name" "$POLICY_NAME" \
            "$ROUTING_MODE" "$PRIORITY_LABEL" "$host_scripts" "$output_dir" "$command_line"
        return 0
    fi

    echo "[run] $experiment_set $workload_name $POLICY_NAME -> $output_dir"
    set +e
    "${CMD[@]}"
    status_code=$?
    set -e

    if [[ "$INTERRUPTED" -eq 1 ]] || is_interrupt_status "$status_code"; then
        echo "error: interrupted while running $output_dir; stopping matrix" >&2
        exit 130
    elif [[ "$status_code" -eq 0 ]]; then
        status="ok"
    else
        status="failed"
    fi

    extract_run "$output_dir"
    append_summary "$status" "$experiment_set" "$workload_name" "$POLICY_NAME" \
        "$ROUTING_MODE" "$PRIORITY_LABEL" "$host_scripts" "$output_dir" "$command_line"

    if [[ "$status" == "failed" && "$KEEP_GOING" -ne 1 ]]; then
        die "gem5 failed for $output_dir"
    fi
}

run_balanced_matrix() {
    local workload_raw
    local policy_raw
    local workload
    local host_scripts

    for workload_raw in "${WORKLOAD_LIST[@]}"; do
        workload="$(normalize_workload "$workload_raw")"
        host_scripts="$(resolve_host_scripts balanced "$workload")"
        for policy_raw in "${POLICY_LIST[@]}"; do
            abort_if_interrupted
            run_one balanced "$workload" "$policy_raw" "$host_scripts"
        done
    done
}

run_aggressor_victim_matrix() {
    local workload_raw
    local policy_raw
    local workload
    local host_scripts

    for workload_raw in "${WORKLOAD_LIST[@]}"; do
        workload="$(normalize_workload "$workload_raw")"
        host_scripts="$(resolve_host_scripts aggressor "$workload")"
        for policy_raw in "${POLICY_LIST[@]}"; do
            abort_if_interrupted
            run_one aggressor "$workload" "$policy_raw" "$host_scripts"
        done
    done
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --out-root)
            OUT_ROOT="$2"
            shift 2
            ;;
        --out-root=*)
            OUT_ROOT="${1#*=}"
            shift
            ;;
        --run-set)
            RUN_SET="$2"
            shift 2
            ;;
        --run-set=*)
            RUN_SET="${1#*=}"
            shift
            ;;
        --workloads)
            WORKLOADS_CSV="$2"
            shift 2
            ;;
        --workloads=*)
            WORKLOADS_CSV="${1#*=}"
            shift
            ;;
        --policies)
            POLICIES_CSV="$2"
            POLICIES_SET=1
            shift 2
            ;;
        --policies=*)
            POLICIES_CSV="${1#*=}"
            POLICIES_SET=1
            shift
            ;;
        --host-scripts)
            HOST_SCRIPTS_OVERRIDE="$2"
            shift 2
            ;;
        --host-scripts=*)
            HOST_SCRIPTS_OVERRIDE="${1#*=}"
            shift
            ;;
        --ycsb-a)
            YCSB_A_SCRIPT="$2"
            shift 2
            ;;
        --ycsb-a=*)
            YCSB_A_SCRIPT="${1#*=}"
            shift
            ;;
        --ycsb-b)
            YCSB_B_SCRIPT="$2"
            shift 2
            ;;
        --ycsb-b=*)
            YCSB_B_SCRIPT="${1#*=}"
            shift
            ;;
        --ycsb-c)
            YCSB_C_SCRIPT="$2"
            shift 2
            ;;
        --ycsb-c=*)
            YCSB_C_SCRIPT="${1#*=}"
            shift
            ;;
        --stream-script)
            STREAM_SCRIPT="$2"
            shift 2
            ;;
        --stream-script=*)
            STREAM_SCRIPT="${1#*=}"
            shift
            ;;
        --balanced-host-scripts-a)
            BALANCED_HOST_SCRIPTS_A="$2"
            shift 2
            ;;
        --balanced-host-scripts-a=*)
            BALANCED_HOST_SCRIPTS_A="${1#*=}"
            shift
            ;;
        --balanced-host-scripts-b)
            BALANCED_HOST_SCRIPTS_B="$2"
            shift 2
            ;;
        --balanced-host-scripts-b=*)
            BALANCED_HOST_SCRIPTS_B="${1#*=}"
            shift
            ;;
        --balanced-host-scripts-c)
            BALANCED_HOST_SCRIPTS_C="$2"
            shift 2
            ;;
        --balanced-host-scripts-c=*)
            BALANCED_HOST_SCRIPTS_C="${1#*=}"
            shift
            ;;
        --aggressor-host-scripts-a)
            AGGRESSOR_HOST_SCRIPTS_A="$2"
            shift 2
            ;;
        --aggressor-host-scripts-a=*)
            AGGRESSOR_HOST_SCRIPTS_A="${1#*=}"
            shift
            ;;
        --aggressor-host-scripts-b)
            AGGRESSOR_HOST_SCRIPTS_B="$2"
            shift 2
            ;;
        --aggressor-host-scripts-b=*)
            AGGRESSOR_HOST_SCRIPTS_B="${1#*=}"
            shift
            ;;
        --aggressor-host-scripts-c)
            AGGRESSOR_HOST_SCRIPTS_C="$2"
            shift 2
            ;;
        --aggressor-host-scripts-c=*)
            AGGRESSOR_HOST_SCRIPTS_C="${1#*=}"
            shift
            ;;
        --gem5-binary)
            GEM5_BINARY="$2"
            shift 2
            ;;
        --gem5-binary=*)
            GEM5_BINARY="${1#*=}"
            shift
            ;;
        --config-script)
            CONFIG_SCRIPT="$2"
            shift 2
            ;;
        --config-script=*)
            CONFIG_SCRIPT="${1#*=}"
            shift
            ;;
        --disk)
            DISK_IMAGE="$2"
            shift 2
            ;;
        --disk=*)
            DISK_IMAGE="${1#*=}"
            shift
            ;;
        --kernel)
            KERNEL_PATH="$2"
            shift 2
            ;;
        --kernel=*)
            KERNEL_PATH="${1#*=}"
            shift
            ;;
        --cpu-type)
            CPU_TYPE="$2"
            shift 2
            ;;
        --cpu-type=*)
            CPU_TYPE="${1#*=}"
            shift
            ;;
        --switch-start-cpu-type)
            SWITCH_START_CPU_TYPE="$2"
            shift 2
            ;;
        --switch-start-cpu-type=*)
            SWITCH_START_CPU_TYPE="${1#*=}"
            shift
            ;;
        --cpu-switch)
            CPU_SWITCH=1
            shift
            ;;
        --no-cpu-switch)
            CPU_SWITCH=0
            shift
            ;;
        --mem-size)
            MEM_SIZE="$2"
            shift 2
            ;;
        --mem-size=*)
            MEM_SIZE="${1#*=}"
            shift
            ;;
        --cxl-mem-size)
            CXL_MEM_SIZE="$2"
            shift 2
            ;;
        --cxl-mem-size=*)
            CXL_MEM_SIZE="${1#*=}"
            shift
            ;;
        --cxl-error-rate)
            CXL_ERROR_RATE="$2"
            shift 2
            ;;
        --cxl-error-rate=*)
            CXL_ERROR_RATE="${1#*=}"
            shift
            ;;
        --host-cxl-ratios)
            HOST_CXL_RATIOS="$2"
            shift 2
            ;;
        --host-cxl-ratios=*)
            HOST_CXL_RATIOS="${1#*=}"
            shift
            ;;
        --cxl-clock)
            CXL_CLOCK="$2"
            shift 2
            ;;
        --cxl-clock=*)
            CXL_CLOCK="${1#*=}"
            shift
            ;;
        --debug-flags)
            DEBUG_FLAGS="$2"
            shift 2
            ;;
        --debug-flags=*)
            DEBUG_FLAGS="${1#*=}"
            shift
            ;;
        --no-debug-flags)
            DEBUG_FLAGS=""
            shift
            ;;
        --priority-limit)
            PRIORITY_LIMIT="$2"
            shift 2
            ;;
        --priority-limit=*)
            PRIORITY_LIMIT="${1#*=}"
            shift
            ;;
        --priority-host-ratios)
            CUSTOM_PRIORITY_HOST_RATIOS="$2"
            shift 2
            ;;
        --priority-host-ratios=*)
            CUSTOM_PRIORITY_HOST_RATIOS="${1#*=}"
            shift
            ;;
        --num-dirs)
            NUM_DIRS="$2"
            shift 2
            ;;
        --num-dirs=*)
            NUM_DIRS="${1#*=}"
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --skip-existing)
            SKIP_EXISTING=1
            shift
            ;;
        --keep-going)
            KEEP_GOING=1
            shift
            ;;
        --sudo)
            USE_SUDO=1
            shift
            ;;
        --no-sudo)
            USE_SUDO=0
            shift
            ;;
        --no-resetstats-on-switch)
            RESETSTATS_ON_SWITCH=0
            shift
            ;;
        --wait-all-hosts|--host-script-barrier|--defer-non-trigger-host-scripts)
            DEFER_NON_TRIGGER_HOST_SCRIPTS=1
            shift
            ;;
        --no-wait-all-hosts|--no-host-script-barrier|--no-defer-non-trigger-host-scripts)
            DEFER_NON_TRIGGER_HOST_SCRIPTS=0
            shift
            ;;
        --extra-gem5-arg)
            EXTRA_GEM5_ARGS+=("$2")
            shift 2
            ;;
        --extra-gem5-arg=*)
            EXTRA_GEM5_ARGS+=("${1#*=}")
            shift
            ;;
        --extra-config-arg)
            EXTRA_CONFIG_ARGS+=("$2")
            shift 2
            ;;
        --extra-config-arg=*)
            EXTRA_CONFIG_ARGS+=("${1#*=}")
            shift
            ;;
        *)
            die "unknown option '$1'"
            ;;
    esac
done

[[ "$NUM_HOSTS" -eq 4 ]] || die "multi_test.sh currently expects NUM_HOSTS=4"

case "$RUN_SET" in
    all|balanced|aggressor) ;;
    *) die "--run-set must be all, balanced, or aggressor" ;;
esac

if [[ -n "$CUSTOM_PRIORITY_HOST_RATIOS" ]]; then
    validate_priority_csv "$CUSTOM_PRIORITY_HOST_RATIOS"
    if [[ "$POLICIES_SET" -eq 0 ]]; then
        POLICIES_CSV="custom"
    fi
fi

IFS=',' read -r -a WORKLOAD_LIST <<< "$WORKLOADS_CSV"
IFS=',' read -r -a POLICY_LIST <<< "$POLICIES_CSV"
[[ "${#WORKLOAD_LIST[@]}" -gt 0 ]] || die "empty --workloads"
[[ "${#POLICY_LIST[@]}" -gt 0 ]] || die "empty --policies"

prepare_summary

case "$RUN_SET" in
    all)
        run_balanced_matrix
        run_aggressor_victim_matrix
        ;;
    balanced)
        run_balanced_matrix
        ;;
    aggressor)
        run_aggressor_victim_matrix
        ;;
esac

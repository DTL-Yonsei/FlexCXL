#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: ./run_multi.sh 1|2|3 [extra multi_test.sh options]

Policy selector:
  1  rr
  2  wrr-victim
  3  wrr-aggressor

Examples:
  ./run_multi.sh 1
  ./run_multi.sh 2 --skip-existing
  ./run_multi.sh 3 --workloads c --out-root multi_av_th4_stream_cf50_c_only
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 2
fi

selector="$1"
shift

case "$selector" in
    1|rr|RR|round-robin)
        policy="rr"
        ;;
    2|wrr-victim|victim|victim-favored|7:1:1:1|7,1,1,1)
        policy="wrr-victim"
        ;;
    3|wrr-aggressor|aggressor|aggressor-favored|1:3:3:3|1,3,3,3)
        policy="wrr-aggressor"
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        usage
        exit 2
        ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$script_dir/multi_test.sh" \
    --run-set aggressor \
    --workloads a,b,c \
    --policies "$policy" \
    --out-root multi_av_th4_stream_cf50 \
    --skip-existing \
    --keep-going \
    --defer-non-trigger-host-scripts \
    "$@"

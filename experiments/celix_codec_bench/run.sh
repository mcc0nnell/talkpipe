#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_root"

runs="${RUNS:-1}"
if ! [[ "$runs" =~ ^[1-9][0-9]*$ ]]; then
    echo "RUNS must be a positive integer" >&2
    exit 2
fi

docker build -f experiments/celix_codec_bench/Dockerfile -t talkpipe-celix-codec-bench .
for ((run = 1; run <= runs; ++run)); do
    echo "RUN=$run"
    if docker run --rm talkpipe-celix-codec-bench; then
        echo "EXIT=0"
    else
        status=$?
        echo "EXIT=$status"
        exit "$status"
    fi
done

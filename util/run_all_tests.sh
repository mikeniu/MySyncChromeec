#!/usr/bin/env bash

TEST_SCRIPT="./test_jlink_version.sh"
LOG_DIR="bisection_logs"
MASTER_LOG="$LOG_DIR/master_bisection_log.txt"

if [[ ! -x "$TEST_SCRIPT" ]]; then
    echo "Error: $TEST_SCRIPT not found or not executable."
    exit 1
fi

mkdir -p "$LOG_DIR"

echo "J-Link High Latency Bisection Sweep" | tee "$MASTER_LOG"
echo "Date: $(date)" >> "$MASTER_LOG"
echo "=========================================" | tee -a "$MASTER_LOG"

echo "Caching sudo credentials for the run..."
sudo -v

# Find all JLink directories and sort them by version number
for item in $(ls -1d JLink_Linux_V* | sort -V); do
    if [[ ! -d "$item" ]]; then
        continue
    fi

    VERSION_NAME=$(basename "$item")
    echo -e "\n=== Testing $VERSION_NAME ===" | tee -a "$MASTER_LOG"

    "$TEST_SCRIPT" "$item"
    EXIT_CODE=$?

    if [[ $EXIT_CODE -eq 0 ]]; then
        RESULT="[ GOOD  ]"
    elif [[ $EXIT_CODE -eq 125 ]]; then
        RESULT="[ ERROR ] (Environment setup failed)"
    else
        RESULT="[  BAD  ]"
    fi

    echo "$RESULT $VERSION_NAME" | tee -a "$MASTER_LOG"

    # Keep the sudo credential cache alive for the next iteration
    sudo -v
done

echo -e "\n=========================================" | tee -a "$MASTER_LOG"
echo "Sweep complete! Summary and individual logs saved to $LOG_DIR/."
#!/usr/bin/env bash

KNOWN_GOOD_DIR="$(pwd)/JLink_Linux_V684a_x86_64"
TEST_ITERATIONS=3

if [[ -z "$1" ]]; then
    echo "Usage: $0 <path_to_jlink_directory>"
    exit 1
fi

JLINK_DIR="${1%/}"
VERSION_NAME=$(basename "$JLINK_DIR")
PORT=19020
TIMEOUT_SECS=60

BASE_DIR="$(pwd)"
LOG_DIR="$BASE_DIR/bisection_logs"
mkdir -p "$LOG_DIR"

SERVER_OUT="$LOG_DIR/${VERSION_NAME}_server_log.txt"
CLIENT_OUT="$LOG_DIR/${VERSION_NAME}_client_out.txt"

if [[ ! -x "$JLINK_DIR/JLinkRemoteServerCLExe" ]] || [[ ! -x "$JLINK_DIR/JLinkExe" ]]; then
    echo "ERROR: Could not find executables in $JLINK_DIR"
    exit 1
fi

cleanup() {
    sudo pkill -9 -f "[J]LinkRemoteServerCLExe" >/dev/null 2>&1
    sudo pkill -9 -f "[J]LinkExe" >/dev/null 2>&1
    sudo rm -f /tmp/JLink* >/dev/null 2>&1

    while ss -antl 2>/dev/null | grep -q ":$PORT "; do
        sleep 0.1
    done

    sleep 1
}

sanitize_hardware() {
    echo ">>> Sanitizing hardware state..."
    (
        cd "$KNOWN_GOOD_DIR" || exit 1
        echo -e "r\nq" | timeout 10 ./JLinkExe \
            -device STM32H743ZI \
            -if SWD \
            -speed 4000 \
            -autoconnect 1 > /dev/null 2>&1
    )
    # Give the STM32H7 plenty of time to stabilize clocks after reset.
    sleep 3
}

trap cleanup EXIT

echo "================================================="
echo ">>> Testing Version: $VERSION_NAME"
echo "================================================="

for (( i=1; i<=TEST_ITERATIONS; i++ )); do
    echo "--- Attempt $i of $TEST_ITERATIONS ---"

    cleanup
    sanitize_hardware

    # Start server in a subshell.
    (
        cd "$JLINK_DIR" || exit 1
        sudo ./JLinkRemoteServerCLExe -Port $PORT -select USB > "$SERVER_OUT" 2>&1 &
    )

    WAIT_COUNT=0
    while ! ss -antl 2>/dev/null | grep -q ":$PORT "; do
        sleep 0.1
        ((WAIT_COUNT++))
        if [[ $WAIT_COUNT -ge 50 ]]; then
            echo ">>> RESULT: ERROR (Server failed to start or bind to port $PORT)."
            exit 125
        fi
    done

    # Start client in a subshell.
    (
        cd "$JLINK_DIR" || exit 1
        echo "q" | timeout $TIMEOUT_SECS ./JLinkExe \
            -ip 127.0.0.1:$PORT \
            -device STM32H743ZI \
            -if SWD \
            -speed 4000 \
            -autoconnect 1 \
            -ExitOnError 1 > "$CLIENT_OUT" 2>&1
    )
    CLIENT_EXIT_CODE=$?

    if [[ $CLIENT_EXIT_CODE -eq 124 ]]; then
        echo ">>> RESULT: FAILURE (Timed out on attempt $i)."
        exit 1
    fi

    if [[ -f "$CLIENT_OUT" ]]; then
        if grep -q "Error occurred:" "$CLIENT_OUT" || grep -q "Connect failed" "$CLIENT_OUT"; then
            echo ">>> RESULT: FAILURE (Target connection failed on attempt $i)."
            exit 1
        fi
    else
        echo ">>> RESULT: FAILURE (Log file missing on attempt $i)."
        exit 1
    fi

    if [[ $CLIENT_EXIT_CODE -ne 0 ]]; then
        echo ">>> RESULT: FAILURE (Client exited with code $CLIENT_EXIT_CODE on attempt $i)."
        exit 1
    fi

    echo "    Attempt $i Passed!"
done

echo ">>> RESULT: SUCCESS ($VERSION_NAME passed $TEST_ITERATIONS/$TEST_ITERATIONS attempts)."
exit 0
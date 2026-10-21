#!/usr/bin/env bash

# Boots the current kernel and initramfs in QEMU without a terminal and checks
# that every expected boot line appears in order: the /init lines, the initserv
# stub banner and one "self-test passed" line per self-test listed in the stub.
# Stops QEMU as soon as the result is known. Exit code 0 means a clean boot.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
STUB_SOURCE="${SYSTEM_DIR}/Boot/InitservStub/initserv_stub.c"
LOG_FILE="${LOG_FILE:-${SYSTEM_DIR}/images/qemu-boot.log}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-900}"

if [[ ! "${BOOT_TIMEOUT}" =~ ^[1-9][0-9]*$ ]]; then
    echo "BOOT_TIMEOUT must be a positive number of seconds: ${BOOT_TIMEOUT}" >&2
    exit 1
fi

EXPECTED_LINES=(
    "PUNos init: userspace started"
    "PUNos init: filesystems ready"
    "PUNos initserv stub: smoke-test process is running"
)
while IFS= read -r line; do
    EXPECTED_LINES+=("${line}")
done < <(grep -oE 'PUNos initserv stub: [a-z0-9]+ self-test passed' "${STUB_SOURCE}")

if [[ "${#EXPECTED_LINES[@]}" -le 3 ]]; then
    echo "No self-tests found in ${STUB_SOURCE}" >&2
    exit 1
fi

EXPECTED_LINES+=("PUNos init: initserv started" "PUNos initserv: logserv ready" "PUNos initserv: services started" "PUNos services: appserv ready")

mkdir -p "$(dirname -- "${LOG_FILE}")"
: > "${LOG_FILE}"

INIT_PATH=/init-smoke "${SCRIPT_DIR}/run-qemu-initramfs.sh" < /dev/null > "${LOG_FILE}" 2>&1 &
QEMU_PID=$!
trap 'kill "${QEMU_PID}" 2>/dev/null || true; wait "${QEMU_PID}" 2>/dev/null || true' EXIT

# Prints the line number of the first log line containing $1, or nothing.
first_line_with() {
    tr -d '\r' < "${LOG_FILE}" | grep -n -F -m 1 -- "$1" | cut -d: -f1 || true
}

LAST_LINE="${EXPECTED_LINES[${#EXPECTED_LINES[@]} - 1]}"
DEADLINE=$((SECONDS + BOOT_TIMEOUT))
RESULT=""
while [[ -z "${RESULT}" ]]; do
    if grep -q -E 'self-test failed|PUNos init: .* failed|Kernel panic' "${LOG_FILE}"; then
        RESULT="failed"
    elif [[ -n "$(first_line_with "${LAST_LINE}")" ]]; then
        RESULT="finished"
    elif ! kill -0 "${QEMU_PID}" 2>/dev/null; then
        RESULT="QEMU exited"
    elif ((SECONDS >= DEADLINE)); then
        RESULT="timeout after ${BOOT_TIMEOUT} s"
    else
        sleep 2
    fi
done

STATUS=0
PREVIOUS=0
for expected in "${EXPECTED_LINES[@]}"; do
    NUMBER="$(first_line_with "${expected}")"
    if [[ -z "${NUMBER}" ]]; then
        echo "Missing boot line: ${expected}" >&2
        STATUS=1
    elif ((NUMBER < PREVIOUS)); then
        echo "Boot line out of order: ${expected}" >&2
        STATUS=1
    else
        PREVIOUS="${NUMBER}"
    fi
done

if [[ "${RESULT}" != "finished" || "${STATUS}" -ne 0 ]]; then
    echo "PUNos boot check failed (${RESULT}). Serial log: ${LOG_FILE}" >&2
    tr -d '\r' < "${LOG_FILE}" | grep -E 'PUNos|failed|panic' >&2 || true
    exit 1
fi

echo "PUNos boot check passed: ${#EXPECTED_LINES[@]} boot lines in order."
echo "Serial log: ${LOG_FILE}"

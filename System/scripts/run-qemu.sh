#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
KERNEL_IMAGE="${SYSTEM_DIR}/linux-build/arch/x86/boot/bzImage"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "Required command is not installed: qemu-system-x86_64" >&2
    exit 1
fi

if [[ ! -f "${KERNEL_IMAGE}" ]]; then
    echo "Kernel image not found: ${KERNEL_IMAGE}" >&2
    echo "Run ${SCRIPT_DIR}/build-kernel.sh first." >&2
    exit 1
fi

echo "Starting the bare PUNos kernel in QEMU."
echo "A kernel panic caused by the missing root filesystem is expected."
echo "Press Ctrl+A, then X to quit QEMU."

exec qemu-system-x86_64 \
    -machine q35 \
    -accel tcg \
    -cpu max \
    -smp 2 \
    -m 512M \
    -kernel "${KERNEL_IMAGE}" \
    -append "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 panic=-1" \
    -nographic \
    -no-reboot

#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
KERNEL_IMAGE="${SYSTEM_DIR}/linux-build/arch/x86/boot/bzImage"
INITRAMFS_IMAGE="${SYSTEM_DIR}/images/punos-initramfs.cpio.gz"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "Required command is not installed: qemu-system-x86_64" >&2
    exit 1
fi
if [[ ! -f "${KERNEL_IMAGE}" ]]; then
    echo "Kernel image not found: ${KERNEL_IMAGE}" >&2
    exit 1
fi
if [[ ! -f "${INITRAMFS_IMAGE}" ]]; then
    echo "Initramfs image not found: ${INITRAMFS_IMAGE}" >&2
    echo "Run ${SCRIPT_DIR}/build-initramfs.sh first." >&2
    exit 1
fi

echo "Starting PUNos kernel and initramfs in QEMU."
echo "Press Ctrl+A, then X to quit QEMU."

exec qemu-system-x86_64 \
    -machine q35 \
    -accel tcg \
    -cpu max \
    -smp 2 \
    -m 512M \
    -kernel "${KERNEL_IMAGE}" \
    -initrd "${INITRAMFS_IMAGE}" \
    -append "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 rdinit=/init panic=-1" \
    -nographic \
    -no-reboot

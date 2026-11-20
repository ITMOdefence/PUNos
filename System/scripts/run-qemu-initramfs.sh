#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
KERNEL_IMAGE="${PUNOS_KERNEL_IMAGE:-${SYSTEM_DIR}/linux-build/arch/x86/boot/bzImage}"
INITRAMFS_IMAGE="${PUNOS_INITRAMFS_IMAGE:-${SYSTEM_DIR}/images/punos-initramfs.cpio.gz}"
INIT_PATH="${INIT_PATH:-/init}"
if [[ "${INIT_PATH}" != "/init" && "${INIT_PATH}" != "/init-smoke" ]]; then
    echo "INIT_PATH must be /init or /init-smoke" >&2; exit 1
fi

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

KERNEL_COMMAND_LINE="console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 rdinit=${INIT_PATH} panic=-1"
QEMU_ARGUMENTS=(-nographic -no-reboot)
if [[ -n "${PUNOS_DATA_IMAGE:-}" ]]; then
    if [[ ! -f "${PUNOS_DATA_IMAGE}" || ! -w "${PUNOS_DATA_IMAGE}" ]]; then
        echo "PUNOS_DATA_IMAGE must name an existing writable raw ext4 image" >&2
        exit 1
    fi
    # QEMU key/value options escape a literal comma by doubling it.
    DATA_IMAGE_OPTION="${PUNOS_DATA_IMAGE//,/,,}"
    QEMU_ARGUMENTS+=(-drive "file=${DATA_IMAGE_OPTION},format=raw,if=virtio")
    KERNEL_COMMAND_LINE+=" punos.data=/dev/vda"
fi
if [[ -n "${PUNOS_HOST_PORT:-}" ]]; then
    GUEST_PORT="${PUNOS_GUEST_PORT:-${PUNOS_HOST_PORT}}"
    HOST_ADDRESS="${PUNOS_HOST_ADDRESS:-127.0.0.1}"
    for port in "${PUNOS_HOST_PORT}" "${GUEST_PORT}"; do
        if [[ ! "${port}" =~ ^[1-9][0-9]{0,4}$ ]] || ((10#${port} > 65535)); then
            echo "Forwarded TCP ports must be integers in 1..65535" >&2
            exit 1
        fi
    done
    if [[ ! "${HOST_ADDRESS}" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
        echo "PUNOS_HOST_ADDRESS must be a numeric IPv4 address" >&2
        exit 1
    fi
    IFS=. read -r -a ADDRESS_OCTETS <<< "${HOST_ADDRESS}"
    for octet in "${ADDRESS_OCTETS[@]}"; do
        if ((10#${octet} > 255)); then
            echo "PUNOS_HOST_ADDRESS must be a numeric IPv4 address" >&2
            exit 1
        fi
    done
    QEMU_ARGUMENTS+=(-nic "user,model=virtio-net-pci,hostfwd=tcp:${HOST_ADDRESS}:${PUNOS_HOST_PORT}-:${GUEST_PORT}")
    KERNEL_COMMAND_LINE+=" ip=10.0.2.15::10.0.2.2:255.255.255.0::eth0:off"
elif [[ -n "${PUNOS_GUEST_PORT:-}" || -n "${PUNOS_HOST_ADDRESS:-}" ]]; then
    echo "PUNOS_GUEST_PORT and PUNOS_HOST_ADDRESS require PUNOS_HOST_PORT" >&2
    exit 1
fi

exec qemu-system-x86_64 \
    -machine q35 \
    -accel tcg \
    -cpu max \
    -smp 2 \
    -m 512M \
    -kernel "${KERNEL_IMAGE}" \
    -initrd "${INITRAMFS_IMAGE}" \
    -append "${KERNEL_COMMAND_LINE}" \
    "${QEMU_ARGUMENTS[@]}"

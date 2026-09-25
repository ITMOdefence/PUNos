#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BOOT_DIR="${SYSTEM_DIR}/Boot"
ROOTFS_DIR="${SYSTEM_DIR}/rootfs"
IMAGES_DIR="${SYSTEM_DIR}/images"
ARCHIVE_PATH="${IMAGES_DIR}/punos-initramfs.cpio.gz"

for command_name in cpio find grep gzip install make nm readelf sort touch; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command is not installed: ${command_name}" >&2
        exit 1
    fi
done

make --directory "${BOOT_DIR}" all

verify_elf() {
    local executable="$1"

    if ! readelf --file-header "${executable}" | grep -q \
        'Machine:.*Advanced Micro Devices X86-64'; then
        echo "Not a Linux x86_64 ELF: ${executable}" >&2
        exit 1
    fi
    if readelf --program-headers "${executable}" | grep -q 'INTERP'; then
        echo "ELF contains a forbidden PT_INTERP segment: ${executable}" >&2
        exit 1
    fi
    if nm --undefined-only "${executable}" | grep -q .; then
        echo "ELF contains unresolved symbols: ${executable}" >&2
        exit 1
    fi
}

verify_elf "${BOOT_DIR}/build/init"
verify_elf "${BOOT_DIR}/build/initserv"
verify_elf "${SYSTEM_DIR}/NativeRuntime/SyscallWrappers/build/full_wrappers_runtime"

mkdir -p "${ROOTFS_DIR}" "${IMAGES_DIR}"
find "${ROOTFS_DIR}" -mindepth 1 -delete
chmod 0755 "${ROOTFS_DIR}"

install -d -m 0755 \
    "${ROOTFS_DIR}/dev" \
    "${ROOTFS_DIR}/run" \
    "${ROOTFS_DIR}/punos" \
    "${ROOTFS_DIR}/punos/bin" \
    "${ROOTFS_DIR}/punos/apps" \
    "${ROOTFS_DIR}/punos/config" \
    "${ROOTFS_DIR}/var" \
    "${ROOTFS_DIR}/var/log" \
    "${ROOTFS_DIR}/var/log/punos"
install -d -m 0555 "${ROOTFS_DIR}/proc" "${ROOTFS_DIR}/sys"
install -d -m 01777 "${ROOTFS_DIR}/tmp"
install -m 0755 "${BOOT_DIR}/build/init" "${ROOTFS_DIR}/init"
install -m 0755 "${BOOT_DIR}/build/initserv" \
    "${ROOTFS_DIR}/punos/bin/initserv"
install -m 0755 \
    "${SYSTEM_DIR}/NativeRuntime/SyscallWrappers/build/full_wrappers_runtime" \
    "${ROOTFS_DIR}/punos/bin/syscall-selftest"

# Normalize mtimes and archive order. cpio records uid/gid as root explicitly.
find "${ROOTFS_DIR}" -exec touch --no-dereference --date=@0 {} +
(
    cd "${ROOTFS_DIR}"
    find . -print0 \
        | sort --zero-terminated \
        | cpio --null --create --format=newc --reproducible --owner=0:0 \
        | gzip --no-name --best > "${ARCHIVE_PATH}.part"
)
mv "${ARCHIVE_PATH}.part" "${ARCHIVE_PATH}"

echo "PUNos initramfs is ready: ${ARCHIVE_PATH}"
echo "Staging rootfs: ${ROOTFS_DIR}"

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

# Self-tests run by the initserv smoke-test stub: "<built file>:<name in /punos/bin>".
SELF_TESTS=(
    "NativeRuntime/SyscallWrappers/build/full_wrappers_runtime:syscall-selftest"
    "SystemLibs/libcore/build/core_runtime:libcore-selftest"
    "SystemLibs/libmath/build/math_runtime:libmath-selftest"
    "SystemLibs/libmem/build/memory_runtime:libmem-selftest"
    "SystemLibs/librandom/build/random_runtime:librandom-selftest"
    "SystemLibs/libgraphics/build/graphics_runtime:libgraphics-selftest"
    "SystemLibs/libstr/build/string_runtime:libstr-selftest"
    "SystemLibs/libfile/build/file_runtime:libfile-selftest"
    "SystemLibs/libtime/build/time_runtime:libtime-selftest"
    "SystemLibs/libprocess/build/process_runtime:libprocess-selftest"
    "SystemLibs/libthread/build/thread_runtime:libthread-selftest"
    "SystemLibs/libsync/build/sync_runtime:libsync-selftest"
    "SystemLibs/libeventloop/build/eventloop_runtime:libeventloop-selftest"
    "SystemLibs/libinputdevice/build/input_device_runtime:libinputdevice-selftest"
    "SystemLibs/libv4l2/build/v4l2_runtime:libv4l2-selftest"
    "SystemServices/displayserv/build/displayserv_runtime:displayserv-selftest"
)

SERVICES=(
    "SystemServices/displayserv/build/displayserv:displayserv"
    "SystemServices/compositorserv/build/compositorserv:compositorserv"
    "SystemServices/windowserv/build/windowserv:windowserv"
)

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
for self_test in "${SELF_TESTS[@]}"; do
    verify_elf "${SYSTEM_DIR}/${self_test%%:*}"
done
for service in "${SERVICES[@]}"; do
    verify_elf "${SYSTEM_DIR}/${service%%:*}"
done

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
for self_test in "${SELF_TESTS[@]}"; do
    install -m 0755 "${SYSTEM_DIR}/${self_test%%:*}" \
        "${ROOTFS_DIR}/punos/bin/${self_test##*:}"
done
for service in "${SERVICES[@]}"; do
    install -m 0755 "${SYSTEM_DIR}/${service%%:*}" \
        "${ROOTFS_DIR}/punos/bin/${service##*:}"
done

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

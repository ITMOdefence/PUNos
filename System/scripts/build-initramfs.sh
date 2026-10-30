#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BOOT_DIR="${SYSTEM_DIR}/Boot"
ROOTFS_DIR="${SYSTEM_DIR}/rootfs"
IMAGES_DIR="${SYSTEM_DIR}/images"
ARCHIVE_PATH="${IMAGES_DIR}/punos-initramfs.cpio.gz"
REGISTRY="${PUNOS_REGISTRY:-${SYSTEM_DIR}/SystemServices/platform_registry.json}"
IMAGE_MODE="${PUNOS_IMAGE_MODE:-production}"
if [[ "${IMAGE_MODE}" != production && "${IMAGE_MODE}" != smoke ]]; then
    echo "PUNOS_IMAGE_MODE must be production or smoke" >&2
    exit 1
fi

for command_name in cpio find grep gzip install make nm python3 readelf sort touch; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command is not installed: ${command_name}" >&2
        exit 1
    fi
done

make --directory "${BOOT_DIR}" REGISTRY="${REGISTRY}" all

ACTIVE_EXTRA_SERVICES="$(python3 - "${REGISTRY}" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as source:
    registry = json.load(source)
for item in registry['services']:
    if item['service_id'] not in ('logserv', 'appserv') and item['enabled']:
        if item['executable'] != '/punos/bin/' + item['service_id']:
            raise SystemExit('enabled service requires /punos/bin executable')
        print(item['service_id'])
PY
)"

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
    "SpecialLibs/ipclib/build/ipc_runtime:ipc-selftest"
    "SpecialLibs/loglib/build/log_runtime:log-selftest"
    "SpecialLibs/inputlib/build/input_runtime:input-selftest"
    "SpecialLibs/camlib/build/camera_runtime:camera-selftest"
    "SpecialLibs/displaylib/build/display_runtime:display-selftest"
    "ApplicationLayer/appframework/build/lifecycle_runtime:appfw-lifecycle-selftest"
    "ApplicationLayer/appframework/build/create_runtime:appfw-create-selftest"
    "ApplicationLayer/appframework/build/run_runtime:appfw-run-selftest"
    "ApplicationLayer/appframework/build/fault_runtime:appfw-fault-selftest"
    "ApplicationLayer/appframework/build/logserv_runtime:appfw-logserv-selftest"
    "ApplicationLayer/appframework/build/apps_wire_runtime:appfw-wire-selftest"
    "ApplicationLayer/appframework/build/apps_runtime:appfw-apps-selftest"
    "ApplicationLayer/appframework/build/messaging_runtime:appfw-messaging-selftest"
    "SystemServices/appserv/build/service_test:appserv-selftest"
    "SystemServices/msgserv/build/broker_test:msgserv-selftest"
    "SystemServices/logserv/build/queue_test:logserv-selftest"
    "SystemServices/initserv/build/policy_test:initserv-selftest"
    "SystemServices/displayserv/build/displayserv_runtime:displayserv-selftest"
    "SystemServices/compositorserv/build/compositorserv_runtime:compositorserv-selftest"
    "SystemServices/windowserv/build/windowserv_runtime:windowserv-selftest"
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
verify_elf "${SYSTEM_DIR}/SystemServices/initserv/build/initserv"
verify_elf "${SYSTEM_DIR}/SystemServices/logserv/build/logserv"
verify_elf "${SYSTEM_DIR}/SystemServices/appserv/build/appserv"
for service in ${ACTIVE_EXTRA_SERVICES}; do
    verify_elf "${SYSTEM_DIR}/SystemServices/${service}/build/${service}"
done
if [[ "${IMAGE_MODE}" == smoke ]]; then
    verify_elf "${BOOT_DIR}/build/initserv-smoke"
    verify_elf "${BOOT_DIR}/build/init-smoke"
    verify_elf "${BOOT_DIR}/build/services-bootcheck"
    for self_test in "${SELF_TESTS[@]}"; do
        verify_elf "${SYSTEM_DIR}/${self_test%%:*}"
    done
fi

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
install -m 0755 "${SYSTEM_DIR}/SystemServices/initserv/build/initserv" "${ROOTFS_DIR}/punos/bin/initserv"
install -m 0755 "${SYSTEM_DIR}/SystemServices/logserv/build/logserv" "${ROOTFS_DIR}/punos/bin/logserv"
install -m 0755 "${SYSTEM_DIR}/SystemServices/appserv/build/appserv" \
    "${ROOTFS_DIR}/punos/bin/appserv"
for service in ${ACTIVE_EXTRA_SERVICES}; do
    install -m 0755 "${SYSTEM_DIR}/SystemServices/${service}/build/${service}" \
        "${ROOTFS_DIR}/punos/bin/${service}"
done
if [[ "${IMAGE_MODE}" == smoke ]]; then
    install -m 0755 "${BOOT_DIR}/build/initserv-smoke" "${ROOTFS_DIR}/punos/bin/initserv-smoke"
    install -m 0755 "${BOOT_DIR}/build/init-smoke" "${ROOTFS_DIR}/init-smoke"
    install -m 0755 "${BOOT_DIR}/build/services-bootcheck" "${ROOTFS_DIR}/punos/bin/services-bootcheck"
    for self_test in "${SELF_TESTS[@]}"; do
        install -m 0755 "${SYSTEM_DIR}/${self_test%%:*}" \
            "${ROOTFS_DIR}/punos/bin/${self_test##*:}"
    done
fi

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

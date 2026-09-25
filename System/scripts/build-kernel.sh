#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
KERNEL_DIR="${SYSTEM_DIR}/kernel"
BUILD_DIR="${SYSTEM_DIR}/linux-build"

VERSION_FILE="${KERNEL_DIR}/version"
CONFIG_FILE="${KERNEL_DIR}/configs/x86_64.config"

if [[ ! -f "${VERSION_FILE}" ]]; then
    echo "Kernel version file not found: ${VERSION_FILE}" >&2
    exit 1
fi

KERNEL_VERSION="$(tr -d '[:space:]' < "${VERSION_FILE}")"
SOURCE_DIR="${SYSTEM_DIR}/linux-${KERNEL_VERSION}"
KERNEL_IMAGE="${BUILD_DIR}/arch/x86/boot/bzImage"

if [[ ! -f "${SOURCE_DIR}/Makefile" ]]; then
    echo "Kernel sources not found: ${SOURCE_DIR}" >&2
    echo "Run ${SCRIPT_DIR}/prepare-kernel.sh first." >&2
    exit 1
fi

if [[ ! -f "${CONFIG_FILE}" ]]; then
    echo "Kernel configuration not found: ${CONFIG_FILE}" >&2
    exit 1
fi

for command_name in make gcc; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command is not installed: ${command_name}" >&2
        exit 1
    fi
done

if [[ -n "${JOBS:-}" ]]; then
    BUILD_JOBS="${JOBS}"
elif command -v nproc >/dev/null 2>&1; then
    BUILD_JOBS="$(nproc)"
else
    BUILD_JOBS=1
fi

if [[ ! "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive integer: ${BUILD_JOBS}" >&2
    exit 1
fi

mkdir -p "${BUILD_DIR}"
cp "${CONFIG_FILE}" "${BUILD_DIR}/.config"

echo "Preparing Linux ${KERNEL_VERSION} configuration..."
make --directory "${SOURCE_DIR}" \
    O="${BUILD_DIR}" \
    olddefconfig

echo "Building Linux ${KERNEL_VERSION} with ${BUILD_JOBS} jobs..."
make --directory "${SOURCE_DIR}" \
    O="${BUILD_DIR}" \
    --jobs "${BUILD_JOBS}" \
    bzImage

if [[ ! -f "${KERNEL_IMAGE}" ]]; then
    echo "Kernel build finished without producing ${KERNEL_IMAGE}" >&2
    exit 1
fi

echo "Kernel image is ready: ${KERNEL_IMAGE}"

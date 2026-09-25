#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
KERNEL_DIR="${SYSTEM_DIR}/kernel"
DOWNLOAD_DIR="${SYSTEM_DIR}/download"
BUILD_DIR="${SYSTEM_DIR}/linux-build"
ROOTFS_DIR="${SYSTEM_DIR}/rootfs"

VERSION_FILE="${KERNEL_DIR}/version"
CHECKSUM_FILE="${KERNEL_DIR}/SHA256SUMS"

if [[ ! -f "${VERSION_FILE}" ]]; then
    echo "Kernel version file not found: ${VERSION_FILE}" >&2
    exit 1
fi

if [[ ! -f "${CHECKSUM_FILE}" ]]; then
    echo "Kernel checksum file not found: ${CHECKSUM_FILE}" >&2
    exit 1
fi

KERNEL_VERSION="$(tr -d '[:space:]' < "${VERSION_FILE}")"

if [[ ! "${KERNEL_VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid kernel version: ${KERNEL_VERSION}" >&2
    exit 1
fi

KERNEL_MAJOR="${KERNEL_VERSION%%.*}"
ARCHIVE_NAME="linux-${KERNEL_VERSION}.tar.xz"
ARCHIVE_PATH="${DOWNLOAD_DIR}/${ARCHIVE_NAME}"
SOURCE_DIR="${SYSTEM_DIR}/linux-${KERNEL_VERSION}"
DOWNLOAD_URL="https://cdn.kernel.org/pub/linux/kernel/v${KERNEL_MAJOR}.x/${ARCHIVE_NAME}"

for command_name in make sha256sum tar; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command is not installed: ${command_name}" >&2
        exit 1
    fi
done

mkdir -p "${DOWNLOAD_DIR}" "${BUILD_DIR}" "${ROOTFS_DIR}"

if [[ ! -f "${ARCHIVE_PATH}" ]]; then
    if ! command -v curl >/dev/null 2>&1; then
        echo "Required command is not installed: curl" >&2
        exit 1
    fi

    echo "Downloading Linux ${KERNEL_VERSION}..."
    curl --fail --location \
        --output "${ARCHIVE_PATH}.part" \
        "${DOWNLOAD_URL}"
    mv "${ARCHIVE_PATH}.part" "${ARCHIVE_PATH}"
else
    echo "Using existing archive: ${ARCHIVE_PATH}"
fi

echo "Verifying ${ARCHIVE_NAME}..."
(
    cd "${DOWNLOAD_DIR}"
    sha256sum --check "${CHECKSUM_FILE}"
)

if [[ -d "${SOURCE_DIR}" ]]; then
    if [[ ! -f "${SOURCE_DIR}/Makefile" ]]; then
        echo "Existing kernel source directory is incomplete: ${SOURCE_DIR}" >&2
        exit 1
    fi

    ACTUAL_VERSION="$(make --silent --directory "${SOURCE_DIR}" kernelversion)"
    if [[ "${ACTUAL_VERSION}" != "${KERNEL_VERSION}" ]]; then
        echo "Unexpected kernel source version: ${ACTUAL_VERSION}" >&2
        echo "Expected: ${KERNEL_VERSION}" >&2
        exit 1
    fi

    echo "Kernel sources already exist: ${SOURCE_DIR}"
else
    echo "Extracting Linux ${KERNEL_VERSION}..."
    tar --extract --file "${ARCHIVE_PATH}" --directory "${SYSTEM_DIR}"
fi

echo "Linux ${KERNEL_VERSION} sources are ready."

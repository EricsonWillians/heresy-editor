#!/usr/bin/env bash

# Create a reproducible, portable Linux archive from a configured build tree.

set -Eeuo pipefail
IFS=$'\n\t'

if (( $# != 3 )); then
    printf 'Usage: %s BUILD_DIRECTORY OUTPUT_DIRECTORY ARCHITECTURE\n' "${0##*/}" >&2
    exit 2
fi

SOURCE_DIRECTORY="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly SOURCE_DIRECTORY
BUILD_DIRECTORY="$(cd -- "$1" && pwd -P)"
readonly BUILD_DIRECTORY
mkdir -p -- "$2"
OUTPUT_DIRECTORY="$(cd -- "$2" && pwd -P)"
readonly OUTPUT_DIRECTORY
readonly ARCHITECTURE="$3"

[[ "$ARCHITECTURE" =~ ^[A-Za-z0-9_.-]+$ ]] || {
    printf 'Invalid architecture: %s\n' "$ARCHITECTURE" >&2
    exit 2
}

readonly EXECUTABLE="${BUILD_DIRECTORY}/heresy"
readonly CACHE="${BUILD_DIRECTORY}/CMakeCache.txt"
[[ -x "$EXECUTABLE" ]] || {
    printf 'Heresy Editor executable not found: %s\n' "$EXECUTABLE" >&2
    exit 1
}
[[ -f "$CACHE" ]] || {
    printf 'CMake cache not found: %s\n' "$CACHE" >&2
    exit 1
}

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$CACHE")"
[[ "$VERSION" =~ ^[0-9]+([.][0-9]+)*$ ]] || {
    printf 'Could not determine a valid project version from %s\n' "$CACHE" >&2
    exit 1
}
readonly VERSION

readonly PACKAGE_NAME="Heresy-Editor-${VERSION}-linux-${ARCHITECTURE}"
readonly ARCHIVE="${OUTPUT_DIRECTORY}/${PACKAGE_NAME}.tar.gz"
TEMP_DIRECTORY="$(mktemp -d "${OUTPUT_DIRECTORY}/.heresy-package.XXXXXX")"
readonly TEMP_DIRECTORY
trap 'rm -rf -- "$TEMP_DIRECTORY"' EXIT
readonly PACKAGE_ROOT="${TEMP_DIRECTORY}/${PACKAGE_NAME}"

mkdir -p -- "$PACKAGE_ROOT"
install -m 0755 -- "$EXECUTABLE" "${PACKAGE_ROOT}/heresy"
cp -R -- \
    "${SOURCE_DIRECTORY}/common" \
    "${SOURCE_DIRECTORY}/games" \
    "${SOURCE_DIRECTORY}/ports" \
    "$PACKAGE_ROOT/"
install -m 0644 -- \
    "${SOURCE_DIRECTORY}/bindings.cfg" \
    "${SOURCE_DIRECTORY}/defaults.cfg" \
    "${SOURCE_DIRECTORY}/operations.cfg" \
    "$PACKAGE_ROOT/"
install -m 0644 -- "${SOURCE_DIRECTORY}/misc/about_logo.png" "${PACKAGE_ROOT}/common/"
install -m 0644 -- \
    "${SOURCE_DIRECTORY}/AUTHORS.md" \
    "${SOURCE_DIRECTORY}/GPL.txt" \
    "${SOURCE_DIRECTORY}/INSTALL.txt" \
    "${SOURCE_DIRECTORY}/README.txt" \
    "$PACKAGE_ROOT/"

SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$SOURCE_DIRECTORY" log -1 --format=%ct)}"
readonly SOURCE_DATE_EPOCH
tar --sort=name \
    --mtime="@${SOURCE_DATE_EPOCH}" \
    --owner=0 --group=0 --numeric-owner \
    -C "$TEMP_DIRECTORY" -cf - "$PACKAGE_NAME" | gzip -9 -n > "$ARCHIVE"

(
    cd -- "$OUTPUT_DIRECTORY"
    sha256sum "${PACKAGE_NAME}.tar.gz" > "${PACKAGE_NAME}.tar.gz.sha256"
)

printf '%s\n' "$ARCHIVE"

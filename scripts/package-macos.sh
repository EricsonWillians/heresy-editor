#!/usr/bin/env bash

# Create a distributable macOS disk image from a configured build tree.

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

readonly APP_BUNDLE="${BUILD_DIRECTORY}/heresy.app"
readonly CACHE="${BUILD_DIRECTORY}/CMakeCache.txt"
[[ -d "$APP_BUNDLE" ]] || {
    printf 'Heresy Editor application bundle not found: %s\n' "$APP_BUNDLE" >&2
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

readonly PACKAGE_NAME="Heresy-Editor-${VERSION}-macos-${ARCHITECTURE}"
readonly ARCHIVE="${OUTPUT_DIRECTORY}/${PACKAGE_NAME}.dmg"
TEMP_DIRECTORY="$(mktemp -d "${OUTPUT_DIRECTORY}/.heresy-package.XXXXXX")"
readonly TEMP_DIRECTORY
trap 'rm -rf -- "$TEMP_DIRECTORY"' EXIT
readonly IMAGE_ROOT="${TEMP_DIRECTORY}/Heresy Editor"

mkdir -p -- "$IMAGE_ROOT"
cp -R -- "$APP_BUNDLE" "$IMAGE_ROOT/"
install -m 0644 -- \
    "${SOURCE_DIRECTORY}/AUTHORS.md" \
    "${SOURCE_DIRECTORY}/GPL.txt" \
    "${SOURCE_DIRECTORY}/INSTALL.txt" \
    "${SOURCE_DIRECTORY}/README.txt" \
    "$IMAGE_ROOT/"
ln -s /Applications "${IMAGE_ROOT}/Applications"

hdiutil create \
    -volname "Heresy Editor" \
    -srcfolder "$IMAGE_ROOT" \
    -format UDZO \
    -ov \
    "$ARCHIVE" >/dev/null

(
    cd -- "$OUTPUT_DIRECTORY"
    shasum -a 256 "${PACKAGE_NAME}.dmg" > "${PACKAGE_NAME}.dmg.sha256"
)

printf '%s\n' "$ARCHIVE"

#!/usr/bin/env bash

# Heresy Editor build driver.
#
# This script provides a consistent CMake workflow for local development and
# CI-like builds. It intentionally does not install packages or invoke sudo.

# Bash 3.2 remains the default shell on older supported macOS releases. Avoid
# nounset here because that version treats declared empty arrays as unbound.
set -Ee -o pipefail
IFS=$'\n\t'

readonly SCRIPT_NAME="${0##*/}"
readonly MINIMUM_CMAKE_VERSION="3.28"
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly SOURCE_DIR

BUILD_DIR="${SOURCE_DIR}/build"
BUILD_TYPE="Release"
BUILD_TYPE_EXPLICIT=0
GENERATOR=""
JOBS="auto"

DO_CONFIGURE=1
DO_BUILD=1
DO_CLEAN=0
DO_TEST=0
DO_INSTALL=0
DRY_RUN=0
VERBOSE=0
COLOR_ENABLED=1

ENABLE_TESTS="ON"
ENABLE_OPENGL="ON"
USE_SYSTEM_FLTK="OFF"
USE_SYSTEM_GTEST="OFF"
INSTALL_PREFIX=""
CCACHE_ENABLED=0
CC_OVERRIDE=""
CXX_OVERRIDE=""
MODE="default"
HAS_CONFIGURE_ONLY_OPTIONS=0

TARGETS=()
CMAKE_ARGS=()
CTEST_ARGS=()

if [[ ! -t 1 || -n "${NO_COLOR:-}" || "${TERM:-}" == "dumb" ]]; then
    COLOR_ENABLED=0
fi

if (( COLOR_ENABLED )); then
    COLOR_BLUE=$'\033[1;34m'
    COLOR_GREEN=$'\033[1;32m'
    COLOR_YELLOW=$'\033[1;33m'
    COLOR_RED=$'\033[1;31m'
    COLOR_RESET=$'\033[0m'
else
    COLOR_BLUE=""
    COLOR_GREEN=""
    COLOR_YELLOW=""
    COLOR_RED=""
    COLOR_RESET=""
fi

info() {
    printf '%s[INFO]%s %s\n' "$COLOR_BLUE" "$COLOR_RESET" "$*"
}

success() {
    printf '%s[OK]%s %s\n' "$COLOR_GREEN" "$COLOR_RESET" "$*"
}

warn() {
    printf '%s[WARN]%s %s\n' "$COLOR_YELLOW" "$COLOR_RESET" "$*" >&2
}

die() {
    printf '%s[ERROR]%s %s\n' "$COLOR_RED" "$COLOR_RESET" "$*" >&2
    exit 1
}

usage() {
    cat <<EOF
Heresy Editor build script

Usage:
  ./${SCRIPT_NAME} [options] [-- <additional CMake arguments>]

Core options:
  -h, --help                  Show this help message
  -b, --build-dir DIR         Build directory (default: build)
  -t, --type TYPE             Debug, Release, RelWithDebInfo, or MinSizeRel
                              (default: Release)
  -G, --generator NAME        CMake generator; fresh builds prefer Ninja when
                              it is available
  -j, --jobs N                Parallel jobs, or "auto" (default: auto)
      --target TARGET         Build only TARGET; may be specified repeatedly
      --clean                 Safely remove the CMake build directory first
      --configure-only        Configure without compiling
      --build-only            Compile an already-configured build directory
      --dry-run               Print commands without executing them
  -v, --verbose               Enable verbose CMake, compiler, and test output
      --no-color              Disable colored status output

Project options:
      --tests                 Build unit tests (default)
      --no-tests              Do not build unit tests
      --test                  Run CTest after a successful build
      --opengl                Enable OpenGL rendering (default)
      --no-opengl             Build with the software renderer only
      --bundled-fltk          Fetch the project-pinned FLTK (default)
      --system-fltk           Use an installed FLTK package
      --bundled-gtest         Fetch the project-pinned GoogleTest (default)
      --system-gtest          Use an installed GoogleTest package

Toolchain and installation:
      --cc COMPILER           C compiler passed to CMake
      --cxx COMPILER          C++ compiler passed to CMake
      --ccache                Use ccache as the compiler launcher
      --install               Install after building; defaults to
                              BUILD_DIR/install for a safe local install
      --install-prefix DIR    Configure a custom installation prefix
      --cmake-arg ARG         Additional CMake configure argument; repeatable
      --ctest-arg ARG         Additional CTest argument; repeatable

Examples:
  ./${SCRIPT_NAME}
  ./${SCRIPT_NAME} --type Debug --test
  ./${SCRIPT_NAME} --clean --no-tests --no-opengl
  ./${SCRIPT_NAME} --system-fltk --system-gtest --test
  ./${SCRIPT_NAME} --target heresy --jobs 8
  ./${SCRIPT_NAME} --install --install-prefix ./build/stage
  ./${SCRIPT_NAME} -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

Relative paths are resolved from the repository root, so this script can be
invoked from any working directory. Dependencies must already be installed;
the script never invokes a package manager or sudo.
EOF
}

require_value() {
    local option="$1"
    local value="${2-}"

    [[ -n "$value" ]] || die "${option} requires a value."
}

normalize_build_type() {
    case "$1" in
        Debug|debug)
            printf 'Debug\n'
            ;;
        Release|release)
            printf 'Release\n'
            ;;
        RelWithDebInfo|relwithdebinfo)
            printf 'RelWithDebInfo\n'
            ;;
        MinSizeRel|minsizerel)
            printf 'MinSizeRel\n'
            ;;
        *)
            die "Unsupported build type '$1'. Use Debug, Release, RelWithDebInfo, or MinSizeRel."
            ;;
    esac
}

set_mode() {
    local requested_mode="$1"

    if [[ "$MODE" != "default" && "$MODE" != "$requested_mode" ]]; then
        die "--configure-only and --build-only cannot be used together."
    fi

    MODE="$requested_mode"
    if [[ "$requested_mode" == "configure" ]]; then
        DO_CONFIGURE=1
        DO_BUILD=0
    else
        DO_CONFIGURE=0
        DO_BUILD=1
    fi
}

while (( $# > 0 )); do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -b|--build-dir)
            require_value "$1" "${2-}"
            BUILD_DIR="$2"
            shift 2
            ;;
        --build-dir=*)
            BUILD_DIR="${1#*=}"
            require_value "--build-dir" "$BUILD_DIR"
            shift
            ;;
        -t|--type)
            require_value "$1" "${2-}"
            BUILD_TYPE="$(normalize_build_type "$2")"
            BUILD_TYPE_EXPLICIT=1
            shift 2
            ;;
        --type=*)
            BUILD_TYPE="$(normalize_build_type "${1#*=}")"
            BUILD_TYPE_EXPLICIT=1
            shift
            ;;
        -G|--generator)
            require_value "$1" "${2-}"
            GENERATOR="$2"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift 2
            ;;
        --generator=*)
            GENERATOR="${1#*=}"
            require_value "--generator" "$GENERATOR"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        -j|--jobs)
            require_value "$1" "${2-}"
            JOBS="$2"
            shift 2
            ;;
        --jobs=*)
            JOBS="${1#*=}"
            require_value "--jobs" "$JOBS"
            shift
            ;;
        --target)
            require_value "$1" "${2-}"
            TARGETS+=("$2")
            shift 2
            ;;
        --target=*)
            TARGETS+=("${1#*=}")
            require_value "--target" "${TARGETS[${#TARGETS[@]}-1]}"
            shift
            ;;
        --clean)
            DO_CLEAN=1
            shift
            ;;
        --configure-only)
            set_mode "configure"
            shift
            ;;
        --build-only)
            set_mode "build"
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        --no-color)
            COLOR_ENABLED=0
            shift
            ;;
        --tests)
            ENABLE_TESTS="ON"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --no-tests)
            ENABLE_TESTS="OFF"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --test)
            DO_TEST=1
            shift
            ;;
        --opengl)
            ENABLE_OPENGL="ON"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --no-opengl)
            ENABLE_OPENGL="OFF"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --bundled-fltk)
            USE_SYSTEM_FLTK="OFF"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --system-fltk)
            USE_SYSTEM_FLTK="ON"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --bundled-gtest)
            USE_SYSTEM_GTEST="OFF"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --system-gtest)
            USE_SYSTEM_GTEST="ON"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --cc)
            require_value "$1" "${2-}"
            CC_OVERRIDE="$2"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift 2
            ;;
        --cc=*)
            CC_OVERRIDE="${1#*=}"
            require_value "--cc" "$CC_OVERRIDE"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --cxx)
            require_value "$1" "${2-}"
            CXX_OVERRIDE="$2"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift 2
            ;;
        --cxx=*)
            CXX_OVERRIDE="${1#*=}"
            require_value "--cxx" "$CXX_OVERRIDE"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --ccache)
            CCACHE_ENABLED=1
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --install)
            DO_INSTALL=1
            shift
            ;;
        --install-prefix)
            require_value "$1" "${2-}"
            INSTALL_PREFIX="$2"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift 2
            ;;
        --install-prefix=*)
            INSTALL_PREFIX="${1#*=}"
            require_value "--install-prefix" "$INSTALL_PREFIX"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --cmake-arg)
            require_value "$1" "${2-}"
            CMAKE_ARGS+=("$2")
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift 2
            ;;
        --cmake-arg=*)
            CMAKE_ARGS+=("${1#*=}")
            require_value "--cmake-arg" "${CMAKE_ARGS[${#CMAKE_ARGS[@]}-1]}"
            HAS_CONFIGURE_ONLY_OPTIONS=1
            shift
            ;;
        --ctest-arg)
            require_value "$1" "${2-}"
            CTEST_ARGS+=("$2")
            shift 2
            ;;
        --ctest-arg=*)
            CTEST_ARGS+=("${1#*=}")
            require_value "--ctest-arg" "${CTEST_ARGS[${#CTEST_ARGS[@]}-1]}"
            shift
            ;;
        --)
            shift
            while (( $# > 0 )); do
                CMAKE_ARGS+=("$1")
                HAS_CONFIGURE_ONLY_OPTIONS=1
                shift
            done
            ;;
        -* )
            die "Unknown option '$1'. Run ./${SCRIPT_NAME} --help for usage."
            ;;
        *)
            die "Unexpected positional argument '$1'. Use --target for build targets."
            ;;
    esac
done

# --no-color is parsed after the initial color setup, so clear the sequences
# before emitting any status messages.
if (( ! COLOR_ENABLED )); then
    COLOR_BLUE=""
    COLOR_GREEN=""
    COLOR_YELLOW=""
    COLOR_RED=""
    COLOR_RESET=""
fi

case "$BUILD_DIR" in
    /*|[A-Za-z]:/*)
        ;;
    *)
        BUILD_DIR="${SOURCE_DIR}/${BUILD_DIR}"
        ;;
esac

if [[ "$INSTALL_PREFIX" != "" ]]; then
    case "$INSTALL_PREFIX" in
        /*|[A-Za-z]:/*)
            ;;
        *)
            INSTALL_PREFIX="${SOURCE_DIR}/${INSTALL_PREFIX}"
            ;;
    esac
fi

if [[ "$JOBS" != "auto" && ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    die "--jobs must be a positive integer or 'auto'."
fi

if (( DO_CLEAN )) && (( ! DO_CONFIGURE )); then
    die "--clean cannot be combined with --build-only."
fi

if (( DO_TEST )) && (( ! DO_BUILD )); then
    die "--test requires a build; it cannot be combined with --configure-only."
fi

if (( ${#CTEST_ARGS[@]} > 0 )) && (( ! DO_TEST )); then
    die "--ctest-arg requires --test."
fi

if (( ${#TARGETS[@]} > 0 )) && (( ! DO_BUILD )); then
    die "--target requires a build; it cannot be combined with --configure-only."
fi

if (( DO_TEST )) && [[ "$ENABLE_TESTS" == "OFF" ]]; then
    die "--test cannot be combined with --no-tests."
fi

if [[ "$USE_SYSTEM_GTEST" == "ON" && "$ENABLE_TESTS" == "OFF" ]]; then
    die "--system-gtest has no effect with --no-tests."
fi

if (( DO_INSTALL )) && (( ! DO_BUILD )); then
    die "--install requires a build; it cannot be combined with --configure-only."
fi

if (( DO_INSTALL )) && (( ! DO_CONFIGURE )); then
    die "--install requires configuration so the installation prefix is deterministic."
fi

if (( ! DO_CONFIGURE )) && (( HAS_CONFIGURE_ONLY_OPTIONS )); then
    die "Configuration options cannot be used with --build-only."
fi

command -v cmake >/dev/null 2>&1 || die "CMake ${MINIMUM_CMAKE_VERSION} or newer is required."

CMAKE_VERSION="$(cmake --version | sed -n '1s/^cmake version //p')"
if [[ "$CMAKE_VERSION" =~ ^([0-9]+)\.([0-9]+) ]]; then
    CMAKE_MAJOR=$((10#${BASH_REMATCH[1]}))
    CMAKE_MINOR=$((10#${BASH_REMATCH[2]}))
else
    die "Unable to determine the installed CMake version."
fi

if (( CMAKE_MAJOR < 3 || (CMAKE_MAJOR == 3 && CMAKE_MINOR < 28) )); then
    die "CMake ${MINIMUM_CMAKE_VERSION} or newer is required; found ${CMAKE_VERSION}."
fi

detect_jobs() {
    local detected=""

    if command -v getconf >/dev/null 2>&1; then
        detected="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi
    if [[ ! "$detected" =~ ^[1-9][0-9]*$ ]] && command -v sysctl >/dev/null 2>&1; then
        detected="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    fi
    if [[ ! "$detected" =~ ^[1-9][0-9]*$ ]]; then
        detected=1
    fi

    printf '%s\n' "$detected"
}

if [[ "$JOBS" == "auto" ]]; then
    JOBS="$(detect_jobs)"
fi

validate_tool() {
    local label="$1"
    local tool="$2"

    if [[ "$tool" == */* ]]; then
        [[ -x "$tool" ]] || die "${label} '$tool' does not exist or is not executable."
    else
        command -v "$tool" >/dev/null 2>&1 || die "${label} '$tool' was not found in PATH."
    fi
}

if [[ -n "$CC_OVERRIDE" ]]; then
    validate_tool "C compiler" "$CC_OVERRIDE"
fi
if [[ -n "$CXX_OVERRIDE" ]]; then
    validate_tool "C++ compiler" "$CXX_OVERRIDE"
fi
if (( CCACHE_ENABLED )); then
    command -v ccache >/dev/null 2>&1 || die "--ccache was requested, but ccache was not found in PATH."
fi

cmake_definition_was_provided() {
    local definition="$1"
    local argument=""

    for argument in "${CMAKE_ARGS[@]}"; do
        case "$argument" in
            "-D${definition}="*|"-D${definition}:"*"="*)
                return 0
                ;;
        esac
    done

    return 1
}

check_linux_xpm_headers() {
    local probe_compiler="${CXX_OVERRIDE:-${CXX:-c++}}"

    [[ "$(uname -s)" == "Linux" ]] || return 0
    (( DO_CONFIGURE )) || return 0
    (( ! DRY_RUN )) || return 0
    cmake_definition_was_provided "X11_Xpm_INCLUDE_PATH" && return 0

    if ! printf '#include <X11/xpm.h>\n' | "$probe_compiler" -E -x c++ - >/dev/null 2>&1; then
        die "Xpm development headers are required. On Debian/Ubuntu, install libxpm-dev; on other Linux distributions, install the X11/Xpm development package."
    fi
}

check_linux_xpm_headers

print_command() {
    local argument=""
    local quoted=""

    printf '  '
    for argument in "$@"; do
        printf -v quoted '%q' "$argument"
        printf '%s ' "$quoted"
    done
    printf '\n'
}

run() {
    print_command "$@"
    if (( ! DRY_RUN )); then
        "$@"
    fi
}

clean_build_directory() {
    local resolved_build_dir=""
    local existing_entry=""

    [[ -n "$BUILD_DIR" ]] || die "Refusing to clean an empty build-directory path."
    [[ ! -L "$BUILD_DIR" ]] || die "Refusing to clean symlinked build directory '$BUILD_DIR'."

    if [[ ! -d "$BUILD_DIR" ]]; then
        info "Build directory does not exist; nothing to clean."
        return
    fi

    resolved_build_dir="$(cd -- "$BUILD_DIR" && pwd -P)"
    [[ "$resolved_build_dir" != "/" ]] || die "Refusing to clean the filesystem root."
    [[ "$resolved_build_dir" != "$SOURCE_DIR" ]] || die "Refusing to clean the source directory."

    case "${SOURCE_DIR}/" in
        "${resolved_build_dir}/"*)
            die "Refusing to clean '$resolved_build_dir' because it contains the source directory."
            ;;
    esac

    # Only the emptiness of the directory matters here; filenames are not parsed.
    existing_entry="$(ls -A "$resolved_build_dir" 2>/dev/null || true)"
    if [[ -n "$existing_entry" &&
          ! -f "${resolved_build_dir}/CMakeCache.txt" &&
          ! -f "${resolved_build_dir}/.heresy-editor-build" ]]; then
        die "Refusing to clean a non-empty directory that is not marked as a Heresy Editor CMake build: $resolved_build_dir"
    fi

    info "Cleaning build directory: $resolved_build_dir"
    run cmake -E remove_directory "$resolved_build_dir"
}

validate_build_directory() {
    local resolved_build_dir=""

    [[ -n "$BUILD_DIR" ]] || die "Build-directory path cannot be empty."

    if [[ -d "$BUILD_DIR" ]]; then
        resolved_build_dir="$(cd -- "$BUILD_DIR" && pwd -P)"
        [[ "$resolved_build_dir" != "/" ]] || die "The filesystem root cannot be used as a build directory."
        [[ "$resolved_build_dir" != "$SOURCE_DIR" ]] || die "In-source builds are not supported."

        case "${SOURCE_DIR}/" in
            "${resolved_build_dir}/"*)
                die "The build directory cannot contain the source directory: $resolved_build_dir"
                ;;
        esac
    fi
}

validate_build_directory

if (( DO_CLEAN )); then
    clean_build_directory
fi

if (( DO_INSTALL )) && [[ -z "$INSTALL_PREFIX" ]]; then
    INSTALL_PREFIX="${BUILD_DIR}/install"
fi

if (( ! DO_CONFIGURE )) && [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]] && (( ! DRY_RUN )); then
    die "Build directory is not configured: ${BUILD_DIR}"
fi

read_cache_value() {
    local key="$1"
    sed -n "s/^${key}:[^=]*=//p" "${BUILD_DIR}/CMakeCache.txt" | head -n 1
}

if (( ! DO_CONFIGURE )) && [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    if (( ! BUILD_TYPE_EXPLICIT )); then
        CACHED_BUILD_TYPE="$(read_cache_value CMAKE_BUILD_TYPE)"
        if [[ -n "$CACHED_BUILD_TYPE" ]]; then
            BUILD_TYPE="$CACHED_BUILD_TYPE"
        fi
    fi

    CACHED_VALUE="$(read_cache_value ENABLE_OPENGL)"
    [[ -z "$CACHED_VALUE" ]] || ENABLE_OPENGL="$CACHED_VALUE"
    CACHED_VALUE="$(read_cache_value ENABLE_UNIT_TESTS)"
    [[ -z "$CACHED_VALUE" ]] || ENABLE_TESTS="$CACHED_VALUE"
    CACHED_VALUE="$(read_cache_value USE_SYSTEM_FLTK)"
    [[ -z "$CACHED_VALUE" ]] || USE_SYSTEM_FLTK="$CACHED_VALUE"
    CACHED_VALUE="$(read_cache_value USE_SYSTEM_GOOGLE_TEST)"
    [[ -z "$CACHED_VALUE" ]] || USE_SYSTEM_GTEST="$CACHED_VALUE"
fi

if [[ -z "$GENERATOR" ]]; then
    if (( ! DO_CLEAN )) && [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n 1)"
    elif (( DO_CONFIGURE )) && command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    fi
fi

case "$GENERATOR" in
    Ninja|"Ninja Multi-Config")
        command -v ninja >/dev/null 2>&1 || die "The '${GENERATOR}' generator requires ninja in PATH."
        ;;
esac

printf '\n%sHeresy Editor build%s\n' "$COLOR_BLUE" "$COLOR_RESET"
printf '  Source:       %s\n' "$SOURCE_DIR"
printf '  Build:        %s\n' "$BUILD_DIR"
printf '  Type:         %s\n' "$BUILD_TYPE"
printf '  Generator:    %s\n' "${GENERATOR:-CMake default}"
printf '  Jobs:         %s\n' "$JOBS"
printf '  OpenGL:       %s\n' "$ENABLE_OPENGL"
printf '  Unit tests:   %s\n' "$ENABLE_TESTS"
printf '  FLTK:         %s\n' "$([[ "$USE_SYSTEM_FLTK" == "ON" ]] && printf 'system' || printf 'bundled')"
if [[ "$ENABLE_TESTS" == "ON" ]]; then
    printf '  GoogleTest:   %s\n' "$([[ "$USE_SYSTEM_GTEST" == "ON" ]] && printf 'system' || printf 'bundled')"
fi
if [[ -n "$INSTALL_PREFIX" ]]; then
    printf '  Install:      %s\n' "$INSTALL_PREFIX"
fi
if (( DRY_RUN )); then
    printf '  Mode:         dry run\n'
fi
printf '\n'

START_SECONDS=$SECONDS

if (( DO_CONFIGURE )); then
    info "Preparing build directory"
    run cmake -E make_directory "$BUILD_DIR"
    run cmake -E touch "${BUILD_DIR}/.heresy-editor-build"

    CONFIGURE_COMMAND=(
        cmake
        -S "$SOURCE_DIR"
        -B "$BUILD_DIR"
    )

    if [[ -n "$GENERATOR" ]]; then
        CONFIGURE_COMMAND+=(-G "$GENERATOR")
    fi

    CONFIGURE_COMMAND+=(
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DENABLE_OPENGL=${ENABLE_OPENGL}"
        "-DENABLE_UNIT_TESTS=${ENABLE_TESTS}"
        "-DUSE_SYSTEM_FLTK=${USE_SYSTEM_FLTK}"
    )

    if [[ "$ENABLE_TESTS" == "ON" ]]; then
        CONFIGURE_COMMAND+=("-DUSE_SYSTEM_GOOGLE_TEST=${USE_SYSTEM_GTEST}")
    fi
    if [[ -n "$INSTALL_PREFIX" ]]; then
        CONFIGURE_COMMAND+=("-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}")
    fi
    if [[ -n "$CC_OVERRIDE" ]]; then
        CONFIGURE_COMMAND+=("-DCMAKE_C_COMPILER=${CC_OVERRIDE}")
    fi
    if [[ -n "$CXX_OVERRIDE" ]]; then
        CONFIGURE_COMMAND+=("-DCMAKE_CXX_COMPILER=${CXX_OVERRIDE}")
    fi
    if (( CCACHE_ENABLED )); then
        CONFIGURE_COMMAND+=(
            "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
        )
    fi
    if (( VERBOSE )); then
        CONFIGURE_COMMAND+=(--log-level=VERBOSE)
    fi
    if (( ${#CMAKE_ARGS[@]} > 0 )); then
        CONFIGURE_COMMAND+=("${CMAKE_ARGS[@]}")
    fi

    info "Configuring CMake"
    run "${CONFIGURE_COMMAND[@]}"
fi

if (( DO_BUILD )); then
    BUILD_COMMAND=(
        cmake --build "$BUILD_DIR"
        --config "$BUILD_TYPE"
        --parallel "$JOBS"
    )

    if (( ${#TARGETS[@]} > 0 )); then
        BUILD_COMMAND+=(--target "${TARGETS[@]}")
    fi
    if (( VERBOSE )); then
        BUILD_COMMAND+=(--verbose)
    fi

    info "Compiling Heresy Editor"
    run "${BUILD_COMMAND[@]}"
fi

if (( DO_TEST )); then
    TEST_COMMAND=(
        ctest
        --test-dir "$BUILD_DIR"
        --build-config "$BUILD_TYPE"
        --parallel "$JOBS"
        --output-on-failure
    )

    if (( VERBOSE )); then
        TEST_COMMAND+=(--verbose)
    fi
    if (( ${#CTEST_ARGS[@]} > 0 )); then
        TEST_COMMAND+=("${CTEST_ARGS[@]}")
    fi

    info "Running tests"
    run "${TEST_COMMAND[@]}"
fi

if (( DO_INSTALL )); then
    INSTALL_COMMAND=(
        cmake --install "$BUILD_DIR"
        --config "$BUILD_TYPE"
    )

    info "Installing Heresy Editor"
    run "${INSTALL_COMMAND[@]}"
fi

ELAPSED_SECONDS=$((SECONDS - START_SECONDS))
if (( DRY_RUN )); then
    success "Dry run completed."
elif (( DO_BUILD )); then
    success "Build workflow completed in ${ELAPSED_SECONDS}s."
else
    success "Configuration completed in ${ELAPSED_SECONDS}s."
fi

#!/usr/bin/env bash
set -euo pipefail

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
root_dir="$( cd "${script_dir}/.." && pwd )"

usage() {
    cat >&2 <<'EOF'
Usage:
  IDASDK=/path/to/ida-sdk scripts/build-ida.sh [options]
  scripts/build-ida.sh --sdk /path/to/ida-sdk [options]

Options:
  --sdk PATH          IDA SDK checkout or source directory
  --qt-prefix PATH    Qt prefix containing lib/cmake/Qt6/Qt6Config.cmake
  --build-dir PATH    Build directory (default: ./build-ida)
  --stage-dir PATH    Staging root (default: <build>/stage)
  --generator NAME    CMake generator (default: Ninja)
  --cmake-arg ARG     Extra argument passed through to CMake configure
  -h, --help          Show this help text

Environment:
  IDASDK, CANTORDUST_QT_PREFIX, CANTORDUST_BUILD_DIR, IDABIN,
  CANTORDUST_CMAKE_GENERATOR and CANTORDUST_CMAKE_ARGS are also respected.
EOF
}

sdk_dir="${IDASDK:-}"
qt_prefix="${CANTORDUST_QT_PREFIX:-}"
build_dir="${CANTORDUST_BUILD_DIR:-}"
stage_dir="${IDABIN:-}"
generator="${CANTORDUST_CMAKE_GENERATOR:-Ninja}"
extra_args=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sdk)
            sdk_dir="$2"; shift 2 ;;
        --qt-prefix)
            qt_prefix="$2"; shift 2 ;;
        --build-dir)
            build_dir="$2"; shift 2 ;;
        --stage-dir)
            stage_dir="$2"; shift 2 ;;
        --generator)
            generator="$2"; shift 2 ;;
        --cmake-arg)
            extra_args+=( "$2" ); shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do extra_args+=( "$1" ); shift; done
            break ;;
        -*)
            echo "Unknown option: $1" >&2
            usage
            exit 2 ;;
        *)
            if [[ -z "$sdk_dir" ]]; then
                sdk_dir="$1"
                shift
            else
                extra_args+=( "$1" )
                shift
            fi ;;
    esac
done

if [[ -z "$sdk_dir" ]]; then
    usage
    exit 2
fi

if [[ ! -f "${sdk_dir}/src/include/pro.h" &&
      ! -f "${sdk_dir}/include/pro.h" ]]; then
    echo "Invalid IDA SDK directory: ${sdk_dir}" >&2
    exit 2
fi

if [[ -z "$build_dir" ]]; then
    build_dir="${root_dir}/build-ida"
fi
if [[ -z "$stage_dir" ]]; then
    stage_dir="${build_dir}/stage"
fi

args=(
    -S "$root_dir"
    -B "$build_dir"
    -G "$generator"
    -DCMAKE_BUILD_TYPE=Release
    -DCANTORDUST_BUILD_IDA_PLUGIN=ON
    -DIDASDK="$sdk_dir"
    -DIDABIN="$stage_dir"
)

if [[ -n "$qt_prefix" ]]; then
    args+=( -DCMAKE_PREFIX_PATH="$qt_prefix" )
fi

if [[ -n "${CANTORDUST_CMAKE_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    env_extra=( ${CANTORDUST_CMAKE_ARGS} )
    extra_args+=( "${env_extra[@]}" )
fi

args+=( "${extra_args[@]}" )

cmake "${args[@]}"
cmake --build "$build_dir" --target cantor_dust
printf '\nNative IDA plugin build artifacts under: %s/plugins\n' "$stage_dir"

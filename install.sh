#!/bin/sh

set -eu

usage() {
    cat <<'EOF'
Usage: ./install.sh [--prefix PATH] [--uninstall]

Build and install lowtask under /usr/local by default. Use sudo for a
system-wide installation, or select a writable user prefix such as
$HOME/.local.

Options:
  --prefix PATH  Install under PATH instead of /usr/local
  --uninstall    Remove files installed under the selected prefix
  -h, --help     Show this help
EOF
}

prefix=${PREFIX:-/usr/local}
destdir=${DESTDIR:-}
target=install

while [ "$#" -gt 0 ]; do
    case $1 in
        --prefix)
            [ "$#" -ge 2 ] || {
                printf '%s\n' 'lowtask: --prefix requires a path' >&2
                exit 2
            }
            prefix=$2
            shift 2
            ;;
        --prefix=*)
            prefix=${1#--prefix=}
            shift
            ;;
        --uninstall)
            target=uninstall
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'lowtask: unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

project_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)

if [ "$target" = install ]; then
    command -v make >/dev/null 2>&1 || {
        printf '%s\n' 'lowtask: make is required' >&2
        exit 1
    }
    command -v install >/dev/null 2>&1 || {
        printf '%s\n' 'lowtask: install is required' >&2
        exit 1
    }
    make -C "$project_dir" all

    LOWTASK_PROJECT_DIR=$project_dir LOWTASK_PREFIX=$prefix LOWTASK_DESTDIR=$destdir \
        LOWTASK_HOST_OS=$(uname -s) \
        /bin/sh "$project_dir/scripts/install-files.sh" "$target"
    printf 'Installed lowtask to %s%s/bin/lowtask\n' "$destdir" "$prefix"
else
    LOWTASK_PROJECT_DIR=$project_dir LOWTASK_PREFIX=$prefix LOWTASK_DESTDIR=$destdir \
        LOWTASK_HOST_OS=$(uname -s) \
        /bin/sh "$project_dir/scripts/install-files.sh" "$target"
    printf 'Removed lowtask from %s%s\n' "$destdir" "$prefix"
fi

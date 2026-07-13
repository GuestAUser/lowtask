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

case $prefix in
    /*) ;;
    *)
        printf '%s\n' 'lowtask: installation prefix must be an absolute path' >&2
        exit 2
        ;;
esac

case $destdir in
    ''|/*) ;;
    *)
        printf '%s\n' 'lowtask: DESTDIR must be empty or an absolute path' >&2
        exit 2
        ;;
esac

project_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
bin_path=$destdir$prefix/bin
icon_path=$destdir$prefix/share/icons/hicolor/scalable/apps
application_path=$destdir$prefix/share/applications

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
    install -d "$bin_path" "$icon_path" "$application_path"
    install -m 0755 "$project_dir/lowtask" "$bin_path/lowtask"
    install -m 0644 "$project_dir/assets/lowtask-mark.svg" "$icon_path/lowtask.svg"
    install -m 0644 "$project_dir/assets/lowtask.desktop" \
        "$application_path/lowtask.desktop"
    printf 'Installed lowtask to %s%s/bin/lowtask\n' "$destdir" "$prefix"
else
    rm -f -- "$bin_path/lowtask" "$icon_path/lowtask.svg" \
        "$application_path/lowtask.desktop"
    printf 'Removed lowtask from %s%s\n' "$destdir" "$prefix"
fi

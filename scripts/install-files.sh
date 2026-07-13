#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    printf '%s\n' 'usage: install-files.sh MODE' >&2
    exit 2
fi

mode=$1
project_dir=${LOWTASK_PROJECT_DIR-}
prefix=${LOWTASK_PREFIX-}
destdir=${LOWTASK_DESTDIR-}
host_os=${LOWTASK_HOST_OS-}

case $project_dir in
    /*) ;;
    *)
        printf '%s\n' 'install-files.sh: project directory must be an absolute path' >&2
        exit 2
        ;;
esac

case $prefix in
    /*) ;;
    '')
        printf '%s\n' 'install-files.sh: installation prefix must not be empty' >&2
        exit 2
        ;;
    *)
        printf '%s\n' 'install-files.sh: installation prefix must be an absolute path' >&2
        exit 2
        ;;
esac

case $destdir in
    ''|/*) ;;
    *)
        printf '%s\n' 'install-files.sh: DESTDIR must be empty or an absolute path' >&2
        exit 2
        ;;
esac

case $prefix:$destdir in
    */../*|*/..:*|*/..)
        printf '%s\n' 'install-files.sh: PREFIX and DESTDIR must not contain .. components' >&2
        exit 2
        ;;
esac

case $host_os in
    Linux|Darwin) ;;
    *)
        printf 'install-files.sh: unsupported host OS: %s\n' "$host_os" >&2
        exit 2
        ;;
esac

bin_path=$destdir$prefix/bin
icon_path=$destdir$prefix/share/icons/hicolor/scalable/apps
application_path=$destdir$prefix/share/applications

case $mode in
    install)
        command -v install >/dev/null 2>&1 || {
            printf '%s\n' 'install-files.sh: install is required' >&2
            exit 1
        }

        install -d "$bin_path"
        install -m 0755 "$project_dir/lowtask" "$bin_path/lowtask"

        if [ "$host_os" = Linux ]; then
            install -d "$icon_path" "$application_path"
            install -m 0644 "$project_dir/assets/lowtask-mark.svg" "$icon_path/lowtask.svg"
            install -m 0644 "$project_dir/assets/lowtask.desktop" \
                "$application_path/lowtask.desktop"
        fi
        ;;
    uninstall)
        rm -f -- "$bin_path/lowtask"

        if [ "$host_os" = Linux ]; then
            rm -f -- "$icon_path/lowtask.svg" "$application_path/lowtask.desktop"
        fi
        ;;
    *)
        printf 'install-files.sh: unsupported mode: %s\n' "$mode" >&2
        exit 2
        ;;
esac

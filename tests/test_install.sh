#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -P "$(dirname "$0")/.." && pwd)
test_root=$(mktemp -d "${TMPDIR:-/tmp}/lowtask-install.XXXXXX")
trap 'rm -rf "$test_root"' EXIT HUP INT TERM
prefix="$test_root/prefix with spaces"
dumped_icon="$test_root/embedded-icon.svg"

"$project_dir/install.sh" --prefix "$prefix"

test -x "$prefix/bin/lowtask"
test "$(stat -c '%a' "$prefix/bin/lowtask")" = 755
test "$(stat -c '%a' "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg")" = 644
test "$(stat -c '%a' "$prefix/share/applications/lowtask.desktop")" = 644
cmp -s "$project_dir/assets/lowtask-mark.svg" \
    "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg"
cmp -s "$project_dir/assets/lowtask.desktop" \
    "$prefix/share/applications/lowtask.desktop"
objcopy --dump-section ".lowtask.icon=$dumped_icon" "$prefix/bin/lowtask"
cmp -s "$project_dir/assets/lowtask-mark.svg" "$dumped_icon"

"$project_dir/install.sh" --prefix "$prefix" --uninstall

test ! -e "$prefix/bin/lowtask"
test ! -e "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg"
test ! -e "$prefix/share/applications/lowtask.desktop"

printf '%s\n' 'install-test: PASS'

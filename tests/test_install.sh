#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -P "$(dirname "$0")/.." && pwd)
test_root=$(mktemp -d "${TMPDIR:-/tmp}/lowtask-install.XXXXXX")
trap 'rm -rf "$test_root"' EXIT HUP INT TERM
prefix="$test_root/prefix with spaces"
dumped_icon="$test_root/embedded-icon.svg"
timeout_supervisor="$project_dir/build/timeout-supervisor"

fail() {
    printf 'install-test: %s\n' "$1" >&2
    exit 1
}

expect_status() {
    expected_status=$1
    shift

    if "$@"; then
        actual_status=0
    else
        actual_status=$?
    fi

    [ "$actual_status" -eq "$expected_status" ] ||
        fail "expected status $expected_status, got $actual_status: $*"
}

assert_not_running() {
    process_id=$1

    if kill -0 "$process_id" 2>/dev/null; then
        fail "process $process_id survived timeout supervision"
    fi
}

file_mode() {
    stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1"
}

macho_section_field() {
    otool -l "$1" | awk -v section="$2" -v field="$3" '
        $1 == "sectname" && $2 == section { found = 1; next }
        found && $1 == field { print $2; exit }
    '
}

darwin_stage="$test_root/darwin-stage"
darwin_install_plan=$(make -Bn -C "$project_dir" HOST_OS=Darwin \
    "DESTDIR=$darwin_stage" "PREFIX=$prefix" install)

case $darwin_install_plan in
    *objcopy*) fail 'Darwin build plan invokes objcopy' ;;
esac

case $darwin_install_plan in
    *'-sectcreate __TEXT __lowtask_icon'*) ;;
    *) fail 'Darwin build plan does not embed the SVG with ld' ;;
esac

case $darwin_install_plan in
    *'/share/icons/'*|*'/share/applications/'*)
        fail 'Darwin install plan includes Linux launcher assets'
        ;;
esac

darwin_test_plan=$(make -Bn -C "$project_dir" HOST_OS=Darwin test)

case $darwin_test_plan in
    *'timeout --'*) fail 'Darwin test plan invokes GNU timeout' ;;
    *'build/timeout-supervisor'*) ;;
    *) fail 'Darwin test plan has no portable timeout runner' ;;
esac

make -C "$project_dir" build/timeout-supervisor

expect_status 0 "$timeout_supervisor" 1s TERM 1s sh -c 'exit 0'
expect_status 23 "$timeout_supervisor" 1s TERM 1s sh -c 'exit 23'
expect_status 127 "$timeout_supervisor" 1s TERM 1s lowtask-command-that-does-not-exist

non_executable="$test_root/non-executable"
printf '%s\n' '#!/bin/sh' 'exit 0' >"$non_executable"
chmod 0644 "$non_executable"
expect_status 126 "$timeout_supervisor" 1s TERM 1s "$non_executable"
expect_status 125 "$timeout_supervisor" invalid TERM 1s sh -c 'exit 0'

expect_status 124 "$timeout_supervisor" 1s TERM 1s sh -c \
    'trap "exit 0" TERM; while :; do sleep 1; done'

timeout_pids="$test_root/timeout-pids"
expect_status 137 "$timeout_supervisor" 1s TERM 1s sh -c \
    'trap "" TERM; sh -c '\''trap "" TERM; while :; do sleep 1; done'\'' &
     printf "%s %s\n" "$$" "$!" >"$1"; while :; do sleep 1; done' \
    timeout-child "$timeout_pids"
read -r timeout_leader timeout_child <"$timeout_pids"
assert_not_running "$timeout_leader"
assert_not_running "$timeout_child"

interrupt_pids="$test_root/interrupt-pids"
"$timeout_supervisor" 30s TERM 1s sh -c \
    'trap "" TERM; sh -c '\''trap "" TERM; while :; do sleep 1; done'\'' &
     printf "%s %s\n" "$$" "$!" >"$1"; while :; do sleep 1; done' \
    interrupt-child "$interrupt_pids" &
supervisor_pid=$!
sleep 1
kill -TERM "$supervisor_pid"
if wait "$supervisor_pid"; then
    interrupt_status=0
else
    interrupt_status=$?
fi
[ "$interrupt_status" -eq 143 ] ||
    fail "interrupted supervisor returned $interrupt_status instead of 143"
read -r interrupt_leader interrupt_child <"$interrupt_pids"
assert_not_running "$interrupt_leader"
assert_not_running "$interrupt_child"

missing_install_path="$test_root/missing-install-path"
missing_install_build_marker="$test_root/missing-install-build-ran"
missing_install_prefix="$test_root/missing-install-prefix"
mkdir "$missing_install_path"
ln -s "$(command -v dirname)" "$missing_install_path/dirname"
cat > "$missing_install_path/make" <<'EOF'
#!/bin/sh
: > "$MISSING_INSTALL_BUILD_MARKER"
exit 0
EOF
chmod +x "$missing_install_path/make"

if PATH="$missing_install_path" \
   MISSING_INSTALL_BUILD_MARKER="$missing_install_build_marker" \
   "$project_dir/install.sh" --prefix "$missing_install_prefix" \
   > "$test_root/missing-install-public-output" 2>&1; then
    fail 'public installer accepted a PATH without install'
else
    missing_install_status=$?
fi
[ "$missing_install_status" -eq 1 ] ||
    fail "public missing-install check returned $missing_install_status instead of 1"
grep -F 'lowtask: install is required' \
    "$test_root/missing-install-public-output" >/dev/null ||
    fail 'public missing-install diagnostic was not explicit'
test ! -e "$missing_install_build_marker" ||
    fail 'public installer built before checking for install'
test ! -e "$missing_install_prefix/bin/lowtask" ||
    fail 'public installer copied a binary without install'

if PATH="$missing_install_path" \
   LOWTASK_PROJECT_DIR="$project_dir" \
   LOWTASK_PREFIX="$missing_install_prefix" \
   LOWTASK_DESTDIR= \
   LOWTASK_HOST_OS=Linux \
   /bin/sh "$project_dir/scripts/install-files.sh" install \
   > "$test_root/missing-install-helper-output" 2>&1; then
    fail 'install helper accepted a PATH without install'
else
    missing_helper_status=$?
fi
[ "$missing_helper_status" -eq 1 ] ||
    fail "helper missing-install check returned $missing_helper_status instead of 1"
grep -F 'install-files.sh: install is required' \
    "$test_root/missing-install-helper-output" >/dev/null ||
    fail 'helper missing-install diagnostic was not explicit'
test ! -e "$missing_install_prefix/bin/lowtask" ||
    fail 'install helper copied a binary without install'

"$project_dir/install.sh" --prefix "$prefix"

test -x "$prefix/bin/lowtask"
test "$(file_mode "$prefix/bin/lowtask")" = 755

case $(uname -s) in
    Linux)
        test "$(file_mode "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg")" = 644
        test "$(file_mode "$prefix/share/applications/lowtask.desktop")" = 644
        cmp -s "$project_dir/assets/lowtask-mark.svg" \
            "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg"
        cmp -s "$project_dir/assets/lowtask.desktop" \
            "$prefix/share/applications/lowtask.desktop"
        objcopy --dump-section ".lowtask.icon=$dumped_icon" "$prefix/bin/lowtask"
        cmp -s "$project_dir/assets/lowtask-mark.svg" "$dumped_icon"
        ;;
    Darwin)
        test ! -e "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg"
        test ! -e "$prefix/share/applications/lowtask.desktop"
        icon_offset=$(macho_section_field "$prefix/bin/lowtask" __lowtask_icon offset)
        icon_size=$(macho_section_field "$prefix/bin/lowtask" __lowtask_icon size)
        test -n "$icon_offset"
        test -n "$icon_size"
        dd if="$prefix/bin/lowtask" of="$dumped_icon" bs=1 \
            skip="$((icon_offset))" count="$((icon_size))" 2>/dev/null
        cmp -s "$project_dir/assets/lowtask-mark.svg" "$dumped_icon"
        ;;
    *)
        fail "unsupported test platform: $(uname -s)"
        ;;
esac

no_make_path="$test_root/no-make-path"
mkdir "$no_make_path"

for required_command in dirname rm uname; do
    command_path=$(command -v "$required_command")
    ln -s "$command_path" "$no_make_path/$required_command"
done

PATH=$no_make_path "$project_dir/install.sh" --prefix "$prefix" --uninstall

test ! -e "$prefix/bin/lowtask"

case $(uname -s) in
    Linux)
        test ! -e "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg"
        test ! -e "$prefix/share/applications/lowtask.desktop"
        ;;
    Darwin)
        test ! -e "$prefix/share/icons/hicolor/scalable/apps/lowtask.svg"
        test ! -e "$prefix/share/applications/lowtask.desktop"
        ;;
esac

literal_prefix="$test_root/"'$(shell printf literal-prefix)'
"$project_dir/install.sh" --prefix "$literal_prefix"

test -x "$literal_prefix/bin/lowtask"

PATH=$no_make_path "$project_dir/install.sh" --prefix "$literal_prefix" --uninstall

test ! -e "$literal_prefix/bin/lowtask"

make_stage="$test_root/make stage"
make_marker="$test_root/make-interpolation-ran"
make_prefix='/literal $(shell touch '"$make_marker"')'
make -C "$project_dir" "DESTDIR=$make_stage" "PREFIX=$make_prefix" install

test ! -e "$make_marker"
test -x "$make_stage$make_prefix/bin/lowtask"

make -C "$project_dir" "DESTDIR=$make_stage" "PREFIX=$make_prefix" uninstall
test ! -e "$make_stage$make_prefix/bin/lowtask"

expect_status 2 make -C "$project_dir" "DESTDIR=$make_stage" PREFIX= install
expect_status 2 make -C "$project_dir" "DESTDIR=$make_stage" PREFIX=relative install
expect_status 2 make -C "$project_dir" "DESTDIR=$make_stage" PREFIX=/opt/../escape install
expect_status 2 make -C "$project_dir" DESTDIR="$make_stage/../escape" PREFIX=/opt install
expect_status 2 make -C "$project_dir" "DESTDIR=$make_stage" PREFIX=/opt HOST_OS=Plan9 install

simulated_darwin_stage="$test_root/simulated darwin"
make -C "$project_dir" -o lowtask HOST_OS=Darwin "DESTDIR=$simulated_darwin_stage" \
    PREFIX=/usr/local install
test -x "$simulated_darwin_stage/usr/local/bin/lowtask"
test ! -e "$simulated_darwin_stage/usr/local/share/icons/hicolor/scalable/apps/lowtask.svg"
test ! -e "$simulated_darwin_stage/usr/local/share/applications/lowtask.desktop"
make -C "$project_dir" HOST_OS=Darwin "DESTDIR=$simulated_darwin_stage" \
    PREFIX=/usr/local uninstall
test ! -e "$simulated_darwin_stage/usr/local/bin/lowtask"

printf '%s\n' 'install-test: PASS'

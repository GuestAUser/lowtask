#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -P "$(dirname "$0")/.." && pwd)
checker="$project_dir/scripts/check-source-size.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/lowtask-source-size.XXXXXX")
active_checker_pid=
active_shim_pid=
active_ready_file=
newline='
'

cleanup() {
    if [ -z "$active_shim_pid" ] && [ -n "$active_ready_file" ] &&
       [ -f "$active_ready_file" ]; then
        IFS= read -r active_shim_pid < "$active_ready_file" || active_shim_pid=
    fi

    if [ -n "$active_checker_pid" ]; then
        kill -TERM "$active_checker_pid" 2>/dev/null || :
    fi
    cleanup_attempts=0
    while [ -z "$active_shim_pid" ] && [ -n "$active_ready_file" ] &&
          [ ! -f "$active_ready_file" ] && [ "$cleanup_attempts" -lt 5 ]; do
        cleanup_attempts=$((cleanup_attempts + 1))
        sleep 1
    done
    if [ -z "$active_shim_pid" ] && [ -n "$active_ready_file" ] &&
       [ -f "$active_ready_file" ]; then
        IFS= read -r active_shim_pid < "$active_ready_file" || active_shim_pid=
    fi
    if [ -n "$active_shim_pid" ]; then
        kill -TERM "$active_shim_pid" 2>/dev/null || :
    fi
    if [ -n "$active_checker_pid" ]; then
        wait "$active_checker_pid" 2>/dev/null || :
    fi
    if [ -n "$active_shim_pid" ] && kill -0 "$active_shim_pid" 2>/dev/null; then
        kill -KILL "$active_shim_pid" 2>/dev/null || :
    fi

    rm -rf "$test_root"
}

trap cleanup 0
trap 'exit 1' HUP INT QUIT TERM

fail() {
    printf 'source-size-test: %s\n' "$1" >&2
    exit 1
}

directory_is_empty() {
    directory=$1

    for entry in "$directory"/* "$directory"/.[!.]* "$directory"/..?*; do
        if [ -e "$entry" ] || [ -L "$entry" ]; then
            return 1
        fi
    done

    return 0
}

write_code_lines() {
    output_file=$1
    line_count=$2

    : > "$output_file"

    append_code_lines "$output_file" "$line_count"
}

append_code_lines() {
    output_file=$1
    line_count=$2
    line_number=1

    while [ "$line_number" -le "$line_count" ]; do
        printf 'int source_line_%s;\n' "$line_number" >> "$output_file"
        line_number=$((line_number + 1))
    done
}

append_repeated_lines() {
    output_file=$1
    line_count=$2
    line_text=$3
    line_number=1

    while [ "$line_number" -le "$line_count" ]; do
        printf '%s\n' "$line_text" >> "$output_file"
        line_number=$((line_number + 1))
    done
}

expect_success() {
    fixture_dir=$1

    if ! sh "$checker" "$fixture_dir" > "$test_root/output" 2>&1; then
        cat "$test_root/output" >&2
        fail "expected success for $fixture_dir"
    fi
}

expect_failure() {
    fixture_dir=$1
    expected_message=$2

    if sh "$checker" "$fixture_dir" > "$test_root/output" 2>&1; then
        fail "expected failure for $fixture_dir"
    fi

    if ! grep -F "$expected_message" "$test_root/output" >/dev/null; then
        cat "$test_root/output" >&2
        fail "missing failure message: $expected_message"
    fi
}

expect_failure_when_find_masks_batch_status() {
    fixture_dir=$1
    find_shim_dir="$test_root/find-shim"
    real_find=$(command -v find)
    mkdir "$find_shim_dir"

    {
        printf '%s\n' '#!/bin/sh'
        printf '"%s" "$@" || :\n' "$real_find"
        printf '%s\n' 'exit 0'
    } > "$find_shim_dir/find"
    chmod +x "$find_shim_dir/find"

    if PATH="$find_shim_dir:$PATH" sh "$checker" "$fixture_dir" \
        > "$test_root/output" 2>&1; then
        fail "checker trusted a zero find status after a failing batch"
    fi

    if ! grep -F '251 pure LOC exceeds the 250-line limit' \
        "$test_root/output" >/dev/null; then
        cat "$test_root/output" >&2
        fail "masked find batch did not report its source-size violation"
    fi
}

expect_tmpdir_clean() {
    fixture_dir=$1
    expected_status=$2
    checker_tmp="$test_root/checker-tmp-$expected_status"
    mkdir "$checker_tmp"

    if [ "$expected_status" = success ]; then
        if ! TMPDIR="$checker_tmp" sh "$checker" "$fixture_dir" \
            > "$test_root/output" 2>&1; then
            cat "$test_root/output" >&2
            fail "expected success while checking temporary-file cleanup"
        fi
    elif TMPDIR="$checker_tmp" sh "$checker" "$fixture_dir" \
        > "$test_root/output" 2>&1; then
        fail "expected failure while checking temporary-file cleanup"
    fi

    if ! directory_is_empty "$checker_tmp"; then
        fail "checker left temporary entries after $expected_status"
    fi
}

expect_signal_cleanup() {
    fixture_dir=$1
    signal_name=$2
    signal_root="$test_root/signal-$signal_name"
    shim_dir="$signal_root/shims"
    isolated_tmp="$signal_root/tmp"
    ready_dir="$signal_root/ready"
    release_pipe="$signal_root/release"
    real_find=$(command -v find)
    mkdir -p "$shim_dir" "$isolated_tmp" "$ready_dir"
    mkfifo "$release_pipe"

    cat > "$shim_dir/find" <<'EOF'
#!/bin/sh
trap 'exit 1' HUP INT QUIT TERM
printf '%s\n' "$$" > "$SIGNAL_READY_DIR/find.pid"
IFS= read -r release < "$SIGNAL_RELEASE"
"$REAL_FIND" "$@"
EOF
    chmod +x "$shim_dir/find"

    active_ready_file="$ready_dir/find.pid"

    set -m
    PATH="$shim_dir:$PATH" \
        TMPDIR="$isolated_tmp" \
        SIGNAL_READY_DIR="$ready_dir" \
        SIGNAL_RELEASE="$release_pipe" \
        REAL_FIND="$real_find" \
        sh "$checker" "$fixture_dir" > "$signal_root/output" 2>&1 &
    active_checker_pid=$!
    set +m

    attempts=0
    while [ "$attempts" -lt 5 ]; do
        if [ -f "$active_ready_file" ]; then
            break
        fi
        if ! kill -0 "$active_checker_pid" 2>/dev/null; then
            cat "$signal_root/output" >&2
            fail "checker exited before $signal_name signal probe was ready"
        fi
        attempts=$((attempts + 1))
        sleep 1
    done

    if [ ! -f "$active_ready_file" ]; then
        fail "timed out preparing $signal_name signal probe"
    fi
    IFS= read -r active_shim_pid < "$active_ready_file"

    kill -s "$signal_name" "$active_checker_pid"
    kill -s "$signal_name" "$active_shim_pid"
    if wait "$active_checker_pid"; then
        checker_status=0
    else
        checker_status=$?
    fi
    active_checker_pid=

    if [ "$checker_status" -ne 1 ]; then
        fail "checker returned $checker_status after $signal_name instead of 1"
    fi

    attempts=0
    while kill -0 "$active_shim_pid" 2>/dev/null; do
        if [ "$attempts" -ge 5 ]; then
            fail "shim remained after $signal_name"
        fi
        attempts=$((attempts + 1))
        sleep 1
    done
    active_shim_pid=
    active_ready_file=

    if ! directory_is_empty "$isolated_tmp"; then
        fail "checker left temporary entries after $signal_name"
    fi
}

passing_dir="$test_root/passing"
mkdir -p "$passing_dir/build"

cat > "$passing_dir/small.c" <<'EOF'
/*
plain block-comment text
*/

int small_value = 1;
EOF

write_code_lines "$passing_dir/build/generated.c" 251
write_code_lines "$passing_dir/boundary.h" 250

comment_heavy_file="$passing_dir/comment-heavy.c"
write_code_lines "$comment_heavy_file" 250
printf '%s\n' '/*' >> "$comment_heavy_file"
append_repeated_lines "$comment_heavy_file" 251 'block comment text'
printf '%s\n' '*/' >> "$comment_heavy_file"
append_repeated_lines "$comment_heavy_file" 251 '// line comment text'
append_repeated_lines "$comment_heavy_file" 251 ''

expect_success "$passing_dir"

oversized_dir="$test_root/oversized"
mkdir "$oversized_dir"
write_code_lines "$oversized_dir/oversized.c" 251
expect_failure "$oversized_dir" '251 pure LOC exceeds the 250-line limit'
expect_failure_when_find_masks_batch_status "$oversized_dir"

inline_block_dir="$test_root/inline-block"
mkdir "$inline_block_dir"
inline_block_file="$inline_block_dir/code_before_block.c"
write_code_lines "$inline_block_file" 249
printf '%s\n' 'int code_before_comment; /*' >> "$inline_block_file"
append_repeated_lines "$inline_block_file" 251 'multiline comment text'
printf '%s\n' '*/' >> "$inline_block_file"
expect_success "$inline_block_dir"

comment_after_code_dir="$test_root/comment-after-code"
mkdir "$comment_after_code_dir"
write_code_lines "$comment_after_code_dir/comment_after_code.c" 250
printf '%s\n' 'int final_line; /* comment after code */' \
    >> "$comment_after_code_dir/comment_after_code.c"
expect_failure "$comment_after_code_dir" '251 pure LOC exceeds the 250-line limit'

literal_delimiter_dir="$test_root/literal-delimiters"
mkdir "$literal_delimiter_dir"
literal_file="$literal_delimiter_dir/literals.c"
write_code_lines "$literal_file" 245
printf '%s\n' \
    'const char *block_start = "/*";' \
    'const char *block_end = "*/";' \
    'const char *line_start = "//";' \
    'const char slash = '\''/'\'';' \
    'const char escaped_quote[] = "\"/* still literal */";' \
    >> "$literal_file"
expect_success "$literal_delimiter_dir"

valid_exempt_dir="$test_root/valid-exempt"
mkdir "$valid_exempt_dir"
printf '%s\n' '/* SIZE_OK: Fixture verifies documented oversized exemptions. */' \
    > "$valid_exempt_dir/valid_exempt.c"
append_code_lines "$valid_exempt_dir/valid_exempt.c" 251
expect_success "$valid_exempt_dir"

line_five_exempt_dir="$test_root/line-five-exempt"
mkdir "$line_five_exempt_dir"
printf '%s\n' '' '// preamble' '/* preamble */' '' \
    '/* SIZE_OK: The fifth physical line carries the waiver. */' \
    > "$line_five_exempt_dir/line_five.c"
append_code_lines "$line_five_exempt_dir/line_five.c" 251
expect_success "$line_five_exempt_dir"

late_exempt_dir="$test_root/late-exempt"
mkdir "$late_exempt_dir"
write_code_lines "$late_exempt_dir/late_exempt.c" 5
printf '%s\n' '/* SIZE_OK: This marker is deliberately too late. */' \
    >> "$late_exempt_dir/late_exempt.c"
append_code_lines "$late_exempt_dir/late_exempt.c" 246
expect_failure "$late_exempt_dir" 'SIZE_OK marker must appear within the first five lines'

empty_exempt_dir="$test_root/empty-exempt"
mkdir "$empty_exempt_dir"
printf '%s\n' '/* SIZE_OK: */' > "$empty_exempt_dir/empty_exempt.c"
append_code_lines "$empty_exempt_dir/empty_exempt.c" 251
expect_failure "$empty_exempt_dir" 'SIZE_OK marker requires a nonempty rationale'

bare_exempt_dir="$test_root/bare-exempt"
mkdir "$bare_exempt_dir"
printf '%s\n' '/* SIZE_OK */' > "$bare_exempt_dir/bare_exempt.c"
append_code_lines "$bare_exempt_dir/bare_exempt.c" 251
expect_failure "$bare_exempt_dir" 'SIZE_OK marker requires a nonempty rationale'

trailing_rationale_dir="$test_root/trailing-rationale"
mkdir "$trailing_rationale_dir"
printf '%s\n' '/* SIZE_OK */ int rationale_is_code;' \
    > "$trailing_rationale_dir/trailing_rationale.c"
append_code_lines "$trailing_rationale_dir/trailing_rationale.c" 250
expect_failure "$trailing_rationale_dir" \
    'SIZE_OK marker requires a nonempty rationale'

marker_like_dir="$test_root/marker-like"
mkdir "$marker_like_dir"
cat > "$marker_like_dir/marker_like.c" <<'EOF'
const char *marker_text = "/* SIZE_OK: strings are not waivers */";
int SIZE_OK = 1;
EOF
append_code_lines "$marker_like_dir/marker_like.c" 249
expect_failure "$marker_like_dir" '251 pure LOC exceeds the 250-line limit'

special_root="$test_root/root [glob]* space"
mkdir -p "$special_root/source/build"
write_code_lines "$special_root/boundary.c" 250
write_code_lines "$special_root/source/build/generated.c" 251
expect_success "$special_root"

newline_root="$test_root/root-ending-in-newline$newline"
mkdir "$newline_root"
write_code_lines "$newline_root/boundary.c" 250
expect_success "$newline_root"

newline_dir="$test_root/newline-name"
mkdir "$newline_dir"
newline_file="$newline_dir/$(printf 'line\nbreak.c')"
write_code_lines "$newline_file" 251
expect_failure "$newline_dir" '251 pure LOC exceeds the 250-line limit'

expect_tmpdir_clean "$passing_dir" success
expect_tmpdir_clean "$oversized_dir" failure

expect_signal_cleanup "$passing_dir" TERM
expect_signal_cleanup "$passing_dir" HUP
expect_signal_cleanup "$passing_dir" INT
expect_signal_cleanup "$passing_dir" QUIT

printf '%s\n' 'source-size-test: PASS'

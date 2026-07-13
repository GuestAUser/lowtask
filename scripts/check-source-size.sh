#!/bin/sh

set -eu

trap 'exit 1' HUP INT QUIT TERM

if [ "$#" -gt 1 ]; then
    printf '%s\n' 'usage: check-source-size.sh [SOURCE_ROOT]' >&2
    exit 2
fi

root=${1:-.}

if [ ! -d "$root" ]; then
    printf 'check-source-size.sh: source root is not a directory: %s\n' "$root" >&2
    exit 2
fi

newline='
'

if canonical_root=$(CDPATH= cd -P "$root" && {
    pwd || exit 1
    printf x
}); then
    canonical_root=${canonical_root%x}
else
    printf 'check-source-size.sh: cannot resolve source root: %s\n' "$root" >&2
    exit 2
fi

case $canonical_root in
    *"$newline") root=${canonical_root%"$newline"} ;;
    *)
        printf 'check-source-size.sh: cannot resolve source root: %s\n' "$root" >&2
        exit 2
        ;;
esac

awk_program='
    function inspect_comment(text, line_number, marker_at, rationale) {
        marker_at = index(text, "SIZE_OK")
        if (marker_at == 0) {
            return
        }

        rationale = substr(text, marker_at + length("SIZE_OK"))
        sub(/^[[:space:]:;,.!?#*\/-]+/, "", rationale)
        sub(/[[:space:]:;,.!?#*\/-]+$/, "", rationale)

        if (line_number > 5) {
            late_marker = 1
        } else if (rationale == "") {
            empty_marker = 1
        } else {
            valid_marker = 1
        }
    }

    function scan_line(line, line_length, position, character, next_character,
                       comment_length, comment_text, has_code) {
        line_length = length(line)
        position = 1
        has_code = 0

        while (position <= line_length) {
            if (in_block_comment) {
                comment_length = index(substr(line, position), "*/")
                if (comment_length == 0) {
                    inspect_comment(substr(line, position), FNR)
                    return has_code
                }

                comment_text = substr(line, position, comment_length - 1)
                inspect_comment(comment_text, FNR)
                position += comment_length + 1
                in_block_comment = 0
                continue
            }

            character = substr(line, position, 1)
            next_character = substr(line, position + 1, 1)

            if (quote != "") {
                has_code = 1
                if (escaped) {
                    escaped = 0
                } else if (character == "\\") {
                    escaped = 1
                } else if (character == quote) {
                    quote = ""
                }
                position++
                continue
            }

            if (character == "/" && next_character == "*") {
                in_block_comment = 1
                position += 2
                continue
            }

            if (character == "/" && next_character == "/") {
                inspect_comment(substr(line, position + 2), FNR)
                return has_code
            }

            if (character == "\"" || character == "\047") {
                quote = character
                has_code = 1
                position++
                continue
            }

            if (character !~ /[[:space:]]/) {
                has_code = 1
            }
            position++
        }

        if (quote != "" && !escaped) {
            quote = ""
        }
        escaped = 0
        return has_code
    }

    {
        if (scan_line($0)) {
            pure_lines++
        }
    }

    END {
        failed = 0

        if (empty_marker) {
            printf "%s: SIZE_OK marker requires a nonempty rationale\n", FILENAME
            failed = 1
        }

        if (late_marker) {
            printf "%s: SIZE_OK marker must appear within the first five lines\n", FILENAME
            failed = 1
        }

        if (pure_lines > 250 && !valid_marker) {
            printf "%s: %d pure LOC exceeds the 250-line limit\n", FILENAME, pure_lines
            failed = 1
        }

        exit failed
    }
'

traversal_status=0

if batch_failures=$(find "$root" -type d -name build -prune -o \
    -type f \( -name '*.c' -o -name '*.h' \) \
    -exec sh -c '
        awk_program=$1
        shift
        batch_failed=

        for source_file in "$@"; do
            if ! awk "$awk_program" "$source_file" 1>&2; then
                batch_failed=1
            fi
        done

        if [ -n "$batch_failed" ]; then
            printf "%s\n" SOURCE_SIZE_BATCH_FAILED
        fi
        exit 0
    ' sh "$awk_program" {} +); then
    traversal_status=0
else
    traversal_status=1
fi

if [ "$traversal_status" -ne 0 ] || [ -n "$batch_failures" ]; then
    exit 1
fi

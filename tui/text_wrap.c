#include "tui/text_wrap.h"

#include "core/text.h"

static const unsigned char *decode(const unsigned char *cursor, uint32_t *codepoint,
                                   size_t *bytes) {
    *codepoint = 0xfffdU;
    *bytes = 1U;
    (void)text_decode_utf8(cursor, codepoint, bytes);
    return cursor + *bytes;
}

static const char *cluster_end(const char *start) {
    const unsigned char *cursor = (const unsigned char *)start;
    uint32_t codepoint = 0U;
    size_t bytes = 0U;
    cursor = decode(cursor, &codepoint, &bytes);
    while (*cursor != '\0') {
        const unsigned char *next = cursor;
        next = decode(next, &codepoint, &bytes);
        if (text_codepoint_width(codepoint) != 0U) break;
        cursor = next;
    }
    return (const char *)cursor;
}

void tui_text_wrap_init(TuiTextWrap *wrap, const char *text, size_t width) {
    if (wrap != NULL) *wrap = (TuiTextWrap){.cursor = text == NULL ? "" : text, .width = width};
}

const char *tui_text_wrap_remaining(TuiTextWrap *wrap) {
    if (wrap == NULL || wrap->cursor == NULL) return "";
    while (*wrap->cursor == ' ') {
        ++wrap->cursor;
        while (*wrap->cursor != '\0') {
            uint32_t codepoint = 0U;
            size_t bytes = 0U;
            (void)decode((const unsigned char *)wrap->cursor, &codepoint, &bytes);
            if (text_codepoint_width(codepoint) != 0U) break;
            wrap->cursor += bytes;
        }
    }
    return wrap->cursor;
}

bool tui_text_wrap_next(TuiTextWrap *wrap, TuiTextLine *line) {
    if (wrap == NULL || line == NULL || wrap->width == 0U) return false;
    const char *start = tui_text_wrap_remaining(wrap);
    if (*start == '\0') return false;
    const unsigned char *cursor = (const unsigned char *)start;
    const unsigned char *last_space = NULL;
    size_t cells = 0U;
    while (*cursor != '\0') {
        const unsigned char *before = cursor;
        uint32_t codepoint = 0U;
        size_t bytes = 0U;
        cursor = decode(cursor, &codepoint, &bytes);
        const size_t cell_width = text_codepoint_width(codepoint);
        if (cell_width > wrap->width - cells) {
            cursor = before;
            break;
        }
        cells += cell_width;
        if (codepoint == ' ') last_space = before;
    }
    const unsigned char *end = cursor;
    if (*cursor != '\0' && last_space != NULL && last_space > (const unsigned char *)start) {
        end = last_space;
    }
    if (end == (const unsigned char *)start) end = (const unsigned char *)cluster_end(start);
    size_t bytes = (size_t)(end - (const unsigned char *)start);
    while (bytes > 0U && start[bytes - 1U] == ' ') --bytes;
    *line = (TuiTextLine){.text = start, .bytes = bytes};
    wrap->cursor = (const char *)end;
    return true;
}

size_t tui_text_wrap_count(const char *text, size_t width) {
    TuiTextWrap wrap;
    tui_text_wrap_init(&wrap, text, width);
    TuiTextLine line;
    size_t count = 0U;
    while (tui_text_wrap_next(&wrap, &line)) ++count;
    return count;
}

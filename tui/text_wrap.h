#ifndef LOWTASK_TUI_TEXT_WRAP_H
#define LOWTASK_TUI_TEXT_WRAP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *cursor;
    size_t width;
} TuiTextWrap;

typedef struct {
    const char *text;
    size_t bytes;
} TuiTextLine;

void tui_text_wrap_init(TuiTextWrap *wrap, const char *text, size_t width);
const char *tui_text_wrap_remaining(TuiTextWrap *wrap);
bool tui_text_wrap_next(TuiTextWrap *wrap, TuiTextLine *line);
size_t tui_text_wrap_count(const char *text, size_t width);

#endif

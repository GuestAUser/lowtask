#ifndef LOWTASK_TUI_COLOR_H
#define LOWTASK_TUI_COLOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TUI_COLOR_CANVAS = 0,
    TUI_COLOR_PANEL,
    TUI_COLOR_RAISED,
    TUI_COLOR_ROW_ALT,
    TUI_COLOR_HOVER,
    TUI_COLOR_SELECTED,
    TUI_COLOR_PRESSED,
    TUI_COLOR_TEXT,
    TUI_COLOR_TEXT_MUTED,
    TUI_COLOR_DATE,
    TUI_COLOR_ACCENT,
    TUI_COLOR_ACCENT_STRONG,
    TUI_COLOR_BORDER,
    TUI_COLOR_GRID,
    TUI_COLOR_URGENT,
    TUI_COLOR_DANGER,
    TUI_COLOR_WARNING,
    TUI_COLOR_INFO,
    TUI_COLOR_COUNT
} TuiColorToken;

uint32_t color_token_rgb(TuiColorToken token);
unsigned color_token_xterm(TuiColorToken token);
int color_ansi(char *output, size_t output_size, uint32_t rgb, bool truecolor, bool foreground);
uint32_t color_blend(uint32_t first, uint32_t second, float amount);

#endif

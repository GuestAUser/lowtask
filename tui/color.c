#include "tui/color.h"

#include <math.h>
#include <stdio.h>

typedef struct {
    uint32_t rgb;
    unsigned xterm;
} ColorValue;

/* Pair semantic RGB values with deliberate xterm fallbacks instead of cube approximations. */
static const ColorValue palette[TUI_COLOR_COUNT] = {
    [TUI_COLOR_CANVAS] = {0x080c09U, 232U},
    [TUI_COLOR_PANEL] = {0x0d1510U, 233U},
    [TUI_COLOR_RAISED] = {0x17221bU, 234U},
    [TUI_COLOR_ROW_ALT] = {0x111b15U, 233U},
    [TUI_COLOR_HOVER] = {0x20352aU, 235U},
    [TUI_COLOR_SELECTED] = {0x1d442eU, 236U},
    [TUI_COLOR_PRESSED] = {0x285b3dU, 237U},
    [TUI_COLOR_TEXT] = {0xe8f3eaU, 255U},
    [TUI_COLOR_TEXT_MUTED] = {0xa9baadU, 250U},
    [TUI_COLOR_DATE] = {0xcad8cdU, 252U},
    [TUI_COLOR_ACCENT] = {0x4ade80U, 121U},
    [TUI_COLOR_ACCENT_STRONG] = {0x86efacU, 157U},
    [TUI_COLOR_BORDER] = {0x4b805dU, 65U},
    [TUI_COLOR_GRID] = {0x1d3828U, 235U},
    [TUI_COLOR_URGENT] = {0xf15d9eU, 205U},
    [TUI_COLOR_DANGER] = {0xf87171U, 210U},
    [TUI_COLOR_WARNING] = {0xe7c55aU, 222U},
    [TUI_COLOR_INFO] = {0x68aeefU, 117U},
};

static unsigned component(uint32_t color, unsigned shift) {
    return (unsigned)((color >> shift) & 0xffU);
}

uint32_t color_token_rgb(TuiColorToken token) {
    return token >= TUI_COLOR_CANVAS && token < TUI_COLOR_COUNT ? palette[token].rgb
                                                                : palette[TUI_COLOR_TEXT].rgb;
}

unsigned color_token_xterm(TuiColorToken token) {
    return token >= TUI_COLOR_CANVAS && token < TUI_COLOR_COUNT ? palette[token].xterm
                                                                : palette[TUI_COLOR_TEXT].xterm;
}

int color_ansi(char *output, size_t output_size, uint32_t rgb, bool truecolor, bool foreground) {
    if (output == NULL || output_size == 0U) {
        return -1;
    }
    const unsigned red = component(rgb, 16U);
    const unsigned green = component(rgb, 8U);
    const unsigned blue = component(rgb, 0U);
    int written = 0;
    if (truecolor) {
        written = snprintf(output, output_size, "\x1b[%d;2;%u;%u;%um", foreground ? 38 : 48,
                           red, green, blue);
    } else {
        unsigned index = 0U;
        bool semantic = false;
        for (size_t token = 0U; token < TUI_COLOR_COUNT; ++token) {
            if (palette[token].rgb == rgb) {
                index = palette[token].xterm;
                semantic = true;
                break;
            }
        }
        if (!semantic) {
            const unsigned red_level = (red * 5U + 127U) / 255U;
            const unsigned green_level = (green * 5U + 127U) / 255U;
            const unsigned blue_level = (blue * 5U + 127U) / 255U;
            index = 16U + 36U * red_level + 6U * green_level + blue_level;
        }
        written = snprintf(output, output_size, "\x1b[%d;5;%um", foreground ? 38 : 48, index);
    }
    return written >= 0 && (size_t)written < output_size ? written : -1;
}

uint32_t color_blend(uint32_t first, uint32_t second, float amount) {
    if (!isfinite(amount)) amount = 0.0F;
    if (amount < 0.0F) amount = 0.0F;
    if (amount > 1.0F) amount = 1.0F;
    uint32_t result = 0U;
    for (unsigned shift = 0U; shift <= 16U; shift += 8U) {
        const float a = (float)component(first, shift);
        const float b = (float)component(second, shift);
        const uint32_t value = (uint32_t)(a + (b - a) * amount + 0.5F);
        result |= value << shift;
    }
    return result;
}

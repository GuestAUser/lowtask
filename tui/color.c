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
    [TUI_COLOR_URGENT] = {0xff8793U, 210U},
    [TUI_COLOR_DANGER] = {0xf87171U, 203U},
    [TUI_COLOR_WARNING] = {0xd6b96bU, 180U},
    [TUI_COLOR_INFO] = {0x7fa58bU, 108U},
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

static unsigned color_distance(uint32_t rgb, unsigned red, unsigned green, unsigned blue) {
    const int red_delta = (int)component(rgb, 16U) - (int)red;
    const int green_delta = (int)component(rgb, 8U) - (int)green;
    const int blue_delta = (int)component(rgb, 0U) - (int)blue;
    return (unsigned)(red_delta * red_delta + green_delta * green_delta +
                      blue_delta * blue_delta);
}

static unsigned nearest_cube_level(unsigned value) {
    static const unsigned levels[] = {0U, 95U, 135U, 175U, 215U, 255U};
    unsigned nearest = 0U;
    unsigned difference = 256U;
    for (unsigned index = 0U; index < 6U; ++index) {
        const unsigned candidate_difference = value > levels[index] ?
                                              value - levels[index] : levels[index] - value;
        if (candidate_difference < difference) {
            nearest = index;
            difference = candidate_difference;
        }
    }
    return nearest;
}

static unsigned nearest_xterm_index(uint32_t rgb) {
    unsigned nearest = palette[TUI_COLOR_TEXT].xterm;
    unsigned distance = UINT32_MAX;
    for (size_t token = 0U; token < TUI_COLOR_COUNT; ++token) {
        const unsigned candidate = color_distance(rgb, component(palette[token].rgb, 16U),
                                                   component(palette[token].rgb, 8U),
                                                   component(palette[token].rgb, 0U));
        if (candidate < distance) {
            nearest = palette[token].xterm;
            distance = candidate;
        }
    }

    static const unsigned levels[] = {0U, 95U, 135U, 175U, 215U, 255U};
    const unsigned red_level = nearest_cube_level(component(rgb, 16U));
    const unsigned green_level = nearest_cube_level(component(rgb, 8U));
    const unsigned blue_level = nearest_cube_level(component(rgb, 0U));
    const unsigned cube_distance = color_distance(rgb, levels[red_level], levels[green_level],
                                                   levels[blue_level]);
    if (cube_distance < distance) {
        nearest = 16U + 36U * red_level + 6U * green_level + blue_level;
        distance = cube_distance;
    }
    for (unsigned index = 232U; index <= 255U; ++index) {
        const unsigned gray = 8U + 10U * (index - 232U);
        const unsigned gray_distance = color_distance(rgb, gray, gray, gray);
        if (gray_distance < distance) {
            nearest = index;
            distance = gray_distance;
        }
    }
    return nearest;
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
        const unsigned index = nearest_xterm_index(rgb);
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

#include "tests/pty_test_api.h"

#include <stdlib.h>
#include <string.h>

bool screen_resize(Screen *screen, size_t columns, size_t rows) {
    if (columns == 0U || rows == 0U || columns > MAX_SCREEN_CELLS / rows) return false;
    ScreenCell *cells = calloc(columns * rows, sizeof(*cells));
    if (cells == NULL) return false;
    free(screen->cells);
    screen->cells = cells;
    screen->columns = columns;
    screen->rows = rows;
    screen->column = 0U;
    screen->row = 0U;
    screen->parser_state = 0U;
    screen->csi_length = 0U;
    screen->utf8_length = 0U;
    screen->utf8_needed = 0U;
    return true;
}

void screen_clear(Screen *screen) {
    memset(screen->cells, 0, screen->columns * screen->rows * sizeof(*screen->cells));
    screen->column = 0U;
    screen->row = 0U;
}

static unsigned int utf8_width(const unsigned char *bytes, size_t length) {
    uint32_t codepoint = bytes[0];
    if (length == 2U) codepoint &= 0x1fU;
    if (length == 3U) codepoint &= 0x0fU;
    if (length == 4U) codepoint &= 0x07U;
    for (size_t index = 1U; index < length; ++index) {
        codepoint = (codepoint << 6U) | (uint32_t)(bytes[index] & 0x3fU);
    }
    if ((codepoint >= 0x0300U && codepoint <= 0x036fU) ||
        (codepoint >= 0x200bU && codepoint <= 0x200fU) ||
        (codepoint >= 0x202aU && codepoint <= 0x202eU) ||
        (codepoint >= 0x2060U && codepoint <= 0x206fU)) return 0U;
    if ((codepoint >= 0x1100U && codepoint <= 0x115fU) ||
        (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) ||
        (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
        (codepoint >= 0xff01U && codepoint <= 0xff60U) ||
        (codepoint >= 0x1f300U && codepoint <= 0x1faffU)) return 2U;
    return 1U;
}

static void screen_put(Screen *screen, const unsigned char *bytes, size_t length) {
    const unsigned int width = utf8_width(bytes, length);
    if (width == 0U) {
        if (screen->column > 0U && screen->row < screen->rows) {
            ScreenCell *cell = &screen->cells[screen->row * screen->columns + screen->column - 1U];
            const size_t used = strlen(cell->glyph);
            if (used + length < sizeof(cell->glyph)) {
                memcpy(cell->glyph + used, bytes, length);
                cell->glyph[used + length] = '\0';
            }
        }
        return;
    }
    if (screen->column >= screen->columns || screen->row >= screen->rows) return;
    ScreenCell *cell = &screen->cells[screen->row * screen->columns + screen->column];
    const size_t copy = length < sizeof(cell->glyph) - 1U ? length : sizeof(cell->glyph) - 1U;
    memcpy(cell->glyph, bytes, copy);
    cell->glyph[copy] = '\0';
    if (width == 2U && screen->column + 1U < screen->columns) {
        screen->cells[screen->row * screen->columns + screen->column + 1U].glyph[0] = '\0';
    }
    screen->column += width;
}

static unsigned long csi_parameter(const Screen *screen, size_t ordinal, unsigned long fallback) {
    size_t current = 0U;
    unsigned long value = 0U;
    bool has_digit = false;
    for (size_t index = 0U; index <= screen->csi_length; ++index) {
        const char byte = index < screen->csi_length ? screen->csi[index] : ';';
        if (byte >= '0' && byte <= '9') {
            value = value * 10U + (unsigned long)(byte - '0');
            has_digit = true;
        } else if (byte == ';') {
            if (current == ordinal) return has_digit ? value : fallback;
            ++current;
            value = 0U;
            has_digit = false;
        }
    }
    return fallback;
}

static void screen_apply_csi(Screen *screen, unsigned char final) {
    if (final == 'H' || final == 'f') {
        const unsigned long row = csi_parameter(screen, 0U, 1U);
        const unsigned long column = csi_parameter(screen, 1U, 1U);
        screen->row = row > 0U && row <= screen->rows ? (size_t)row - 1U : 0U;
        screen->column = column > 0U && column <= screen->columns ? (size_t)column - 1U : 0U;
    } else if (final == 'J' && csi_parameter(screen, 0U, 0U) == 2U) {
        screen_clear(screen);
    }
}

void screen_feed(Screen *screen, const char *bytes, size_t length) {
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char byte = (unsigned char)bytes[index];
        if (screen->utf8_needed > 0U) {
            screen->utf8[screen->utf8_length++] = byte;
            if (screen->utf8_length == screen->utf8_needed) {
                screen_put(screen, screen->utf8, screen->utf8_length);
                screen->utf8_length = 0U;
                screen->utf8_needed = 0U;
            }
            continue;
        }
        if (screen->parser_state == 1U) {
            screen->parser_state = byte == '[' ? 2U : 0U;
            screen->csi_length = 0U;
            continue;
        }
        if (screen->parser_state == 2U) {
            if (byte >= 0x40U && byte <= 0x7eU) {
                screen_apply_csi(screen, byte);
                screen->parser_state = 0U;
            } else if (screen->csi_length + 1U < sizeof(screen->csi)) {
                screen->csi[screen->csi_length++] = (char)byte;
            }
            continue;
        }
        if (byte == 0x1bU) {
            screen->parser_state = 1U;
        } else if (byte == '\r') {
            screen->column = 0U;
        } else if (byte == '\n') {
            if (screen->row + 1U < screen->rows) ++screen->row;
        } else if (byte >= 0x20U && byte < 0x7fU) {
            screen_put(screen, &byte, 1U);
        } else if (byte >= 0xc2U && byte <= 0xf4U) {
            screen->utf8[0] = byte;
            screen->utf8_length = 1U;
            screen->utf8_needed = byte <= 0xdfU ? 2U : (byte <= 0xefU ? 3U : 4U);
        }
    }
}

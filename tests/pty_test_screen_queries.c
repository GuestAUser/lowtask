#include "tests/pty_test_api.h"

#include <stdio.h>
#include <string.h>

static bool screen_row_text(const Screen *screen, size_t row, char *output, size_t capacity) {
    if (row >= screen->rows || capacity == 0U) return false;
    size_t used = 0U;
    for (size_t column = 0U; column < screen->columns && used + 1U < capacity; ++column) {
        const ScreenCell *cell = &screen->cells[row * screen->columns + column];
        const size_t length = strlen(cell->glyph);
        if (length == 0U) {
            output[used++] = ' ';
        } else if (used + length < capacity) {
            memcpy(output + used, cell->glyph, length);
            used += length;
        }
    }
    while (used > 0U && output[used - 1U] == ' ') --used;
    output[used] = '\0';
    return true;
}

void screen_dump(const Screen *screen) {
    char row[8192];
    for (size_t index = 0U; index < screen->rows; ++index) {
        if (screen_row_text(screen, index, row, sizeof(row))) {
            fprintf(stderr, "screen[%zu]=%s\n", index, row);
        }
    }
}

bool screen_contains(const Screen *screen, const char *needle) {
    char row[8192];
    for (size_t index = 0U; index < screen->rows; ++index) {
        if (screen_row_text(screen, index, row, sizeof(row)) && strstr(row, needle) != NULL) return true;
    }
    return false;
}

bool screen_find_ascii_row(const Screen *screen, size_t row, const char *needle, size_t *x) {
    if (row >= screen->rows) return false;
    const size_t length = strlen(needle);
    for (size_t column = 0U; column + length <= screen->columns; ++column) {
        bool match = true;
        for (size_t offset = 0U; offset < length; ++offset) {
            const ScreenCell *cell = &screen->cells[row * screen->columns + column + offset];
            if (cell->glyph[0] != needle[offset] || cell->glyph[1] != '\0') {
                match = false;
                break;
            }
        }
        if (match) {
            *x = column;
            return true;
        }
    }
    return false;
}

bool screen_find_ascii(const Screen *screen, const char *needle, size_t *x, size_t *y) {
    for (size_t row = 0U; row < screen->rows; ++row) {
        if (screen_find_ascii_row(screen, row, needle, x)) {
            *y = row;
            return true;
        }
    }
    return false;
}

bool screen_find_row_title(const Screen *screen, const char *needle, size_t *x, size_t *y) {
    const size_t length = strlen(needle);
    bool found = false;
    size_t best_x = SIZE_MAX;
    size_t best_y = 0U;
    for (size_t row = 2U; row < screen->rows; ++row) {
        for (size_t column = 0U; column + length <= screen->columns; ++column) {
            bool match = true;
            for (size_t offset = 0U; offset < length; ++offset) {
                const ScreenCell *cell = &screen->cells[row * screen->columns + column + offset];
                if (cell->glyph[0] != needle[offset] || cell->glyph[1] != '\0') {
                    match = false;
                    break;
                }
            }
            if (match && column < best_x) {
                best_x = column;
                best_y = row;
                found = true;
            }
        }
    }
    if (found) {
        *x = best_x;
        *y = best_y;
    }
    return found;
}

uint32_t screen_hash(const Screen *screen) {
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < screen->columns * screen->rows; ++index) {
        for (size_t byte = 0U; byte < sizeof(screen->cells[index].glyph); ++byte) {
            hash ^= (unsigned char)screen->cells[index].glyph[byte];
            hash *= 16777619U;
        }
    }
    return hash;
}

#include "tui/text_cells.h"

#include <string.h>

static size_t decode_utf8(const unsigned char *text, uint32_t *codepoint) {
    const unsigned char first = text[0];
    if (first < 0x80U) {
        *codepoint = first;
        return 1U;
    }
    size_t count = 0U;
    uint32_t value = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
        count = 2U; value = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
        count = 3U; value = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        count = 4U; value = first & 0x07U;
    } else {
        *codepoint = 0xfffdU;
        return 1U;
    }
    for (size_t index = 1U; index < count; ++index) {
        if (text[index] == '\0' || (text[index] & 0xc0U) != 0x80U) {
            *codepoint = 0xfffdU;
            return 1U;
        }
        value = (value << 6U) | (uint32_t)(text[index] & 0x3fU);
    }
    *codepoint = value;
    return count;
}

typedef struct {
    uint32_t first;
    uint32_t last;
} UnicodeInterval;

static bool in_zero_width_table(uint32_t codepoint) {
    static const UnicodeInterval intervals[] = {
#include "tui/zero_width.inc"
    };
    size_t low = 0U;
    size_t high = sizeof(intervals) / sizeof(intervals[0]);
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if (codepoint < intervals[middle].first) {
            high = middle;
        } else if (codepoint > intervals[middle].last) {
            low = middle + 1U;
        } else {
            return true;
        }
    }
    return false;
}

static bool is_zero_width_format(uint32_t codepoint) {
    static const UnicodeInterval intervals[] = {
        {0x0600U, 0x0605U}, {0x061cU, 0x061cU}, {0x06ddU, 0x06ddU},
        {0x070fU, 0x070fU}, {0x0890U, 0x0891U}, {0x08e2U, 0x08e2U},
        {0x180eU, 0x180eU}, {0x200bU, 0x200fU}, {0x202aU, 0x202eU},
        {0x2060U, 0x2064U}, {0x2066U, 0x206fU}, {0xfeffU, 0xfeffU},
        {0xfff9U, 0xfffbU}, {0x110bdU, 0x110bdU}, {0x110cdU, 0x110cdU},
        {0x13430U, 0x1343fU}, {0x1bca0U, 0x1bca3U}, {0x1d173U, 0x1d17aU},
        {0xe0001U, 0xe0001U}, {0xe0020U, 0xe007fU},
    };
    size_t low = 0U;
    size_t high = sizeof(intervals) / sizeof(intervals[0]);
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if (codepoint < intervals[middle].first) high = middle;
        else if (codepoint > intervals[middle].last) low = middle + 1U;
        else return true;
    }
    return false;
}

unsigned renderer_codepoint_width(uint32_t codepoint) {
    if (in_zero_width_table(codepoint) || is_zero_width_format(codepoint) ||
        (codepoint >= 0x1160U && codepoint <= 0x11ffU)) return 0U;
    if ((codepoint >= 0x1100U && codepoint <= 0x115fU) ||
        (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) ||
        (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
        (codepoint >= 0xff01U && codepoint <= 0xff60U) ||
        (codepoint >= 0xffe0U && codepoint <= 0xffe6U) ||
        (codepoint >= 0x1f300U && codepoint <= 0x1faffU) ||
        (codepoint >= 0x20000U && codepoint <= 0x3fffdU)) {
        return 2U;
    }
    return 1U;
}

void renderer_put_utf8(Renderer *renderer, size_t x, size_t y, const char *text,
                       size_t max_cells, RendererStyle style) {
    if (renderer == NULL || text == NULL || y >= renderer->height || x >= renderer->width) {
        return;
    }
    size_t used = 0U;
    RendererCell *cluster = NULL;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        uint32_t codepoint = 0U;
        const size_t byte_count = decode_utf8(cursor, &codepoint);
        const unsigned width = renderer_codepoint_width(codepoint);
        if (width == 0U) {
            if (cluster != NULL && cluster->glyph_length + byte_count < sizeof(cluster->glyph)) {
                memcpy(cluster->glyph + cluster->glyph_length, cursor, byte_count);
                cluster->glyph_length = (uint8_t)(cluster->glyph_length + byte_count);
                cluster->glyph[cluster->glyph_length] = '\0';
            }
            cursor += byte_count;
            continue;
        }
        if (used + width > max_cells || x + used + width > renderer->width) {
            break;
        }
        RendererCell *cell = &renderer->back[y * renderer->width + x + used];
        *cell = (RendererCell){
            .foreground = style.foreground, .background = style.background,
            .glyph_length = (uint8_t)byte_count, .width = (uint8_t)width,
            .attributes = style.attributes,
        };
        memcpy(cell->glyph, cursor, byte_count);
        cell->glyph[byte_count] = '\0';
        cluster = cell;
        if (width == 2U) {
            RendererCell *continuation = cell + 1;
            *continuation = (RendererCell){
                .foreground = style.foreground, .background = style.background,
                .glyph_length = 0U, .width = 0U, .attributes = style.attributes,
            };
        }
        used += width;
        cursor += byte_count;
    }
}

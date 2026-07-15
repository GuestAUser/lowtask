#include "tui/text_cells.h"

#include "core/text.h"

#include <string.h>

unsigned renderer_codepoint_width(uint32_t codepoint) {
    return text_codepoint_width(codepoint);
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
        size_t byte_count = 1U;
        (void)text_decode_utf8(cursor, &codepoint, &byte_count);
        const unsigned width = renderer_codepoint_width(codepoint);
        if (width == 0U) {
            /* Zero-width code points belong to the prior cell; excess bytes never claim geometry. */
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
            /* The lead cell owns the glyph; a width-zero sentinel protects its second column. */
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

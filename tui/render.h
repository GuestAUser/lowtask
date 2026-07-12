#ifndef LOWTASK_TUI_RENDER_H
#define LOWTASK_TUI_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    RENDER_ATTR_NONE = 0U,
    RENDER_ATTR_BOLD = 1U << 0U,
    RENDER_ATTR_DIM = 1U << 1U,
    RENDER_ATTR_STRIKE = 1U << 2U
};

typedef struct {
    uint32_t foreground;
    uint32_t background;
    unsigned attributes;
} RendererStyle;

typedef struct {
    char glyph[17];
    uint32_t foreground;
    uint32_t background;
    uint8_t glyph_length;
    uint8_t width;
    unsigned attributes;
} RendererCell;

typedef struct {
    size_t width;
    size_t height;
    RendererCell *front;
    RendererCell *back;
    char *output;
    size_t output_capacity;
    size_t pending_offset;
    size_t pending_length;
    bool front_valid;
    bool truecolor;
} Renderer;

bool renderer_init(Renderer *renderer, size_t width, size_t height, bool truecolor);
bool renderer_resize(Renderer *renderer, size_t width, size_t height);
void renderer_free(Renderer *renderer);
void renderer_begin(Renderer *renderer, uint32_t background);
unsigned renderer_codepoint_width(uint32_t codepoint);
void renderer_put_utf8(Renderer *renderer, size_t x, size_t y, const char *text,
                       size_t max_cells, RendererStyle style);
void renderer_fill(Renderer *renderer, size_t x, size_t y, size_t width, size_t height,
                   char glyph, RendererStyle style);
ssize_t renderer_present(Renderer *renderer, int descriptor);
bool renderer_has_pending_output(const Renderer *renderer);

#endif

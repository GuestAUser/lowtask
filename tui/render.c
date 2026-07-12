#include "tui/text_cells.h"

#include "tui/color.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool cell_count(size_t width, size_t height, size_t *count) {
    if (width == 0U || height == 0U || width > SIZE_MAX / height) {
        return false;
    }
    *count = width * height;
    return *count <= SIZE_MAX / sizeof(RendererCell);
}

bool renderer_resize(Renderer *renderer, size_t width, size_t height) {
    if (renderer == NULL || width > 1000U || height > 1000U) {
        return false;
    }
    size_t count = 0U;
    if (!cell_count(width, height, &count) || count > (SIZE_MAX - 128U) / 80U) {
        return false;
    }
    RendererCell *front = calloc(count, sizeof(*front));
    RendererCell *back = calloc(count, sizeof(*back));
    char *output = malloc(count * 80U + 128U);
    if (front == NULL || back == NULL || output == NULL) {
        free(front);
        free(back);
        free(output);
        return false;
    }
    free(renderer->front);
    free(renderer->back);
    free(renderer->output);
    renderer->front = front;
    renderer->back = back;
    renderer->output = output;
    renderer->output_capacity = count * 80U + 128U;
    renderer->width = width;
    renderer->height = height;
    renderer->front_valid = false;
    renderer->pending_offset = 0U;
    renderer->pending_length = 0U;
    return true;
}

bool renderer_init(Renderer *renderer, size_t width, size_t height, bool truecolor) {
    if (renderer == NULL) {
        return false;
    }
    *renderer = (Renderer){.truecolor = truecolor};
    return renderer_resize(renderer, width, height);
}

void renderer_free(Renderer *renderer) {
    if (renderer != NULL) {
        free(renderer->front);
        free(renderer->back);
        free(renderer->output);
        *renderer = (Renderer){0};
    }
}

void renderer_begin(Renderer *renderer, uint32_t background) {
    if (renderer == NULL || renderer->back == NULL) {
        return;
    }
    const size_t count = renderer->width * renderer->height;
    for (size_t index = 0U; index < count; ++index) {
        renderer->back[index] = (RendererCell){
            .glyph = " ", .foreground = 0xffffffU, .background = background,
            .glyph_length = 1U, .width = 1U, .attributes = RENDER_ATTR_NONE,
        };
    }
}

void renderer_fill(Renderer *renderer, size_t x, size_t y, size_t width, size_t height,
                   char glyph, RendererStyle style) {
    if (renderer == NULL || (unsigned char)glyph >= 0x80U) {
        return;
    }
    char text[2] = {glyph, '\0'};
    for (size_t row = 0U; row < height && y + row < renderer->height; ++row) {
        for (size_t column = 0U; column < width && x + column < renderer->width; ++column) {
            renderer_put_utf8(renderer, x + column, y + row, text, 1U, style);
        }
    }
}

static bool append_bytes(Renderer *renderer, size_t *length, const char *bytes, size_t count) {
    if (*length > renderer->output_capacity || count > renderer->output_capacity - *length) {
        return false;
    }
    memcpy(renderer->output + *length, bytes, count);
    *length += count;
    return true;
}

static bool append_format(Renderer *renderer, size_t *length, const char *format, size_t a, size_t b) {
    if (*length >= renderer->output_capacity) {
        return false;
    }
    const int written = snprintf(renderer->output + *length, renderer->output_capacity - *length,
                                 format, a, b);
    if (written < 0 || (size_t)written >= renderer->output_capacity - *length) {
        return false;
    }
    *length += (size_t)written;
    return true;
}

static bool append_style(Renderer *renderer, size_t *length, const RendererCell *cell) {
    char escape[64];
    const int foreground = color_ansi(escape, sizeof(escape), cell->foreground, renderer->truecolor, true);
    if (!append_bytes(renderer, length, "\x1b[0m", 4U) || foreground < 0 ||
        !append_bytes(renderer, length, escape, (size_t)foreground)) {
        return false;
    }
    const int background = color_ansi(escape, sizeof(escape), cell->background, renderer->truecolor, false);
    if (background < 0 || !append_bytes(renderer, length, escape, (size_t)background)) {
        return false;
    }
    if ((cell->attributes & RENDER_ATTR_BOLD) != 0U && !append_bytes(renderer, length, "\x1b[1m", 4U)) return false;
    if ((cell->attributes & RENDER_ATTR_DIM) != 0U && !append_bytes(renderer, length, "\x1b[2m", 4U)) return false;
    if ((cell->attributes & RENDER_ATTR_STRIKE) != 0U && !append_bytes(renderer, length, "\x1b[9m", 4U)) return false;
    return true;
}

static bool cells_equal(const RendererCell *first, const RendererCell *second) {
    return first->foreground == second->foreground && first->background == second->background &&
           first->glyph_length == second->glyph_length && first->width == second->width &&
           first->attributes == second->attributes &&
           memcmp(first->glyph, second->glyph, sizeof(first->glyph)) == 0;
}

static bool styles_equal(const RendererCell *first, const RendererCell *second) {
    return first->foreground == second->foreground && first->background == second->background &&
           first->attributes == second->attributes;
}

bool renderer_has_pending_output(const Renderer *renderer) {
    return renderer != NULL && renderer->pending_offset < renderer->pending_length;
}

static int drain_pending_output(Renderer *renderer, int descriptor) {
    while (renderer->pending_offset < renderer->pending_length) {
        const ssize_t written = write(descriptor, renderer->output + renderer->pending_offset,
                                      renderer->pending_length - renderer->pending_offset);
        if (written > 0) {
            renderer->pending_offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return -1;
    }
    renderer->pending_offset = 0U;
    renderer->pending_length = 0U;
    return 1;
}

ssize_t renderer_present(Renderer *renderer, int descriptor) {
    if (renderer == NULL || descriptor < 0) {
        errno = EINVAL;
        return -1;
    }
    const size_t count = renderer->width * renderer->height;
    if (renderer_has_pending_output(renderer)) {
        const int drained = drain_pending_output(renderer, descriptor);
        if (drained <= 0) return drained;
        renderer->front_valid = false;
    }
    size_t output_length = 0U;
    size_t index = 0U;
    while (index < count) {
        const RendererCell *cell = &renderer->back[index];
        if ((renderer->front_valid && cells_equal(cell, &renderer->front[index])) || cell->width == 0U) {
            ++index;
            continue;
        }
        const size_t row = index / renderer->width + 1U;
        const size_t column = index % renderer->width + 1U;
        if (!append_format(renderer, &output_length, "\x1b[%zu;%zuH", row, column) ||
            !append_style(renderer, &output_length, cell)) {
            errno = EOVERFLOW;
            return -1;
        }
        const size_t row_end = row * renderer->width;
        while (index < row_end) {
            const RendererCell *run_cell = &renderer->back[index];
            if (run_cell->width == 0U) {
                ++index;
                continue;
            }
            if ((renderer->front_valid && cells_equal(run_cell, &renderer->front[index])) ||
                !styles_equal(cell, run_cell)) {
                break;
            }
            if (!append_bytes(renderer, &output_length, run_cell->glyph, run_cell->glyph_length)) {
                errno = EOVERFLOW;
                return -1;
            }
            index += run_cell->width;
        }
    }
    renderer->pending_offset = 0U;
    renderer->pending_length = output_length;
    const int drained = drain_pending_output(renderer, descriptor);
    if (drained <= 0) return drained;
    memcpy(renderer->front, renderer->back, count * sizeof(*renderer->front));
    renderer->front_valid = true;
    return output_length <= (size_t)SSIZE_MAX ? (ssize_t)output_length : -1;
}

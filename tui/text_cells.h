#ifndef LOWTASK_TUI_TEXT_CELLS_H
#define LOWTASK_TUI_TEXT_CELLS_H

#include "tui/render.h"

unsigned renderer_codepoint_width(uint32_t codepoint);
void renderer_put_utf8(Renderer *renderer, size_t x, size_t y, const char *text,
                       size_t max_cells, RendererStyle style);

#endif

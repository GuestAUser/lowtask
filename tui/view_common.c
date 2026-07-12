#include "tui/view_common.h"

#include "tui/color.h"

#include <string.h>

uint32_t tui_view_color(TuiColorToken token) {
    return color_token_rgb(token);
}

RendererStyle tui_view_style(TuiColorToken foreground, TuiColorToken background,
                             unsigned attributes) {
    return (RendererStyle){
        .foreground = tui_view_color(foreground),
        .background = tui_view_color(background),
        .attributes = attributes,
    };
}

uint32_t tui_view_decode_codepoint(const unsigned char **cursor) {
    const unsigned char *bytes = *cursor;
    uint32_t codepoint = bytes[0];
    size_t count = 1U;
    if (bytes[0] >= 0xc2U && bytes[0] <= 0xdfU) {
        codepoint = bytes[0] & 0x1fU;
        count = 2U;
    } else if (bytes[0] >= 0xe0U && bytes[0] <= 0xefU) {
        codepoint = bytes[0] & 0x0fU;
        count = 3U;
    } else if (bytes[0] >= 0xf0U && bytes[0] <= 0xf4U) {
        codepoint = bytes[0] & 0x07U;
        count = 4U;
    }
    for (size_t index = 1U; index < count; ++index) {
        if (bytes[index] == '\0' || (bytes[index] & 0xc0U) != 0x80U) {
            *cursor += 1U;
            return 0xfffdU;
        }
        codepoint = (codepoint << 6U) | (uint32_t)(bytes[index] & 0x3fU);
    }
    *cursor += count;
    return codepoint;
}

size_t tui_view_display_cells(const char *text) {
    size_t width = 0U;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        const uint32_t codepoint = tui_view_decode_codepoint(&cursor);
        width += renderer_codepoint_width(codepoint);
    }
    return width;
}

static size_t prefix_cells_that_fit(const char *text, size_t maximum_cells) {
    size_t width = 0U;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        const uint32_t codepoint = tui_view_decode_codepoint(&cursor);
        const size_t codepoint_width = renderer_codepoint_width(codepoint);
        if (codepoint_width > maximum_cells - width) break;
        width += codepoint_width;
    }
    return width;
}

const char *tui_view_suffix_that_fits(const char *text, size_t maximum_cells) {
    const char *start = text;
    while (*start != '\0' && tui_view_display_cells(start) > maximum_cells) {
        const unsigned char *next = (const unsigned char *)start;
        (void)tui_view_decode_codepoint(&next);
        while (*next != '\0') {
            const unsigned char *after = next;
            const uint32_t codepoint = tui_view_decode_codepoint(&after);
            if (renderer_codepoint_width(codepoint) != 0U) break;
            next = after;
        }
        start = (const char *)next;
    }
    return start;
}

void tui_view_put(Renderer *renderer, size_t x, size_t y, const char *text, size_t limit,
                  RendererStyle cell_style) {
    if (x >= renderer->width || y >= renderer->height || limit == 0U) return;
    char safe[1024];
    size_t source = 0U;
    size_t target = 0U;
    while (text[source] != '\0' && target + 1U < sizeof(safe)) {
        const unsigned char first = (unsigned char)text[source];
        if (first < 0x20U || first == 0x7fU || (first >= 0x80U && first < 0xc2U) ||
            first > 0xf4U) {
            safe[target++] = '?';
            ++source;
            continue;
        }
        if (first < 0x80U) {
            safe[target++] = text[source++];
            continue;
        }
        const size_t count = first <= 0xdfU ? 2U : (first <= 0xefU ? 3U : 4U);
        uint32_t codepoint = first & (count == 2U ? 0x1fU : (count == 3U ? 0x0fU : 0x07U));
        bool valid = target + count < sizeof(safe);
        for (size_t index = 1U; valid && index < count; ++index) {
            const unsigned char continuation = (unsigned char)text[source + index];
            if (continuation == '\0' || (continuation & 0xc0U) != 0x80U) {
                valid = false;
            } else {
                codepoint = (codepoint << 6U) | (uint32_t)(continuation & 0x3fU);
            }
        }
        if (!valid) {
            safe[target++] = '?';
            ++source;
            continue;
        }
        const bool overlong = (count == 2U && codepoint < 0x80U) ||
                              (count == 3U && codepoint < 0x800U) ||
                              (count == 4U && codepoint < 0x10000U);
        if (overlong || (codepoint >= 0x80U && codepoint <= 0x9fU) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
            safe[target++] = '?';
            source += count;
            continue;
        }
        memcpy(safe + target, text + source, count);
        target += count;
        source += count;
    }
    safe[target] = '\0';
    const size_t available = renderer->width - x;
    renderer_put_utf8(renderer, x, y, safe, limit < available ? limit : available, cell_style);
}

void tui_view_put_truncated(Renderer *renderer, size_t x, size_t y, const char *text,
                            size_t limit, bool ascii, RendererStyle cell_style) {
    if (tui_view_display_cells(text) <= limit) {
        tui_view_put(renderer, x, y, text, limit, cell_style);
        return;
    }
    const char *marker = ascii ? "..." : "…";
    const size_t marker_width = ascii ? 3U : 1U;
    if (limit <= marker_width) {
        tui_view_put(renderer, x, y, ascii && limit < marker_width ? text : marker, limit,
                     cell_style);
        return;
    }
    const size_t content_width = limit - marker_width;
    const size_t emitted_width = prefix_cells_that_fit(text, content_width);
    tui_view_put(renderer, x, y, text, content_width, cell_style);
    tui_view_put(renderer, x + emitted_width, y, marker, marker_width, cell_style);
}

void tui_view_draw_box(Renderer *renderer, TuiRect rectangle, bool ascii,
                       RendererStyle box_style) {
    if (rectangle.width < 2U || rectangle.height < 2U) return;
    const char *top_left = ascii ? "+" : "╭";
    const char *top_right = ascii ? "+" : "╮";
    const char *bottom_left = ascii ? "+" : "╰";
    const char *bottom_right = ascii ? "+" : "╯";
    const char *horizontal = ascii ? "-" : "─";
    const char *vertical = ascii ? "|" : "│";
    tui_view_put(renderer, rectangle.x, rectangle.y, top_left, 1U, box_style);
    tui_view_put(renderer, rectangle.x + rectangle.width - 1U, rectangle.y, top_right, 1U,
                 box_style);
    tui_view_put(renderer, rectangle.x, rectangle.y + rectangle.height - 1U, bottom_left, 1U,
                 box_style);
    tui_view_put(renderer, rectangle.x + rectangle.width - 1U,
                 rectangle.y + rectangle.height - 1U, bottom_right, 1U, box_style);
    for (size_t column = 1U; column + 1U < rectangle.width; ++column) {
        tui_view_put(renderer, rectangle.x + column, rectangle.y, horizontal, 1U, box_style);
        tui_view_put(renderer, rectangle.x + column, rectangle.y + rectangle.height - 1U,
                     horizontal, 1U, box_style);
    }
    for (size_t row = 1U; row + 1U < rectangle.height; ++row) {
        tui_view_put(renderer, rectangle.x, rectangle.y + row, vertical, 1U, box_style);
        tui_view_put(renderer, rectangle.x + rectangle.width - 1U, rectangle.y + row, vertical,
                     1U, box_style);
    }
}

bool tui_view_action_equal(AppAction first, AppAction second) {
    return first.type == second.type && first.tab == second.tab && first.task_id == second.task_id &&
           first.option.kind == second.option.kind && first.option.value == second.option.value;
}

const char *tui_view_tab_name(AppTab tab, bool compact) {
    static const char *const full[] = {"ALL", "TODAY", "UPCOMING", "COMPLETED"};
    static const char *const short_names[] = {"ALL", "TODAY", "SOON", "DONE"};
    return compact ? short_names[tab] : full[tab];
}

const char *tui_view_tab_title(AppTab tab, bool compact) {
    static const char *const full[] = {"All", "Today", "Upcoming", "Completed"};
    static const char *const short_names[] = {"All", "Today", "Soon", "Done"};
    return compact ? short_names[tab] : full[tab];
}

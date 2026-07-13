#include "tui/view_common.h"

#include <string.h>

typedef struct {
    char text[384];
    bool title;
} HelpVisualLine;

typedef struct {
    const char *title;
    const char *keyboard;
    const char *mouse;
} HelpGroup;

static const HelpGroup help_groups[] = {
    {"Navigation and views",
     "Keys: j/Down and k/Up select next/previous; g/Home and G/End select first/last; Tab/Shift-Tab and [/] cycle the four views.",
     "Mouse: Row-body release selects; tab release activates; wheel moves three visible tasks and does not activate a task or tab."},
    {"Add and edit",
     "Keys: a adds, e edits, Enter accepts text, Escape cancels, and q/Ctrl-C quits.",
     "Mouse: Add/edit remain text modal interactions; outside clicks do not submit or dismiss."},
    {"Completion and deletion",
     "Keys: Space or x completes/reopens; d or Delete starts deletion. q or Ctrl-C commits a pending deletion immediately before quit.",
     "Mouse: Checkbox release toggles completion. A delete request cannot be canceled; it commits when its delete effect completes and locks other mutation first."},
    {"Priority",
     "Keys: 1 Low, 2 Normal, 3 High, 4 Urgent; h lowers, l raises, and p opens the picker.",
     "Mouse: Priority-marker release selects the task and opens the priority picker. Picker arrows or j/k move, Enter/direct number applies, Escape cancels."},
    {"Schedule and due state",
     "Keys: s opens Today, Tomorrow, +7 Days, Custom, and Clear. Due metadata uses ! overdue, = today, > future, muted dot unscheduled, and muted x completed.",
     "Mouse: Date-field or compact-metadata release opens schedule. At 24-51 columns the compact slot remains that target; wider date/context views retain due detail."},
    {"Pointer safety",
     "Keys: Keyboard remains fully available when pointer input is absent.",
     "Mouse: Primary press/release applies only within the same target. Picker press records its exact option and release applies it once only there; release elsewhere cancels. Right click and double click do nothing."},
    {"Filter and sort",
     "Keys: f cycles Any/Urgent/High/Normal/Low; o cycles Smart/Created/Due/Priority.",
     "Mouse: Visible filter/sort header badges perform the same forward cycle."},
    {"Help",
     "Keys: ? opens from normal mode; Escape or ? closes.",
     "Mouse: Header HELP or ? opens; the top-right X target closes."},
    {"Drag and drop",
     "Keys: Drag is pointer-only; keyboard selection never follows pointer hover or drag target highlight. Escape cancels an active drag; q/Ctrl-C cancel it before quit.",
     "Mouse: Primary row-body press, a 2-cell Manhattan movement, and release on a visible tab target performs the documented stable-ID drop. Wheel cancels; controls keep their normal click behavior."},
};

static void help_add_wrapped(HelpVisualLine *lines, size_t capacity, size_t *count,
                             const char *text, size_t width, bool title) {
    if (width == 0U || *count >= capacity) return;
    const char *cursor = text;
    while (*cursor != '\0' && *count < capacity) {
        while (*cursor == ' ') ++cursor;
        const unsigned char *scan = (const unsigned char *)cursor;
        const unsigned char *last_space = NULL;
        size_t cells = 0U;
        while (*scan != '\0') {
            const unsigned char *before = scan;
            const uint32_t codepoint = tui_view_decode_codepoint(&scan);
            const size_t cell_width = renderer_codepoint_width(codepoint);
            if (cell_width > width - cells) {
                scan = before;
                break;
            }
            cells += cell_width;
            if (codepoint == ' ') last_space = before;
        }
        const unsigned char *end = scan;
        if (*scan != '\0' && last_space != NULL && last_space > (const unsigned char *)cursor) {
            end = last_space;
        }
        if (end == (const unsigned char *)cursor) {
            end = scan;
            if (end == (const unsigned char *)cursor) ++end;
        }
        size_t bytes = (size_t)(end - (const unsigned char *)cursor);
        while (bytes > 0U && cursor[bytes - 1U] == ' ') --bytes;
        if (bytes >= sizeof(lines[*count].text)) bytes = sizeof(lines[*count].text) - 1U;
        memcpy(lines[*count].text, cursor, bytes);
        lines[*count].text[bytes] = '\0';
        lines[*count].title = title;
        ++*count;
        cursor = (const char *)end;
        while (*cursor == ' ') ++cursor;
    }
}

static size_t build_help_column(HelpVisualLine *lines, size_t capacity, size_t width,
                                 size_t first_group, size_t end_group) {
    size_t count = 0U;
    for (size_t index = first_group; index < end_group; ++index) {
        help_add_wrapped(lines, capacity, &count, help_groups[index].title, width, true);
        help_add_wrapped(lines, capacity, &count, help_groups[index].keyboard, width, false);
        help_add_wrapped(lines, capacity, &count, help_groups[index].mouse, width, false);
        if (index + 1U < end_group && count < capacity) lines[count++] = (HelpVisualLine){0};
    }
    return count;
}

static size_t help_column_split(size_t width) {
    HelpVisualLine lines[256];
    const size_t group_count = sizeof(help_groups) / sizeof(help_groups[0]);
    size_t best_split = 1U;
    size_t best_difference = SIZE_MAX;
    /* Shared scrolling stays useful only while both group-aligned columns have content. */
    for (size_t split = 1U; split < group_count; ++split) {
        const size_t left_count = build_help_column(lines, 256U, width, 0U, split);
        const size_t right_count = build_help_column(lines, 256U, width, split, group_count);
        const size_t difference = left_count > right_count ?
                                  left_count - right_count : right_count - left_count;
        if (difference < best_difference) {
            best_split = split;
            best_difference = difference;
        }
    }
    return best_split;
}

static void help_geometry(size_t width, size_t height, size_t *body_width,
                          size_t *page_rows, bool *two_columns) {
    size_t overlay_width = width;
    if (width >= TUI_WIDE_COLUMNS) overlay_width = width - 8U < 80U ? width - 8U : 80U;
    else if (width >= TUI_STANDARD_COLUMNS) overlay_width = width - 8U < 60U ? width - 8U : 60U;
    else if (width >= TUI_NARROW_COLUMNS && width > 2U) overlay_width = width - 2U;
    const bool framed = height >= 9U && width >= TUI_STANDARD_COLUMNS;
    *body_width = overlay_width > (framed ? 2U : 0U) ?
                  overlay_width - (framed ? 2U : 0U) : 0U;
    *page_rows = height == 4U ? 1U :
                 (height >= 5U && height <= 8U ? height - 2U :
                 (height >= 9U ? height - 6U : 0U));
    *two_columns = width >= TUI_WIDE_COLUMNS && *body_width >= 7U;
}

void tui_help_metrics(size_t width, size_t height, bool ascii, size_t *line_count,
                      size_t *page_rows) {
    (void)ascii;
    if (line_count == NULL || page_rows == NULL) return;
    size_t body_width = 0U;
    bool two_columns = false;
    help_geometry(width, height, &body_width, page_rows, &two_columns);
    if (body_width == 0U || *page_rows == 0U) {
        *line_count = 0U;
        return;
    }
    HelpVisualLine left[256];
    HelpVisualLine right[256];
    if (two_columns) {
        const size_t column_width = (body_width - 3U) / 2U;
        const size_t split = help_column_split(column_width);
        const size_t left_count = build_help_column(left, 256U, column_width, 0U, split);
        const size_t right_count = build_help_column(right, 256U, column_width, split,
            sizeof(help_groups) / sizeof(help_groups[0]));
        *line_count = left_count > right_count ? left_count : right_count;
    } else {
        *line_count = build_help_column(left, 256U, body_width, 0U,
            sizeof(help_groups) / sizeof(help_groups[0]));
    }
}

static void draw_help_lines(Renderer *renderer, const TuiRect body, const AppState *app,
                            HelpVisualLine *lines, size_t count, size_t x, size_t width) {
    for (size_t row = 0U; row < body.height; ++row) {
        const size_t index = app->help_scroll + row;
        if (index >= count) break;
        tui_view_put(renderer, x, body.y + row, lines[index].text, width,
                     tui_view_style(lines[index].title ? TUI_COLOR_ACCENT : TUI_COLOR_TEXT,
                                    TUI_COLOR_RAISED,
                                    lines[index].title ? RENDER_ATTR_BOLD : RENDER_ATTR_NONE));
    }
}

void tui_view_draw_help(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view) {
    if (view->app->mode != APP_MODE_HELP && view->mode != TUI_MODE_HELP) return;
    if (renderer->height <= 3U) return;
    const bool framed = renderer->height >= 9U && renderer->width >= TUI_STANDARD_COLUMNS;
    renderer_fill(renderer, layout->help_overlay.x, layout->help_overlay.y,
                  layout->help_overlay.width, layout->help_overlay.height, ' ',
                  tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
    if (framed) {
        tui_view_draw_box(renderer, layout->help_overlay, view->ascii,
                          tui_view_style(TUI_COLOR_BORDER, TUI_COLOR_RAISED,
                                         RENDER_ATTR_NONE));
    }
    tui_view_put(renderer, layout->help_body.x, layout->help_body.y - 1U, "HELP",
                 layout->help_body.width,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
    tui_view_put(renderer, layout->help_close.x, layout->help_close.y,
                 view->ascii ? "[X]" : "[×]", layout->help_close.width,
                 tui_view_style(TUI_COLOR_ACCENT_STRONG, TUI_COLOR_RAISED,
                                RENDER_ATTR_BOLD));
    HelpVisualLine left[256];
    HelpVisualLine right[256];
    if (renderer->width >= TUI_WIDE_COLUMNS && layout->help_body.width >= 7U) {
        const size_t column_width = (layout->help_body.width - 3U) / 2U;
        const size_t split = help_column_split(column_width);
        const size_t left_count = build_help_column(left, 256U, column_width, 0U, split);
        const size_t right_count = build_help_column(right, 256U, column_width, split,
            sizeof(help_groups) / sizeof(help_groups[0]));
        draw_help_lines(renderer, layout->help_body, view->app, left, left_count,
                        layout->help_body.x, column_width);
        draw_help_lines(renderer, layout->help_body, view->app, right, right_count,
                        layout->help_body.x + column_width + 3U, column_width);
    } else {
        const size_t count = build_help_column(left, 256U, layout->help_body.width, 0U,
            sizeof(help_groups) / sizeof(help_groups[0]));
        draw_help_lines(renderer, layout->help_body, view->app, left, count,
                        layout->help_body.x, layout->help_body.width);
    }
}

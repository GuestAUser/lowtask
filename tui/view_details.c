#include "tui/view_common.h"

#include <stdio.h>
#include <string.h>

static const char *wrapped_end(const char *start, size_t width) {
    const unsigned char *cursor = (const unsigned char *)start;
    const unsigned char *last_space = NULL;
    size_t cells = 0U;
    while (*cursor != '\0') {
        const unsigned char *before = cursor;
        const uint32_t codepoint = tui_view_decode_codepoint(&cursor);
        const size_t cell_width = renderer_codepoint_width(codepoint);
        if (cell_width > width - cells) {
            cursor = before;
            break;
        }
        cells += cell_width;
        if (codepoint == ' ') last_space = before;
    }
    if (*cursor != '\0' && last_space != NULL && last_space > (const unsigned char *)start) {
        return (const char *)last_space;
    }
    if (cursor == (const unsigned char *)start && *cursor != '\0') {
        (void)tui_view_decode_codepoint(&cursor);
    }
    return (const char *)cursor;
}

static void draw_wrapped(Renderer *renderer, TuiRect area, const char *text, bool ascii,
                         RendererStyle style) {
    const char *cursor = text;
    for (size_t row = 0U; row < area.height && *cursor != '\0'; ++row) {
        while (*cursor == ' ') ++cursor;
        if (*cursor == '\0') break;
        if (row + 1U == area.height) {
            tui_view_put_truncated(renderer, area.x, area.y + row, cursor, area.width,
                                   ascii, style);
            break;
        }
        const char *end = wrapped_end(cursor, area.width);
        size_t bytes = (size_t)(end - cursor);
        while (bytes > 0U && cursor[bytes - 1U] == ' ') --bytes;
        char line[LOWTASK_DESCRIPTION_MAX + 1U];
        memcpy(line, cursor, bytes);
        line[bytes] = '\0';
        tui_view_put(renderer, area.x, area.y + row, line, area.width, style);
        cursor = end;
    }
}

static void draw_description(Renderer *renderer, const TuiLayout *layout,
                             const TuiViewState *view, const Task *task) {
    const TuiRect target = layout->description_target;
    if (target.width == 0U || target.height == 0U) return;
    const AppAction action = {.type = APP_ACTION_EDIT_DESCRIPTION, .task_id = task->id};
    const bool hovered = tui_view_action_equal(view->app->hovered_action, action);
    const bool pressed = tui_view_action_equal(view->app->pressed_action, action);
    const TuiColorToken background = pressed ? TUI_COLOR_PRESSED :
                                     (hovered ? TUI_COLOR_HOVER :
                                      (layout->inspector.width > 0U ? TUI_COLOR_RAISED :
                                                                    TUI_COLOR_PANEL));
    renderer_fill(renderer, target.x, target.y, target.width, target.height, ' ',
                  tui_view_style(TUI_COLOR_TEXT, background, RENDER_ATTR_NONE));
    const char *rail = view->ascii ? (pressed ? ">" : "|") : (pressed ? "▌" : "▏");
    tui_view_put(renderer, target.x, target.y, rail, 1U,
                 tui_view_style(hovered || pressed ? TUI_COLOR_ACCENT_STRONG :
                                                       TUI_COLOR_BORDER,
                                background, RENDER_ATTR_BOLD));
    if (target.height == 1U) {
        char summary[LOWTASK_DESCRIPTION_MAX + 32U];
        (void)snprintf(summary, sizeof(summary), "DESCRIPTION %s",
                       task->description == NULL ? "(none)" : task->description);
        tui_view_put_truncated(renderer, target.x + 2U, target.y, summary,
                               target.width > 2U ? target.width - 2U : 0U, view->ascii,
                               tui_view_style(TUI_COLOR_TEXT_MUTED, background,
                                              RENDER_ATTR_NONE));
        return;
    }
    tui_view_put(renderer, target.x + 2U, target.y, "DESCRIPTION",
                 target.width > 2U ? target.width - 2U : 0U,
                 tui_view_style(hovered ? TUI_COLOR_ACCENT : TUI_COLOR_TEXT_MUTED,
                                background, RENDER_ATTR_BOLD));
    if (target.width >= 18U) {
        tui_view_put(renderer, target.x + target.width - 4U, target.y, "EDIT", 4U,
                     tui_view_style(hovered || pressed ? TUI_COLOR_ACCENT_STRONG :
                                                           TUI_COLOR_TEXT_MUTED,
                                    background, RENDER_ATTR_BOLD));
    }
    const TuiRect body = {.x = target.x + 2U, .y = target.y + 1U,
                          .width = target.width > 2U ? target.width - 2U : 0U,
                          .height = target.height - 1U};
    const char *content = task->description;
    const bool empty = content == NULL;
    if (empty) content = view->ascii ? "(none) - select EDIT to add" :
                                      "(none) · select EDIT to add";
    draw_wrapped(renderer, body, content, view->ascii,
                 tui_view_style(empty ? TUI_COLOR_TEXT_MUTED : TUI_COLOR_TEXT,
                                background, empty ? RENDER_ATTR_DIM : RENDER_ATTR_NONE));
}

static void draw_inspector(Renderer *renderer, const TuiLayout *layout,
                           const TuiViewState *view, const Task *task) {
    const TuiRect inspector = layout->inspector;
    renderer_fill(renderer, inspector.x, inspector.y, inspector.width, inspector.height, ' ',
                  tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
    tui_view_put(renderer, inspector.x + 2U, inspector.y, "SELECTED TASK",
                 inspector.width - 4U,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
    if (inspector.height > 2U) {
        const size_t title_y = inspector.y + 2U;
        size_t title_rows = layout->description_target.height > 0U &&
                            layout->description_target.y > title_y ?
                            layout->description_target.y - title_y : 1U;
        if (title_rows > 3U) title_rows = 3U;
        draw_wrapped(renderer,
                     (TuiRect){.x = inspector.x + 2U, .y = title_y,
                               .width = inspector.width - 4U, .height = title_rows},
                     task->text, view->ascii,
                     tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
    }
    draw_description(renderer, layout, view, task);
    if (inspector.height < 10U) return;
    static const char *const priorities[] = {"", "LOW", "NORMAL", "HIGH", "URGENT"};
    char detail[64];
    (void)snprintf(detail, sizeof(detail), "PRIORITY  %s", priorities[task->priority]);
    tui_view_put(renderer, inspector.x + 2U, inspector.y + inspector.height - 3U,
                 detail, inspector.width - 4U,
                 tui_view_style(tui_view_priority_color(task->priority), TUI_COLOR_RAISED,
                                RENDER_ATTR_NONE));
    char date[32];
    TuiColorToken token;
    unsigned attributes;
    tui_view_date_label(view->app, task, 14U, view->ascii, date, &token, &attributes);
    (void)snprintf(detail, sizeof(detail), "DUE       %s",
                   date[0] == '\0' ? "UNSCHEDULED" : date);
    tui_view_put_truncated(renderer, inspector.x + 2U,
                           inspector.y + inspector.height - 2U, detail,
                           inspector.width - 4U, view->ascii,
                           tui_view_style(date[0] == '\0' ? TUI_COLOR_TEXT_MUTED : token,
                                          TUI_COLOR_RAISED, attributes));
}

void tui_view_draw_selected_context(Renderer *renderer, const TuiLayout *layout,
                                    const TuiViewState *view) {
    const Task *task = app_state_selected_task_const(view->app);
    if (task == NULL) return;
    if (layout->inspector.width > 0U) draw_inspector(renderer, layout, view, task);
    else draw_description(renderer, layout, view, task);
}

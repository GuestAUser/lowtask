#include "tui/view_common.h"
#include "tui/text_wrap.h"

#include <stdio.h>
#include <string.h>

static void draw_wrapped(Renderer *renderer, TuiRect area, const char *text, bool ascii,
                         RendererStyle style) {
    if (area.width == 0U) return;
    TuiTextWrap wrap;
    tui_text_wrap_init(&wrap, text, area.width);
    for (size_t row = 0U; row < area.height; ++row) {
        const char *remaining = tui_text_wrap_remaining(&wrap);
        if (*remaining == '\0') break;
        if (row + 1U == area.height) {
            tui_view_put_truncated(renderer, area.x, area.y + row, remaining, area.width,
                                   ascii, style);
            break;
        }
        TuiTextLine wrapped;
        if (!tui_text_wrap_next(&wrap, &wrapped)) break;
        char line[LOWTASK_DESCRIPTION_MAX + 1U];
        memcpy(line, wrapped.text, wrapped.bytes);
        line[wrapped.bytes] = '\0';
        tui_view_put(renderer, area.x, area.y + row, line, area.width, style);
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
                 tui_view_style(hovered ? TUI_COLOR_ACCENT : TUI_COLOR_DATE,
                                background, RENDER_ATTR_BOLD));
    if (target.width >= 20U) {
        const size_t button_x = target.x + target.width - 6U;
        const TuiColorToken button_background = pressed ? TUI_COLOR_PRESSED :
                                                (hovered ? TUI_COLOR_HOVER :
                                                 (layout->inspector.width > 0U ?
                                                  TUI_COLOR_PANEL : TUI_COLOR_RAISED));
        renderer_fill(renderer, button_x, target.y, 6U, 1U, ' ',
                      tui_view_style(TUI_COLOR_DATE, button_background, RENDER_ATTR_BOLD));
        tui_view_put(renderer, button_x, target.y, "[EDIT]", 6U,
                     tui_view_style(pressed ? TUI_COLOR_ACCENT_STRONG :
                                    (hovered ? TUI_COLOR_ACCENT : TUI_COLOR_DATE),
                                    button_background, RENDER_ATTR_BOLD));
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
                                background, RENDER_ATTR_NONE));
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

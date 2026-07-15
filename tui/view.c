#include "tui/view_common.h"

void tui_draw(Renderer *renderer, const TaskList *tasks, const TuiViewState *view) {
    if (renderer == NULL || tasks == NULL || view == NULL || view->app == NULL ||
        view->app->tasks == NULL || renderer->width == 0U || renderer->height == 0U ||
        view->mode < TUI_MODE_NORMAL || view->mode > TUI_MODE_HELP ||
        view->effect < TUI_EFFECT_NONE || view->effect > TUI_EFFECT_MOVE) return;
    renderer_begin(renderer, tui_view_color(TUI_COLOR_CANVAS));
    TuiLayout layout;
    if (!tui_layout_compute(renderer->width, renderer->height, view, &layout)) return;
    const size_t header_x = renderer->width >= TUI_STANDARD_COLUMNS ? 4U :
                            (renderer->width >= TUI_NARROW_COLUMNS ? 1U : 0U);
    tui_view_put(renderer, header_x, 0U, "lowtask", 7U,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_CANVAS, RENDER_ATTR_BOLD));
    if (renderer->width >= TUI_STANDARD_COLUMNS && layout.filter_target.x > header_x + 10U) {
        tui_view_put(renderer, header_x + 9U, 0U, "focus, without friction",
                     layout.filter_target.x - header_x - 9U,
                     tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_CANVAS,
                                    RENDER_ATTR_NONE));
    }
    tui_view_draw_header(renderer, &layout, view);
    if (renderer->height < 4U) {
        renderer_fill(renderer, 0U, renderer->height - 1U, renderer->width, 1U, ' ',
                      tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED,
                                     RENDER_ATTR_NONE));
        const bool help = view->app->mode == APP_MODE_HELP || view->mode == TUI_MODE_HELP;
        tui_view_put(renderer, 0U, renderer->height - 1U,
                     help ? "help needs 4 rows" : "more height required", renderer->width,
                     tui_view_style(TUI_COLOR_DANGER, TUI_COLOR_RAISED,
                                    RENDER_ATTR_BOLD));
        return;
    }
    tui_view_draw_tabs(renderer, &layout, view);
    if (layout.panel.width > 0U && layout.panel.height > 0U) {
        renderer_fill(renderer, layout.panel.x, layout.panel.y, layout.panel.width,
                      layout.panel.height, ' ',
                      tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_PANEL, RENDER_ATTR_NONE));
        if (layout.panel_framed) {
            tui_view_draw_box(renderer, layout.panel, view->ascii,
                              tui_view_style(TUI_COLOR_BORDER, TUI_COLOR_PANEL,
                                             RENDER_ATTR_NONE));
        }
        const size_t visible_count = app_state_visible_count(view->app);
        size_t used_rows = 0U;
        while (used_rows < layout.visible_rows) {
            const size_t display_index = layout.selection_pinned &&
                                         used_rows == layout.pinned_row ?
                                         layout.pinned_visible_index :
                                         layout.first_visible + used_rows;
            const AppDisplayRow display = app_state_display_row(view->app, display_index,
                                                                 layout.visible_rows);
            if (display.kind == APP_DISPLAY_ROW_NONE) break;
            if (display.kind == APP_DISPLAY_ROW_GROUP) {
                tui_view_draw_group(renderer, &layout, display.group, used_rows);
            } else {
                const Task *task = app_state_visible_task_const(view->app,
                                                               display.task_ordinal);
                if (task != NULL) {
                    tui_view_draw_task(renderer, &layout, view, task,
                                       display.task_ordinal, used_rows);
                }
            }
            ++used_rows;
        }
        if (visible_count == 0U) tui_view_draw_empty(renderer, &layout, view);
        else tui_view_draw_grid(renderer, &layout, view, used_rows);
        tui_view_draw_selected_context(renderer, &layout, view);
        tui_view_draw_drag_ghost(renderer, &layout, view);
    }
    tui_view_draw_modal(renderer, view);
    tui_view_draw_picker(renderer, &layout, view);
    tui_view_draw_help(renderer, &layout, view);
    tui_view_draw_status(renderer, &layout, view);
}

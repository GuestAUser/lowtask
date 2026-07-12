#include "tui/view_common.h"

#include <stdio.h>
#include <string.h>

static size_t tab_count(const AppState *state, AppTab tab) {
    return app_state_count_tasks(state->tasks, tab, state->priority_filter, state->today);
}

void tui_view_draw_tabs(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view) {
    const AppState *app = view->app;
    for (size_t index = 0U; index < APP_TAB_COUNT; ++index) {
        const AppTab tab = (AppTab)index;
        const TuiRect target = layout->tab_targets[index];
        if (target.width == 0U) continue;
        const AppAction action = {.type = APP_ACTION_SET_TAB, .tab = tab};
        const bool active = app->tab == tab;
        const bool drag_target = app->drag_active && app->drag_target_tab == tab;
        const bool drag_valid = drag_target && app->drag_target_valid;
        const bool hovered = tui_view_action_equal(app->hovered_action, action) || drag_target;
        const bool pressed = tui_view_action_equal(app->pressed_action, action);
        const TuiColorToken background = pressed ? TUI_COLOR_PRESSED :
                                         (active ? TUI_COLOR_RAISED :
                                         (hovered ? TUI_COLOR_HOVER : TUI_COLOR_CANVAS));
        const uint32_t background_rgb = tui_view_color(background);
        renderer_fill(renderer, target.x, target.y, target.width, 1U, ' ',
                      (RendererStyle){
                          .foreground = tui_view_color(active ? TUI_COLOR_TEXT :
                                                       TUI_COLOR_TEXT_MUTED),
                          .background = background_rgb,
                          .attributes = active ? RENDER_ATTR_BOLD : RENDER_ATTR_NONE,
                      });
        const bool minimal = renderer->width < TUI_MINIMAL_COLUMNS;
        const bool moving_rail = !minimal && view->effect == TUI_EFFECT_TAB &&
                                 app->previous_tab >= APP_TAB_ALL &&
                                 app->previous_tab < APP_TAB_COUNT &&
                                 layout->tab_targets[app->previous_tab].width > 0U;
        const bool emphasized = active || drag_valid;
        const char *rail = minimal || (active && moving_rail) ? "" :
                           (view->ascii ? (emphasized ? ">" : (hovered ? "|" : " ")) :
                                          (emphasized ? "▌" : (hovered ? "│" : " ")));
        uint32_t rail_rgb = tui_view_color(pressed ? TUI_COLOR_ACCENT_STRONG :
                                           (active || drag_valid ? TUI_COLOR_ACCENT :
                                                                   TUI_COLOR_BORDER));
        if (active && view->effect == TUI_EFFECT_TAB && !pressed) {
            const float tab_progress = animation_motion_progress(view->effect_progress);
            rail_rgb = color_blend(tui_view_color(TUI_COLOR_BORDER),
                                   tui_view_color(TUI_COLOR_ACCENT), tab_progress);
        }
        tui_view_put(renderer, target.x, target.y, rail, 1U,
                     (RendererStyle){.foreground = rail_rgb, .background = background_rgb,
                                     .attributes = RENDER_ATTR_BOLD});
        char label[48];
        if (minimal) {
            (void)snprintf(label, sizeof(label), "%s %zu", tui_view_tab_title(tab, false),
                           tab_count(app, tab));
        } else if (view->ascii && active && layout->tabs_show_counts) {
            (void)snprintf(label, sizeof(label), "[%s %zu]",
                           tui_view_tab_name(tab, layout->tabs_compact), tab_count(app, tab));
        } else if (layout->tabs_show_counts) {
            (void)snprintf(label, sizeof(label), "%s %zu",
                           tui_view_tab_name(tab, layout->tabs_compact), tab_count(app, tab));
        } else {
            (void)snprintf(label, sizeof(label), "%s",
                           layout->tabs_compact ? tui_view_tab_title(tab, true) :
                                                  tui_view_tab_name(tab, false));
        }
        const size_t inset = minimal ? 0U :
                             (renderer->width < TUI_NARROW_COLUMNS ||
                              (view->ascii && active) ? 1U : 2U);
        tui_view_put(renderer, target.x + inset, target.y, label,
                     target.width > inset ? target.width - inset : 0U,
                     (RendererStyle){
                         .foreground = tui_view_color(active ? TUI_COLOR_TEXT :
                                                      TUI_COLOR_TEXT_MUTED),
                         .background = background_rgb,
                         .attributes = active ? RENDER_ATTR_BOLD : RENDER_ATTR_NONE,
                     });
    }
    if (renderer->width >= TUI_MINIMAL_COLUMNS && view->effect == TUI_EFFECT_TAB &&
        app->previous_tab >= APP_TAB_ALL && app->previous_tab < APP_TAB_COUNT) {
        const TuiRect previous = layout->tab_targets[app->previous_tab];
        const TuiRect current = layout->tab_targets[app->tab];
        if (previous.width > 0U && current.width > 0U) {
            const float progress = animation_motion_progress(view->effect_progress);
            const float position = (float)previous.x +
                                   ((float)current.x - (float)previous.x) * progress;
            const size_t x = (size_t)(position + 0.5F);
            const RendererCell *cell = &renderer->back[current.y * renderer->width + x];
            tui_view_put(renderer, x, current.y, view->ascii ? ">" : "▌", 1U,
                         (RendererStyle){.foreground = tui_view_color(TUI_COLOR_ACCENT),
                                         .background = cell->background,
                                         .attributes = RENDER_ATTR_BOLD});
        }
    }
}

static const char *filter_name(AppPriorityFilter filter) {
    static const char *const names[] = {"Any", "Urgent", "High", "Normal", "Low"};
    return filter >= APP_PRIORITY_FILTER_ANY && filter < APP_PRIORITY_FILTER_COUNT ?
           names[filter] : "?";
}

static const char *sort_name(AppSort sort) {
    static const char *const names[] = {"Smart", "Created", "Due", "Priority"};
    return sort >= APP_SORT_SMART && sort < APP_SORT_COUNT ? names[sort] : "?";
}

static void draw_control(Renderer *renderer, TuiRect target, const char *label,
                         AppAction action, const AppState *app) {
    if (target.width == 0U) return;
    const bool hovered = tui_view_action_equal(app->hovered_action, action);
    const bool pressed = tui_view_action_equal(app->pressed_action, action);
    const TuiColorToken background = pressed ? TUI_COLOR_PRESSED :
                                     (hovered ? TUI_COLOR_HOVER : TUI_COLOR_CANVAS);
    renderer_fill(renderer, target.x, target.y, target.width, target.height, ' ',
                  tui_view_style(TUI_COLOR_TEXT_MUTED, background, RENDER_ATTR_NONE));
    const size_t label_width = tui_view_display_cells(label);
    const size_t x = target.x +
                     (target.width > label_width ? (target.width - label_width) / 2U : 0U);
    tui_view_put(renderer, x, target.y, label, target.width,
                 tui_view_style(pressed ? TUI_COLOR_ACCENT_STRONG :
                                (hovered ? TUI_COLOR_ACCENT : TUI_COLOR_TEXT_MUTED),
                                background,
                                pressed || hovered ? RENDER_ATTR_BOLD : RENDER_ATTR_NONE));
}

void tui_view_draw_header(Renderer *renderer, const TuiLayout *layout,
                          const TuiViewState *view) {
    char filter[32];
    char sort[32];
    if (renderer->width >= TUI_STANDARD_COLUMNS) {
        (void)snprintf(filter, sizeof(filter), "FILTER:%s",
                       filter_name(view->app->priority_filter));
        (void)snprintf(sort, sizeof(sort), "SORT:%s", sort_name(view->app->sort));
    } else {
        (void)snprintf(filter, sizeof(filter), "F:%c",
                       filter_name(view->app->priority_filter)[0]);
        (void)snprintf(sort, sizeof(sort), "S:%c", sort_name(view->app->sort)[0]);
    }
    draw_control(renderer, layout->filter_target, filter,
                 (AppAction){.type = APP_ACTION_CYCLE_PRIORITY_FILTER}, view->app);
    draw_control(renderer, layout->sort_target, sort,
                 (AppAction){.type = APP_ACTION_CYCLE_SORT}, view->app);
    draw_control(renderer, layout->help_target,
                 renderer->width >= TUI_STANDARD_COLUMNS ? "HELP" : "?",
                 (AppAction){.type = APP_ACTION_OPEN_HELP}, view->app);
    if (view->app->mode != APP_MODE_HELP && view->mode != TUI_MODE_HELP) return;
    renderer_fill(renderer, layout->help_target.x, layout->help_target.y,
                  layout->help_target.width, 1U, ' ',
                  tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_SELECTED, RENDER_ATTR_BOLD));
    const char *label = renderer->width >= TUI_STANDARD_COLUMNS ? "HELP" : "?";
    const size_t label_width = strlen(label);
    tui_view_put(renderer,
                 layout->help_target.x +
                     (layout->help_target.width > label_width ?
                      (layout->help_target.width - label_width) / 2U : 0U),
                 layout->help_target.y, label, layout->help_target.width,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_SELECTED, RENDER_ATTR_BOLD));
}

void tui_view_draw_selected_context(Renderer *renderer, const TuiLayout *layout,
                                    const TuiViewState *view) {
    const Task *task = app_state_selected_task_const(view->app);
    if (task == NULL) return;
    if (layout->inspector.width > 0U) {
        renderer_fill(renderer, layout->inspector.x, layout->inspector.y,
                      layout->inspector.width, layout->inspector.height, ' ',
                      tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED,
                                     RENDER_ATTR_NONE));
        tui_view_put(renderer, layout->inspector.x + 2U, layout->inspector.y, "SELECTED",
                     layout->inspector.width - 2U,
                     tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
        if (layout->inspector.height > 2U) {
            tui_view_put_truncated(renderer, layout->inspector.x + 2U,
                                   layout->inspector.y + 2U, task->text,
                                   layout->inspector.width - 4U, view->ascii,
                                   tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED,
                                                  RENDER_ATTR_BOLD));
        }
        if (layout->inspector.height > 4U) {
            static const char *const priorities[] = {"", "LOW", "NORMAL", "HIGH", "URGENT"};
            char detail[64];
            (void)snprintf(detail, sizeof(detail), "PRIORITY %s", priorities[task->priority]);
            tui_view_put(renderer, layout->inspector.x + 2U, layout->inspector.y + 4U,
                         detail, layout->inspector.width - 4U,
                         tui_view_style(tui_view_priority_color(task->priority), TUI_COLOR_RAISED,
                                        RENDER_ATTR_NONE));
        }
        if (layout->inspector.height > 6U) {
            char label[32];
            TuiColorToken token;
            unsigned attributes;
            tui_view_date_label(view->app, task, 16U, view->ascii, label, &token,
                                &attributes);
            tui_view_put(renderer, layout->inspector.x + 2U, layout->inspector.y + 6U,
                         label[0] == '\0' ? "unscheduled" : label,
                         layout->inspector.width - 4U,
                         tui_view_style(label[0] == '\0' ? TUI_COLOR_TEXT_MUTED : token,
                                        TUI_COLOR_RAISED, attributes));
        }
    } else if (layout->context.width > 0U) {
        char context[256];
        (void)snprintf(context, sizeof(context), " SELECTED: %.240s", task->text);
        tui_view_put_truncated(renderer, layout->context.x, layout->context.y, context,
                               layout->context.width, view->ascii,
                               tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_PANEL,
                                              RENDER_ATTR_DIM));
    }
}

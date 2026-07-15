#include "tui/view_common.h"

#include <stdio.h>
#include <string.h>

void tui_view_draw_drag_ghost(Renderer *renderer, const TuiLayout *layout,
                              const TuiViewState *view) {
    const AppState *app = view->app;
    if (!app->drag_active || layout->rows.width == 0U || layout->rows.height == 0U) return;
    size_t row = app->drag_current_row <= layout->rows.y ? 0U :
                 (size_t)app->drag_current_row - layout->rows.y;
    if (row >= layout->rows.height) row = layout->rows.height - 1U;
    if (app->drag_current_row > app->drag_press_row && row + 1U < layout->rows.height) ++row;
    else if (app->drag_current_row < app->drag_press_row && row > 0U) --row;
    const float lift = animation_reduced_motion_enabled() || app->drag_lift_duration <= 0.0F ?
                       1.0F :
                       animation_clamp_progress(app->drag_lift_elapsed /
                                                app->drag_lift_duration);
    const uint32_t background = color_blend(tui_view_color(TUI_COLOR_PRESSED),
                                            tui_view_color(TUI_COLOR_SELECTED),
                                            ease_out_cubic(lift));
    renderer_fill(renderer, layout->rows.x, layout->rows.y + row, layout->rows.width, 1U, ' ',
                  (RendererStyle){.foreground = tui_view_color(TUI_COLOR_TEXT),
                                  .background = background,
                                  .attributes = RENDER_ATTR_BOLD});
    const char *cue = view->ascii ? "[DRAG] " : "▌ DRAG ";
    const size_t cue_width = 7U;
    tui_view_put(renderer, layout->rows.x, layout->rows.y + row, cue, cue_width,
                 (RendererStyle){.foreground = tui_view_color(TUI_COLOR_ACCENT_STRONG),
                                 .background = background,
                                 .attributes = RENDER_ATTR_BOLD});
    if (layout->rows.width > cue_width) {
        tui_view_put_truncated(
            renderer, layout->rows.x + cue_width, layout->rows.y + row,
            app->drag_source_title, layout->rows.width - cue_width, view->ascii,
            (RendererStyle){.foreground = tui_view_color(TUI_COLOR_TEXT),
                            .background = background,
                            .attributes = RENDER_ATTR_BOLD});
    }
}

void tui_view_draw_picker(Renderer *renderer, const TuiLayout *layout,
                          const TuiViewState *view) {
    const bool priority = view->app->mode == APP_MODE_PRIORITY_PICKER ||
                          view->mode == TUI_MODE_PRIORITY_PICKER;
    const bool schedule = view->app->mode == APP_MODE_SCHEDULE_PICKER ||
                          view->mode == TUI_MODE_SCHEDULE_PICKER;
    if (!priority && !schedule) return;
    const size_t count = priority ? 4U : 5U;
    const bool framed = layout->picker.height == count + 2U;
    renderer_fill(renderer, layout->picker.x, layout->picker.y, layout->picker.width,
                  layout->picker.height, ' ',
                  tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
    if (framed) {
        tui_view_draw_box(renderer, layout->picker, view->ascii,
                          tui_view_style(TUI_COLOR_BORDER, TUI_COLOR_RAISED,
                                         RENDER_ATTR_NONE));
    }
    const char *title = priority ? " PRIORITY " : " SCHEDULE ";
    tui_view_put(renderer, layout->picker.x + (framed ? 2U : 1U), layout->picker.y, title,
                 layout->picker.width,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
    static const char *const priority_names[] = {
        "Urgent [4]", "High [3]", "Normal [2]", "Low [1]",
    };
    static const char *const schedule_names[] = {
        "Today [1]", "Tomorrow [2]", "+7 Days [3]", "Custom [4]", "Clear [5]",
    };
    for (size_t index = 0U; index < count; ++index) {
        const TuiRect option = layout->picker_options[index];
        if (option.width == 0U) continue;
        const AppAction action = priority ?
            (AppAction){.type = APP_ACTION_APPLY_OPTION,
                .option = {.kind = APP_OPTION_PRIORITY,
                           .value = (unsigned int)TASK_PRIORITY_URGENT - (unsigned int)index}} :
            (AppAction){.type = APP_ACTION_APPLY_OPTION,
                .option = {.kind = APP_OPTION_SCHEDULE, .value = (unsigned int)index + 1U}};
        const bool focused = index == view->app->focused_option;
        const bool hovered = tui_view_action_equal(view->app->hovered_action, action);
        const bool pressed = tui_view_action_equal(view->app->pressed_action, action);
        const TuiColorToken background = pressed ? TUI_COLOR_PRESSED :
                                         (focused ? TUI_COLOR_SELECTED :
                                         (hovered ? TUI_COLOR_HOVER : TUI_COLOR_RAISED));
        renderer_fill(renderer, option.x, option.y, option.width, 1U, ' ',
                      tui_view_style(TUI_COLOR_TEXT, background,
                                     focused ? RENDER_ATTR_BOLD : 0U));
        tui_view_put(renderer, option.x, option.y,
                     view->ascii ? (focused ? ">" : (hovered ? "|" : " ")) :
                                   (focused ? "▌" : (hovered ? "│" : " ")), 1U,
                     tui_view_style(focused || pressed ? TUI_COLOR_ACCENT_STRONG :
                                                         TUI_COLOR_BORDER,
                                    background, RENDER_ATTR_BOLD));
        tui_view_put(renderer, option.x + 2U, option.y,
                     priority ? priority_names[index] : schedule_names[index],
                     option.width > 2U ? option.width - 2U : 0U,
                     tui_view_style(TUI_COLOR_TEXT, background,
                                    focused ? RENDER_ATTR_BOLD : 0U));
    }
}

static bool status_is_error(const char *message) {
    return message != NULL &&
           (strstr(message, "must be") != NULL || strstr(message, "cannot") != NULL ||
            strstr(message, "unable") != NULL || strstr(message, "failed") != NULL ||
            strstr(message, "valid date") != NULL ||
            strstr(message, "limit reached") != NULL);
}

void tui_view_draw_status(Renderer *renderer, const TuiLayout *layout,
                          const TuiViewState *view) {
    renderer_fill(renderer, layout->status.x, layout->status.y, layout->status.width, 1U, ' ',
                  tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED,
                                 RENDER_ATTR_NONE));
    char summary[192];
    if (view->app->mode == APP_MODE_HELP || view->mode == TUI_MODE_HELP) {
        if (renderer->height <= 3U) {
            tui_view_put(renderer, 0U, layout->status.y, "help needs 4 rows",
                         renderer->width,
                         tui_view_style(TUI_COLOR_DANGER, TUI_COLOR_RAISED,
                                        RENDER_ATTR_BOLD));
            return;
        }
        const size_t total = view->app->help_line_count;
        const size_t first = total == 0U ? 0U : view->app->help_scroll + 1U;
        size_t last = view->app->help_scroll + view->app->help_page_rows;
        if (last > total) last = total;
        (void)snprintf(summary, sizeof(summary), "HELP %zu-%zu/%zu", first, last, total);
        tui_view_put(renderer, 0U, layout->status.y, summary, renderer->width,
                     tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
        return;
    }
    const size_t count = app_state_visible_count(view->app);
    const char *message = view->status != NULL ? view->status : "ready";
    const RendererStyle message_style = status_is_error(message) ?
        tui_view_style(TUI_COLOR_DANGER, TUI_COLOR_RAISED, RENDER_ATTR_BOLD) :
        tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED, RENDER_ATTR_NONE);
    const char *separator = view->ascii ? " | " : " · ";
    const char *noun = count == 1U ? "task" : "tasks";
    if (renderer->width < TUI_NARROW_COLUMNS) {
        (void)snprintf(summary, sizeof(summary), "%s", message);
    } else {
        (void)snprintf(summary, sizeof(summary), " %s%s%zu %s%s%s", message, separator,
                       count, noun, separator, tui_view_tab_name(view->app->tab, false));
    }
    tui_view_put(renderer, 0U, layout->status.y, summary, renderer->width, message_style);
    if (renderer->width < TUI_STANDARD_COLUMNS) return;
    const char *keys = view->app->mode == APP_MODE_EDIT ?
        "Left/Right move  Tab fields  Enter save  Esc cancel " :
        (view->app->mode == APP_MODE_ADD || view->app->mode == APP_MODE_SCHEDULE ?
         "Left/Right move  Enter save  Esc cancel " :
         "a add  e edit  s date  space done  Tab views  q quit ");
    const size_t length = strlen(keys);
    const size_t summary_length = tui_view_display_cells(summary);
    if (length < renderer->width && renderer->width - length > summary_length) {
        tui_view_put(renderer, renderer->width - length, layout->status.y, keys, length,
                     tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED,
                                    RENDER_ATTR_DIM));
    }
}

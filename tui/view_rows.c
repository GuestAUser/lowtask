#include "tui/view_common.h"

#include "core/date.h"

#include <stdio.h>
#include <string.h>

TuiColorToken tui_view_priority_color(TaskPriority priority) {
    if (priority == TASK_PRIORITY_URGENT) return TUI_COLOR_URGENT;
    if (priority == TASK_PRIORITY_HIGH) return TUI_COLOR_DANGER;
    if (priority == TASK_PRIORITY_LOW) return TUI_COLOR_INFO;
    return TUI_COLOR_WARNING;
}

static const char *month_name(unsigned month) {
    static const char *const names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    return month >= 1U && month <= 12U ? names[month - 1U] : "???";
}

void tui_view_date_label(const AppState *app, const Task *task, size_t width, bool ascii,
                         char output[32], TuiColorToken *token, unsigned *attributes) {
    output[0] = '\0';
    *token = TUI_COLOR_DATE;
    *attributes = RENDER_ATTR_NONE;
    if (task->due_date[0] == '\0') return;
    const int order = app->today[0] == '\0' ? 1 : strcmp(task->due_date, app->today);
    const unsigned month = (unsigned)(task->due_date[5] - '0') * 10U +
                           (unsigned)(task->due_date[6] - '0');
    const unsigned day = (unsigned)(task->due_date[8] - '0') * 10U +
                         (unsigned)(task->due_date[9] - '0');
    char short_date[16];
    (void)snprintf(short_date, sizeof(short_date), "%s %02u", month_name(month), day);
    if (task->completed) {
        if (width >= 16U) {
            (void)snprintf(output, 32U, ascii ? "done | %s" : "done · %s", short_date);
        } else if (width >= 11U) {
            (void)snprintf(output, 32U, "done %s", short_date);
        } else {
            (void)snprintf(output, 32U, "%s", short_date);
        }
        *token = TUI_COLOR_TEXT_MUTED;
    } else if (order < 0) {
        if (width >= 16U) {
            (void)snprintf(output, 32U, ascii ? "overdue | %s" : "overdue · %s", short_date);
        } else if (width >= 8U) {
            (void)snprintf(output, 32U, "! %s", short_date);
        } else {
            (void)snprintf(output, 32U, "%s", short_date);
        }
        *token = TUI_COLOR_DANGER;
        *attributes = RENDER_ATTR_BOLD;
    } else if (order == 0) {
        (void)snprintf(output, 32U, "today");
        *token = TUI_COLOR_ACCENT;
        *attributes = RENDER_ATTR_BOLD;
    } else if (width <= 6U) {
        (void)snprintf(output, 32U, "%s", short_date);
    } else if (date_is_next_day(app->today, task->due_date)) {
        (void)snprintf(output, 32U, "tomorrow");
    } else if (width < 10U) {
        (void)snprintf(output, 32U, "%s", short_date);
    } else if (strncmp(task->due_date, app->today, 4U) != 0) {
        (void)snprintf(output, 32U, "%s", task->due_date);
    } else {
        (void)snprintf(output, 32U, "%s", short_date);
    }
}

void tui_view_draw_task(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view, const Task *task,
                        size_t visible_index, size_t row) {
    const AppState *app = view->app;
    const bool selected = visible_index == app->selected;
    const bool hovered = app->hovered_action.task_id == task->id &&
                         app->hovered_action.type != APP_ACTION_NONE;
    const bool pressed = app->pressed_action.task_id == task->id &&
                         app->pressed_action.type != APP_ACTION_NONE;
    uint32_t background_rgb = color_blend(tui_view_color(TUI_COLOR_PANEL),
                                          tui_view_color(TUI_COLOR_ROW_ALT),
                                          (float)(row % 8U) / 28.0F);
    TuiColorToken background_token = TUI_COLOR_PANEL;
    if (pressed) background_token = TUI_COLOR_PRESSED;
    else if (selected) background_token = TUI_COLOR_SELECTED;
    else if (hovered) background_token = TUI_COLOR_HOVER;
    if (pressed || selected || hovered) background_rgb = tui_view_color(background_token);
    const size_t shift = tui_task_row_shift(view, task->id, visible_index);
    unsigned task_attributes = task->completed ? RENDER_ATTR_DIM | RENDER_ATTR_STRIKE :
                                                RENDER_ATTR_NONE;
    if (app->drag_active && app->drag_task_id == task->id) task_attributes |= RENDER_ATTR_DIM;
    if (app->effect_task_id == task->id && view->effect != TUI_EFFECT_NONE &&
        view->effect != TUI_EFFECT_TAB) {
        const float progress = animation_motion_progress(view->effect_progress);
        if (view->effect == TUI_EFFECT_ADD) {
            background_rgb = color_blend(background_rgb, tui_view_color(TUI_COLOR_ACCENT),
                                         (1.0F - progress) * 0.32F);
        } else if (view->effect == TUI_EFFECT_EDIT) {
            background_rgb = color_blend(background_rgb, tui_view_color(TUI_COLOR_ACCENT),
                                         (1.0F - progress) * 0.20F);
        } else if (view->effect == TUI_EFFECT_COMPLETE) {
            const float raw = animation_clamp_progress(view->effect_progress);
            const float pulse = raw < 0.5F ? ease_out_cubic(raw * 2.0F) :
                                             ease_out_cubic((1.0F - raw) * 2.0F);
            background_rgb = color_blend(background_rgb, tui_view_color(TUI_COLOR_ACCENT),
                                         pulse * 0.28F);
        } else if (view->effect == TUI_EFFECT_DELETE) {
            background_rgb = color_blend(background_rgb, tui_view_color(TUI_COLOR_DANGER),
                                         progress * 0.42F);
            task_attributes |= RENDER_ATTR_DIM;
        } else if (view->effect == TUI_EFFECT_MOVE) {
            background_rgb = color_blend(background_rgb, tui_view_color(TUI_COLOR_ACCENT),
                                         (1.0F - progress) * 0.28F);
        }
    }
    const RendererStyle row_style = {
        .foreground = tui_view_color(TUI_COLOR_TEXT),
        .background = background_rgb,
        .attributes = RENDER_ATTR_NONE,
    };
    renderer_fill(renderer, layout->rows.x, layout->rows.y + row, layout->rows.width, 1U,
                  ' ', row_style);
    if (shift >= layout->rows.width) return;
    size_t x = layout->rows.x + shift;
    const char *rail = view->ascii ? (selected ? ">" : (hovered ? "|" : " ")) :
                                     (selected ? "▌" : (hovered ? "│" : " "));
    tui_view_put(renderer, x, layout->rows.y + row, rail, 1U,
                 (RendererStyle){
                     .foreground = tui_view_color(pressed || (selected && hovered) ?
                                                  TUI_COLOR_ACCENT_STRONG :
                                                  (selected ? TUI_COLOR_ACCENT : TUI_COLOR_BORDER)),
                     .background = background_rgb,
                     .attributes = RENDER_ATTR_BOLD,
                 });
    ++x;
    const char *check = view->ascii ? (task->completed ? "[x]" : "[ ]") :
                                      (task->completed ? "✓" : "○");
    const size_t check_width = view->ascii ? 3U : 1U;
    tui_view_put(renderer, x, layout->rows.y + row, check, check_width,
                 (RendererStyle){.foreground = tui_view_color(TUI_COLOR_ACCENT),
                                 .background = background_rgb,
                                 .attributes = task_attributes});
    x = layout->rows.x + shift + (view->ascii ? 5U : 4U);
    const char *priority = view->ascii ?
        (task->priority == TASK_PRIORITY_URGENT ? "U" :
         (task->priority == TASK_PRIORITY_HIGH ? "!" :
          (task->priority == TASK_PRIORITY_LOW ? "v" : "-"))) :
        (task->priority == TASK_PRIORITY_URGENT ? "◆" :
         (task->priority == TASK_PRIORITY_HIGH ? "▲" :
          (task->priority == TASK_PRIORITY_LOW ? "▽" : "•")));
    tui_view_put(renderer, x, layout->rows.y + row, priority, 1U,
                 (RendererStyle){.foreground = tui_view_color(
                                     tui_view_priority_color(task->priority)),
                                 .background = background_rgb,
                                 .attributes = task_attributes});
    const size_t date_width = tui_date_column_width(renderer->width);
    x += date_width == 0U ? (renderer->width < TUI_MINIMAL_COLUMNS ? 2U : 4U) : 2U;
    if (date_width == 0U && x < layout->rows.x + layout->rows.width) {
        const char *marker = task->completed ? "x" :
                             (task->due_date[0] == '\0' ? (view->ascii ? "." : "·") :
                             (strcmp(task->due_date, app->today) < 0 ? "!" :
                             (strcmp(task->due_date, app->today) == 0 ? "=" : ">")));
        const TuiColorToken marker_color = marker[0] == '!' ? TUI_COLOR_DANGER :
                                            (marker[0] == '=' ? TUI_COLOR_ACCENT :
                                            (task->completed || task->due_date[0] == '\0' ?
                                             TUI_COLOR_TEXT_MUTED : TUI_COLOR_DATE));
        tui_view_put(renderer, x++, layout->rows.y + row, marker, 1U,
                     (RendererStyle){.foreground = tui_view_color(marker_color),
                                     .background = background_rgb,
                                     .attributes = task_attributes});
        if (x < layout->rows.x + layout->rows.width) ++x;
    }
    const size_t row_end = layout->rows.x + layout->rows.width;
    size_t title_width = 0U;
    tui_task_title_bounds(view, layout, task->id, visible_index, &x, &title_width);
    if (title_width > 0U) {
        tui_view_put_truncated(
            renderer, x, layout->rows.y + row, task->text, title_width, view->ascii,
            (RendererStyle){.foreground = tui_view_color(task->completed ?
                                                         TUI_COLOR_TEXT_MUTED : TUI_COLOR_TEXT),
                            .background = background_rgb,
                            .attributes = task_attributes |
                                          (selected ? RENDER_ATTR_BOLD : 0U)});
    }
    if (date_width > 0U) {
        char label[32];
        TuiColorToken date_color;
        unsigned date_attributes;
        tui_view_date_label(app, task, date_width, view->ascii, label, &date_color,
                            &date_attributes);
        const size_t length = tui_view_display_cells(label);
        const size_t date_x = row_end - date_width +
                              (length < date_width ? date_width - length : 0U);
        tui_view_put(renderer, date_x, layout->rows.y + row, label, date_width,
                     (RendererStyle){.foreground = tui_view_color(date_color),
                                     .background = background_rgb,
                                     .attributes = date_attributes |
                                                   (task->completed ? RENDER_ATTR_DIM : 0U)});
    }
}

static const char *group_name(AppGroup group) {
    static const char *const names[] = {
        "", "OVERDUE", "DUE TODAY", "TOMORROW", "NEXT 7 DAYS", "LATER",
    };
    return group >= APP_GROUP_NONE && group <= APP_GROUP_LATER ? names[group] : "";
}

void tui_view_draw_group(Renderer *renderer, const TuiLayout *layout,
                         AppGroup group, size_t row) {
    renderer_fill(renderer, layout->rows.x, layout->rows.y + row, layout->rows.width, 1U, ' ',
                  tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_PANEL, RENDER_ATTR_DIM));
    tui_view_put(renderer, layout->rows.x + (layout->rows.width > 2U ? 2U : 0U),
                 layout->rows.y + row, group_name(group), layout->rows.width,
                 tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_PANEL, RENDER_ATTR_BOLD));
}

void tui_view_draw_empty(Renderer *renderer, const TuiLayout *layout,
                         const TuiViewState *view) {
    static const char *const messages[] = {
        "No tasks yet", "Nothing due today", "Nothing upcoming", "Nothing completed",
    };
    if (layout->rows.height == 0U) return;
    const size_t group_height = layout->rows.height > 1U ? 2U : 1U;
    const size_t group_y = layout->rows.y + (layout->rows.height - group_height) / 2U;
    const char *message = messages[view->app->tab];
    const size_t length = strlen(message);
    const size_t x = layout->rows.x +
                     (layout->rows.width > length ? (layout->rows.width - length) / 2U : 0U);
    tui_view_put(renderer, x, group_y, message, layout->rows.width,
                 tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_PANEL, RENDER_ATTR_NONE));
    if (layout->rows.height <= 1U) return;
    const char *add = "a  add a task";
    const size_t add_length = strlen(add);
    const size_t add_x = layout->rows.x +
                         (layout->rows.width > add_length ?
                          (layout->rows.width - add_length) / 2U : 0U);
    tui_view_put(renderer, add_x, group_y + 1U, add, layout->rows.width,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_PANEL, RENDER_ATTR_DIM));
}

void tui_view_draw_grid(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view, size_t used_rows) {
    if (view->ascii || !renderer->truecolor || renderer->width < TUI_WIDE_COLUMNS ||
        animation_reduced_motion_enabled()) return;
    for (size_t row = used_rows + 3U; row < layout->rows.height; row += 4U) {
        for (size_t column = 7U; column < layout->rows.width; column += 8U) {
            tui_view_put(renderer, layout->rows.x + column, layout->rows.y + row, "·", 1U,
                         tui_view_style(TUI_COLOR_GRID, TUI_COLOR_PANEL, RENDER_ATTR_DIM));
        }
    }
}

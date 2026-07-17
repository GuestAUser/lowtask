#include "tui/view.h"

#include "tui/animation.h"
#include "tui/text_wrap.h"

#include <math.h>
#include <string.h>

size_t tui_date_column_width(size_t terminal_width) {
    if (terminal_width >= TUI_WIDE_COLUMNS) return 16U;
    if (terminal_width >= 52U) return 11U;
    return 0U;
}

static size_t decimal_width(size_t value) {
    size_t width = 1U;
    while (value >= 10U) {
        value /= 10U;
        ++width;
    }
    return width;
}

static const char *tab_label(AppTab tab, bool compact) {
    static const char *const full[] = {"ALL", "TODAY", "UPCOMING", "COMPLETED"};
    static const char *const short_names[] = {"All", "Today", "Soon", "Done"};
    return compact ? short_names[tab] : full[tab];
}

static size_t tab_width(const TuiViewState *view, AppTab tab, bool compact, bool counts) {
    size_t width = strlen(tab_label(tab, compact)) + 3U;
    if (counts) {
        width += decimal_width(app_state_count_tasks(view->app->tasks, tab,
                               view->app->priority_filter, view->app->today)) + 1U;
    }
    return width;
}

static size_t tab_total(const TuiViewState *view, bool compact, bool counts) {
    size_t total = APP_TAB_COUNT - 1U;
    for (size_t index = 0U; index < APP_TAB_COUNT; ++index) {
        total += tab_width(view, (AppTab)index, compact, counts);
    }
    return total;
}

static void inspector_description(TuiLayout *layout) {
    if (layout->inspector.width <= 4U || layout->inspector.height <= 3U) {
        return;
    }

    const size_t start = layout->inspector.height >= 8U ? 5U :
                         (layout->inspector.height >= 7U ? 4U : 3U);
    const size_t reserved = layout->inspector.height >= 10U ? 3U : 0U;
    const size_t end = layout->inspector.height - reserved;
    if (end <= start) {
        return;
    }

    layout->description_target = (TuiRect){.x = layout->inspector.x + 2U,
        .y = layout->inspector.y + start, .width = layout->inspector.width - 4U,
        .height = end - start};
}

static void standard_description(const Task *selected, TuiLayout *layout) {
    if (!layout->panel_framed || layout->rows.height < 4U) {
        return;
    }

    const size_t content_width = layout->rows.width > 2U ? layout->rows.width - 2U : 1U;
    size_t body_rows = selected->description == NULL ? 1U :
                       tui_text_wrap_count(selected->description, content_width);
    if (body_rows == 0U) {
        body_rows = 1U;
    }

    size_t detail_height = body_rows + 1U;
    const size_t maximum = layout->rows.height / 2U;
    if (detail_height > maximum) {
        detail_height = maximum;
    }
    if (detail_height < 2U) {
        detail_height = 2U;
    }

    const size_t separator = layout->rows.height > detail_height + 2U ? 1U : 0U;
    layout->description_target = (TuiRect){.x = layout->rows.x, .y = layout->rows.y,
        .width = layout->rows.width, .height = detail_height};
    layout->context = (TuiRect){.x = layout->rows.x, .y = layout->rows.y,
        .width = layout->rows.width, .height = detail_height + separator};
    layout->rows.y += layout->context.height;
    layout->rows.height -= layout->context.height;
}

/*
 * Detail regions are carved out before visible-row and scroll calculations.
 * The resulting rectangles are the shared contract for drawing and hit
 * testing, so the visible affordance and its interactive target cannot drift
 * apart as the terminal moves between standard and wide layouts.
 */
static void apply_detail_geometry(size_t width, const TuiViewState *view, TuiLayout *layout) {
    const Task *selected = app_state_selected_task_const(view->app);
    if (selected == NULL || selected->id == view->app->pending_delete_id ||
        view->app->mode != APP_MODE_NORMAL || view->mode != TUI_MODE_NORMAL) {
        return;
    }

    if (layout->inspector.width > 0U) {
        inspector_description(layout);
    } else if (width >= TUI_STANDARD_COLUMNS && width < TUI_WIDE_COLUMNS) {
        standard_description(selected, layout);
    }
}

static void compute_header_targets(size_t width, TuiLayout *layout) {
    const size_t help_width = width >= TUI_STANDARD_COLUMNS ? 6U : 3U;
    if (width == 0U) return;
    layout->help_target = (TuiRect){.x = width > help_width ? width - help_width : 0U,
                                    .width = width < help_width ? width : help_width, .height = 1U};
    if (width < TUI_MINIMAL_COLUMNS) return;
    const size_t filter_width = width >= TUI_STANDARD_COLUMNS ? 15U : 5U;
    const size_t sort_width = width >= TUI_STANDARD_COLUMNS ? 15U : 5U;
    size_t right = layout->help_target.x;
    if (right >= sort_width) {
        right -= sort_width;
        layout->sort_target = (TuiRect){.x = right, .width = sort_width, .height = 1U};
    }
    if (right >= filter_width) {
        right -= filter_width;
        layout->filter_target = (TuiRect){.x = right, .width = filter_width, .height = 1U};
    }
}

static void compute_tabs(size_t width, size_t height, const TuiViewState *view, TuiLayout *layout) {
    if (height < 4U) return;
    layout->tabs = (TuiRect){.y = 1U, .width = width, .height = 1U};
    const bool active_only = width < TUI_MINIMAL_COLUMNS || height <= 7U;
    const size_t margin = width >= TUI_STANDARD_COLUMNS ? 4U :
                          (width >= TUI_NARROW_COLUMNS ? 1U : 0U);
    const size_t available = width > margin ? width - margin : 0U;
    bool compact = width < TUI_NARROW_COLUMNS;
    bool counts = width >= TUI_STANDARD_COLUMNS;
    if (!active_only && width >= TUI_STANDARD_COLUMNS && tab_total(view, false, true) > available) {
        counts = false;
        if (tab_total(view, false, false) > available) compact = true;
    }
    if (!active_only && width >= TUI_NARROW_COLUMNS && width < TUI_STANDARD_COLUMNS) {
        counts = false;
        compact = false;
    }
    layout->tabs_compact = compact;
    layout->tabs_show_counts = counts;
    size_t x = margin;
    const size_t first = active_only ? (size_t)view->app->tab : 0U;
    const size_t end = active_only ? first + 1U : APP_TAB_COUNT;
    for (size_t index = first; index < end && x < width; ++index) {
        size_t target = width >= TUI_MINIMAL_COLUMNS && width < TUI_NARROW_COLUMNS ?
                        strlen(tab_label((AppTab)index, true)) + 1U :
                        tab_width(view, (AppTab)index, compact,
                                  counts || width < TUI_MINIMAL_COLUMNS);
        if (target > width - x) target = width - x;
        layout->tab_targets[index] = (TuiRect){.x = x, .y = 1U, .width = target, .height = 1U};
        x += target;
        if (index + 1U < end && x < width) ++x;
    }
}

static void compute_overlay_geometry(size_t width, size_t height, const TuiViewState *view,
                                     TuiLayout *layout) {
    if (view->app->mode == APP_MODE_HELP || view->mode == TUI_MODE_HELP) {
        size_t overlay_width = width;
        if (width >= TUI_WIDE_COLUMNS) overlay_width = width - 8U < 80U ? width - 8U : 80U;
        else if (width >= TUI_STANDARD_COLUMNS) overlay_width = width - 8U < 60U ? width - 8U : 60U;
        else if (width >= TUI_NARROW_COLUMNS && width > 2U) overlay_width = width - 2U;
        const size_t overlay_x = (width - overlay_width) / 2U;
        const bool framed = height >= 9U && width >= TUI_STANDARD_COLUMNS;
        const size_t overlay_y = framed ? 1U : 0U;
        const size_t overlay_height = height > 1U ? height - 1U : height;
        layout->help_overlay = (TuiRect){.x = overlay_x, .y = overlay_y,
                                         .width = overlay_width,
                                         .height = overlay_height > overlay_y ? overlay_height - overlay_y : 0U};
        const size_t inset = framed ? 1U : 0U;
        const size_t title_y = overlay_y + inset;
        if (overlay_width > inset * 2U) {
            layout->help_close = (TuiRect){.x = overlay_x + overlay_width - inset - 3U,
                                            .y = title_y, .width = 3U, .height = 1U};
        }
        size_t page_rows = 0U;
        if (height == 4U) page_rows = 1U;
        else if (height >= 5U && height <= 8U) page_rows = height - 2U;
        else if (height >= 9U) page_rows = height - 6U;
        layout->help_body = (TuiRect){.x = overlay_x + inset, .y = title_y + 1U,
                                      .width = overlay_width > inset * 2U ? overlay_width - inset * 2U : 0U,
                                      .height = page_rows};
        return;
    }
    const bool priority = view->app->mode == APP_MODE_PRIORITY_PICKER ||
                          view->mode == TUI_MODE_PRIORITY_PICKER;
    const bool schedule = view->app->mode == APP_MODE_SCHEDULE_PICKER ||
                          view->mode == TUI_MODE_SCHEDULE_PICKER;
    if (!priority && !schedule) return;
    const size_t count = priority ? 4U : 5U;
    const bool framed = width >= 12U && height >= count + 3U;
    size_t picker_width = width > TUI_MODAL_MAX_COLUMNS ? TUI_MODAL_MAX_COLUMNS : width;
    if (framed && picker_width > 4U) picker_width -= 4U;
    const size_t picker_height = framed ? count + 2U : (height > 1U ? height - 1U : 0U);
    const size_t picker_x = (width - picker_width) / 2U;
    const size_t picker_y = framed && height > picker_height ? (height - 1U - picker_height) / 2U : 0U;
    layout->picker = (TuiRect){.x = picker_x, .y = picker_y,
                               .width = picker_width, .height = picker_height};
    if (framed) {
        for (size_t index = 0U; index < count; ++index) {
            layout->picker_options[index] = (TuiRect){.x = picker_x + 1U,
                .y = picker_y + 1U + index, .width = picker_width - 2U, .height = 1U};
        }
    } else if (picker_height > 1U) {
        const size_t visible = picker_height - 1U < count ? picker_height - 1U : count;
        size_t first = view->app->focused_option;
        if (first + visible > count) first = count - visible;
        for (size_t index = 0U; index < visible; ++index) {
            layout->picker_options[first + index] = (TuiRect){.x = picker_x,
                .y = picker_y + 1U + index, .width = picker_width, .height = 1U};
        }
    }
}

bool tui_layout_compute(size_t width, size_t height, const TuiViewState *view, TuiLayout *layout) {
    if (view == NULL || view->app == NULL || view->app->tasks == NULL || layout == NULL ||
        width == 0U || height == 0U || view->app->tab < APP_TAB_ALL ||
        view->app->tab >= APP_TAB_COUNT) return false;
    *layout = (TuiLayout){0};
    layout->header = (TuiRect){.width = width, .height = 1U};
    layout->status = (TuiRect){.y = height - 1U, .width = width, .height = 1U};
    compute_header_targets(width, layout);
    compute_tabs(width, height, view, layout);
    if (height >= 4U) {
        const size_t margin = width >= TUI_STANDARD_COLUMNS ? 4U :
                              (width >= TUI_NARROW_COLUMNS ? 1U : 0U);
        const size_t base_width = width > margin * 2U ? width - margin * 2U : width;
        const float panel_progress = animation_reduced_motion_enabled() ? 1.0F :
                                     animation_clamp_progress(view->panel_progress);
        const size_t offset = (size_t)((1.0F - panel_progress) * (float)(base_width / 6U));
        const size_t panel_x = margin + offset;
        const size_t panel_width = panel_x < width && width - panel_x > margin ?
                                   width - panel_x - margin : 0U;
        layout->panel = (TuiRect){.x = panel_x, .y = 2U, .width = panel_width,
                                  .height = height - 3U};
        layout->panel_framed = height >= 9U && width >= TUI_MINIMAL_COLUMNS && panel_width >= 4U;
        if (layout->panel_framed) {
            layout->rows = (TuiRect){.x = panel_x + 1U, .y = 3U,
                .width = panel_width - 2U, .height = layout->panel.height - 2U};
        } else {
            layout->rows = layout->panel;
        }
        if (height <= 5U && layout->rows.height > 1U) layout->rows.height = 1U;
        if (width >= TUI_WIDE_COLUMNS && layout->rows.width >= 86U) {
            layout->inspector = (TuiRect){.x = layout->rows.x + layout->rows.width - 28U,
                .y = layout->rows.y, .width = 28U, .height = layout->rows.height};
            --layout->inspector.x;
            layout->rows.width -= 29U;
        }
        apply_detail_geometry(width, view, layout);
        layout->visible_rows = layout->rows.height;
        const size_t count = app_state_display_row_count(view->app, layout->visible_rows);
        const size_t maximum = count > layout->visible_rows ? count - layout->visible_rows : 0U;
        size_t requested = 0U;
        if (isfinite(view->scroll) && view->scroll > 0.0F) {
            requested = view->scroll >= (float)maximum ? maximum : (size_t)(view->scroll + 0.5F);
        }
        layout->first_visible = app_state_display_window_start(view->app, requested,
                                                                layout->visible_rows);
        const size_t selected_row = tui_task_display_row(view->app, view->app->selected_task_id,
                                                          layout->visible_rows);
        if (selected_row != SIZE_MAX && layout->visible_rows > 0U) {
            if (selected_row < layout->first_visible) {
                layout->selection_pinned = true;
                layout->pinned_row = 0U;
                layout->pinned_visible_index = selected_row;
            } else if (selected_row >= layout->first_visible + layout->visible_rows) {
                layout->selection_pinned = true;
                layout->pinned_row = layout->visible_rows - 1U;
                layout->pinned_visible_index = selected_row;
            }
        }
    }
    compute_overlay_geometry(width, height, view, layout);
    return true;
}

size_t tui_task_row_shift(const TuiViewState *view, uint64_t task_id, size_t visible_index) {
    if (view == NULL || view->app == NULL || view->effect <= TUI_EFFECT_NONE ||
        view->effect > TUI_EFFECT_MOVE || animation_reduced_motion_enabled()) return 0U;
    const bool affected = view->app->effect_task_id != 0U ?
                          view->app->effect_task_id == task_id : view->effect_index == visible_index;
    if (!affected) return 0U;
    const float progress = ease_out_cubic(view->effect_progress);
    if (view->effect == TUI_EFFECT_ADD && view->app->effect_duration >= 0.27F) {
        return (size_t)((1.0F - progress) * (float)TUI_ADD_SHIFT_CELLS);
    }
    if (view->effect == TUI_EFFECT_DELETE) {
        return (size_t)(progress * (float)TUI_DELETE_SHIFT_CELLS);
    }
    return 0U;
}

#include "tui/detail_geometry.h"

#include "tui/text_wrap.h"

static void inspector_description(TuiLayout *layout) {
    if (layout->inspector.width <= 4U || layout->inspector.height <= 3U) return;
    const size_t start = layout->inspector.height >= 8U ? 5U :
                         (layout->inspector.height >= 7U ? 4U : 3U);
    const size_t reserved = layout->inspector.height >= 10U ? 3U : 0U;
    const size_t end = layout->inspector.height - reserved;
    if (end <= start) return;
    layout->description_target = (TuiRect){.x = layout->inspector.x + 2U,
        .y = layout->inspector.y + start, .width = layout->inspector.width - 4U,
        .height = end - start};
}

static void standard_description(const Task *selected, TuiLayout *layout) {
    if (!layout->panel_framed || layout->rows.height < 4U) return;
    const size_t content_width = layout->rows.width > 2U ? layout->rows.width - 2U : 1U;
    size_t body_rows = selected->description == NULL ? 1U :
                       tui_text_wrap_count(selected->description, content_width);
    if (body_rows == 0U) body_rows = 1U;
    size_t detail_height = body_rows + 1U;
    const size_t maximum = layout->rows.height / 2U;
    if (detail_height > maximum) detail_height = maximum;
    if (detail_height < 2U) detail_height = 2U;
    const size_t separator = layout->rows.height > detail_height + 2U ? 1U : 0U;
    layout->description_target = (TuiRect){.x = layout->rows.x, .y = layout->rows.y,
        .width = layout->rows.width, .height = detail_height};
    layout->context = (TuiRect){.x = layout->rows.x, .y = layout->rows.y,
        .width = layout->rows.width, .height = detail_height + separator};
    layout->rows.y += layout->context.height;
    layout->rows.height -= layout->context.height;
}

void tui_detail_geometry_apply(size_t width, const TuiViewState *view, TuiLayout *layout) {
    const Task *selected = app_state_selected_task_const(view->app);
    if (selected == NULL || selected->id == view->app->pending_delete_id ||
        view->app->mode != APP_MODE_NORMAL || view->mode != TUI_MODE_NORMAL) return;
    if (layout->inspector.width > 0U) inspector_description(layout);
    else if (width >= TUI_STANDARD_COLUMNS && width < TUI_WIDE_COLUMNS) {
        standard_description(selected, layout);
    }
}

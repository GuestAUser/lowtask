#include "tui/view_common.h"

#include <string.h>

static AppTextInput fallback_input(const char *value) {
    AppTextInput input = {0};
    if (value == NULL) return input;
    input.length = strlen(value);
    if (input.length > LOWTASK_TEXT_MAX) input.length = LOWTASK_TEXT_MAX;
    memcpy(input.value, value, input.length);
    input.value[input.length] = '\0';
    input.cursor = input.length;
    return input;
}

static void draw_input_line(Renderer *renderer, size_t x, size_t y, size_t width,
                            const char *label, const AppTextInput *input, bool focused,
                            bool ascii) {
    if (width == 0U || y >= renderer->height) return;
    const size_t label_width = strlen(label);
    tui_view_put(renderer, x, y, label, width,
                 tui_view_style(focused ? TUI_COLOR_ACCENT_STRONG : TUI_COLOR_TEXT_MUTED,
                                TUI_COLOR_RAISED, focused ? RENDER_ATTR_BOLD : 0U));
    if (width <= label_width) return;
    const size_t field_width = width - label_width;
    if (!focused) {
        tui_view_put_truncated(renderer, x + label_width, y, input->value, field_width,
                               ascii, tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED,
                                                     RENDER_ATTR_NONE));
        return;
    }
    const AppTextViewport viewport = app_text_input_viewport(input, field_width);
    char visible[LOWTASK_TEXT_MAX + 1U];
    const size_t prefix_bytes = input->cursor - viewport.start;
    memcpy(visible, input->value + viewport.start, prefix_bytes);
    visible[prefix_bytes] = '\0';
    tui_view_put(renderer, x + label_width, y, visible, viewport.caret_cell,
                 tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
    if (viewport.draw_caret && viewport.caret_cell < field_width) {
        tui_view_put(renderer, x + label_width + viewport.caret_cell, y,
                     ascii ? "_" : "▏", 1U,
                     tui_view_style(TUI_COLOR_ACCENT_STRONG, TUI_COLOR_RAISED,
                                    RENDER_ATTR_BOLD));
    }
    const size_t suffix_bytes = viewport.end - input->cursor;
    memcpy(visible, input->value + input->cursor, suffix_bytes);
    visible[suffix_bytes] = '\0';
    const size_t suffix_x = viewport.caret_cell + (viewport.draw_caret ? 1U : 0U);
    if (suffix_x < field_width) {
        tui_view_put(renderer, x + label_width + suffix_x, y, visible,
                     field_width - suffix_x,
                     tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
    }
}

static void draw_field_label(Renderer *renderer, size_t x, size_t y, size_t width,
                             const char *label, bool focused) {
    tui_view_put(renderer, x, y, label, width,
                 tui_view_style(focused ? TUI_COLOR_ACCENT : TUI_COLOR_TEXT_MUTED,
                                TUI_COLOR_RAISED, focused ? RENDER_ATTR_BOLD : RENDER_ATTR_DIM));
}

void tui_view_draw_modal(Renderer *renderer, const TuiViewState *view) {
    if (view->mode == TUI_MODE_NORMAL || view->mode == TUI_MODE_PRIORITY_PICKER ||
        view->mode == TUI_MODE_SCHEDULE_PICKER || view->mode == TUI_MODE_HELP ||
        renderer->height < 4U) return;
    AppTextInput fallback = fallback_input(view->input);
    const bool live_input = view->app->mode == APP_MODE_ADD ||
                            view->app->mode == APP_MODE_EDIT ||
                            view->app->mode == APP_MODE_SCHEDULE;
    const AppTextInput *title = live_input ? &view->app->input : &fallback;
    const AppTextInput *description = &view->app->description_input;
    const bool editing = view->mode == TUI_MODE_EDIT;
    if (renderer->width < 12U || renderer->height < (editing ? 9U : 5U)) {
        renderer_fill(renderer, 0U, renderer->height - 2U, renderer->width, 1U, ' ',
                       tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
        const bool description_focus = editing &&
                                       view->app->edit_field == APP_EDIT_DESCRIPTION;
        draw_input_line(renderer, 0U, renderer->height - 2U, renderer->width,
                        description_focus ? "D> " : "T> ",
                        description_focus ? description : title, true, view->ascii);
        return;
    }
    const size_t width = renderer->width > TUI_STANDARD_COLUMNS ? TUI_MODAL_MAX_COLUMNS :
                         renderer->width - 4U;
    const size_t modal_height = editing ? 7U : 3U;
    const size_t desired_y = renderer->height / 2U - 1U;
    const size_t maximum_y = renderer->height - 1U - modal_height;
    const TuiRect modal = {.x = (renderer->width - width) / 2U,
                           .y = desired_y < maximum_y ? desired_y : maximum_y,
                           .width = width, .height = modal_height};
    renderer_fill(renderer, modal.x, modal.y, modal.width, modal.height, ' ',
                  tui_view_style(TUI_COLOR_TEXT, TUI_COLOR_RAISED, RENDER_ATTR_NONE));
    tui_view_draw_box(renderer, modal, view->ascii,
                      tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
    const char *heading = view->mode == TUI_MODE_ADD ? " ADD TASK " :
                          (view->mode == TUI_MODE_EDIT ? " EDIT TASK " : " SCHEDULE TASK ");
    tui_view_put(renderer, modal.x + 2U, modal.y, heading, modal.width - 4U,
                 tui_view_style(TUI_COLOR_ACCENT, TUI_COLOR_RAISED, RENDER_ATTR_BOLD));
    const size_t content_width = modal.width > 4U ? modal.width - 4U : 0U;
    if (!editing) {
        draw_input_line(renderer, modal.x + 2U, modal.y + 1U, content_width,
                        "", title, true, view->ascii);
        return;
    }
    const bool title_focus = view->app->edit_field == APP_EDIT_TITLE;
    draw_field_label(renderer, modal.x + 2U, modal.y + 1U, content_width,
                     view->ascii ? "TITLE / REQUIRED" : "TITLE · REQUIRED", title_focus);
    draw_input_line(renderer, modal.x + 2U, modal.y + 2U, content_width,
                    title_focus ? "> " : "  ", title, title_focus, view->ascii);
    draw_field_label(renderer, modal.x + 2U, modal.y + 3U, content_width,
                     view->ascii ? "DESCRIPTION / OPTIONAL" : "DESCRIPTION · OPTIONAL",
                     !title_focus);
    draw_input_line(renderer, modal.x + 2U, modal.y + 4U, content_width,
                    title_focus ? "  " : "> ", description, !title_focus, view->ascii);
    tui_view_put(renderer, modal.x + 2U, modal.y + 5U,
                 "Tab fields  Enter save  Esc cancel", content_width,
                 tui_view_style(TUI_COLOR_TEXT_MUTED, TUI_COLOR_RAISED, RENDER_ATTR_DIM));
}

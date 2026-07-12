#include "input/controller_help.h"

#include "input/controller_internal.h"

void controller_help_open(AppState *state) {
    state->help_saved_selected_task_id = state->selected_task_id;
    state->help_saved_selected = state->selected;
    state->help_saved_tab = state->tab;
    state->help_saved_priority_filter = state->priority_filter;
    state->help_saved_sort = state->sort;
    state->help_saved_list_scroll = state->list_scroll;
    state->help_saved_context = true;
    state->help_scroll = 0U;
    state->mode = APP_MODE_HELP;
    controller_clear_pointer(state);
    controller_set_status(state, "help");
}

void controller_help_close(AppState *state) {
    if (state->mode != APP_MODE_HELP) return;
    state->mode = APP_MODE_NORMAL;
    if (state->help_saved_context) {
        state->tab = state->help_saved_tab;
        state->priority_filter = state->help_saved_priority_filter;
        state->sort = state->help_saved_sort;
        state->selected_task_id = state->help_saved_selected_task_id;
        state->selected = state->help_saved_selected;
        state->list_scroll = state->help_saved_list_scroll;
        (void)app_state_refresh(state);
    }
    state->help_saved_context = false;
    controller_clear_pointer(state);
    controller_set_status(state, "help closed");
}

static void move(AppState *state, int direction, size_t amount) {
    const size_t maximum = state->help_line_count > state->help_page_rows ?
                            state->help_line_count - state->help_page_rows : 0U;
    if (direction < 0) {
        state->help_scroll = amount < state->help_scroll ? state->help_scroll - amount : 0U;
    } else {
        const size_t remaining = maximum - state->help_scroll;
        state->help_scroll += amount < remaining ? amount : remaining;
    }
}

void controller_help_handle(AppState *state, InputEvent event) {
    const uint32_t character = event.type == INPUT_KEY_CHARACTER ? event.codepoint : 0U;
    if (event.type == INPUT_KEY_ESCAPE || character == '?') {
        controller_help_close(state);
    } else if (event.type == INPUT_KEY_UP || character == 'k') {
        move(state, -1, 1U);
    } else if (event.type == INPUT_KEY_DOWN || character == 'j') {
        move(state, 1, 1U);
    } else if (event.type == INPUT_KEY_PAGE_UP) {
        move(state, -1, state->help_page_rows);
    } else if (event.type == INPUT_KEY_PAGE_DOWN) {
        move(state, 1, state->help_page_rows);
    } else if (event.type == INPUT_KEY_HOME) {
        state->help_scroll = 0U;
    } else if (event.type == INPUT_KEY_END) {
        state->help_scroll = state->help_line_count > state->help_page_rows ?
                              state->help_line_count - state->help_page_rows : 0U;
    } else if (event.type == INPUT_KEY_MOUSE && event.mouse_action == INPUT_MOUSE_WHEEL) {
        if (event.mouse_button == INPUT_MOUSE_BUTTON_WHEEL_UP) move(state, -1, 3U);
        if (event.mouse_button == INPUT_MOUSE_BUTTON_WHEEL_DOWN) move(state, 1, 3U);
    } else {
        controller_set_status(state, "close help first");
    }
}

#include "core/state.h"
#include "core/task.h"
#include "tui/color.h"
#include "tui/render.h"
#include "tui/view.h"

#include <assert.h>
#include <float.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool screen_contains(const Renderer *renderer, const char *needle) {
    char row[2048];
    for (size_t y = 0U; y < renderer->height; ++y) {
        size_t used = 0U;
        for (size_t x = 0U; x < renderer->width && used + 5U < sizeof(row); ++x) {
            const RendererCell *cell = &renderer->back[y * renderer->width + x];
            if (cell->glyph_length > 0U) {
                memcpy(row + used, cell->glyph, cell->glyph_length);
                used += cell->glyph_length;
            }
        }
        row[used] = '\0';
        if (strstr(row, needle) != NULL) return true;
    }
    return false;
}

static bool screen_is_ascii(const Renderer *renderer) {
    for (size_t index = 0U; index < renderer->width * renderer->height; ++index) {
        const RendererCell *cell = &renderer->back[index];
        for (size_t byte = 0U; byte < cell->glyph_length; ++byte) {
            if ((unsigned char)cell->glyph[byte] >= 0x80U) return false;
        }
    }
    return true;
}

static bool screen_has_foreground(const Renderer *renderer, uint32_t foreground) {
    for (size_t index = 0U; index < renderer->width * renderer->height; ++index) {
        if (renderer->back[index].foreground == foreground) return true;
    }
    return false;
}

static uint64_t screen_hash(const Renderer *renderer) {
    const unsigned char *bytes = (const unsigned char *)renderer->back;
    const size_t length = renderer->width * renderer->height * sizeof(*renderer->back);
    uint64_t hash = 1469598103934665603ULL;
    for (size_t index = 0U; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static size_t glyph_x(const Renderer *renderer, size_t row, const char *glyph) {
    for (size_t column = 0U; column < renderer->width; ++column) {
        const RendererCell *cell = &renderer->back[row * renderer->width + column];
        if (strcmp(cell->glyph, glyph) == 0) return column;
    }
    return SIZE_MAX;
}

static size_t glyph_count_on_row(const Renderer *renderer, size_t row, const char *glyph) {
    size_t count = 0U;
    for (size_t column = 0U; column < renderer->width; ++column) {
        if (strcmp(renderer->back[row * renderer->width + column].glyph, glyph) == 0) ++count;
    }
    return count;
}

static size_t text_row(const Renderer *renderer, const char *needle) {
    char row[2048];
    for (size_t y = 0U; y < renderer->height; ++y) {
        size_t used = 0U;
        for (size_t x = 0U; x < renderer->width && used + 17U < sizeof(row); ++x) {
            const RendererCell *cell = &renderer->back[y * renderer->width + x];
            if (cell->glyph_length > 0U) {
                memcpy(row + used, cell->glyph, cell->glyph_length);
                used += cell->glyph_length;
            }
        }
        row[used] = '\0';
        if (strstr(row, needle) != NULL) return y;
    }
    return SIZE_MAX;
}

static uint64_t add_task(TaskList *tasks, const char *text, TaskPriority priority,
                         const char *due_date, bool completed) {
    uint64_t id = 0U;
    assert(task_list_add(tasks, text, priority, &id));
    if (due_date != NULL) assert(task_list_set_due_date(tasks, id, due_date));
    if (completed) assert(task_list_toggle_complete(tasks, id));
    return id;
}

static AppAction find_action(size_t width, size_t height, const TuiViewState *view,
                             AppActionType type, uint64_t task_id,
                             AppTab tab) {
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = 0U; column < width; ++column) {
            const AppAction action = tui_hit_test(width, height, column, row, view).action;
            if (action.type == type && action.task_id == task_id &&
                (type != APP_ACTION_SET_TAB || action.tab == tab)) {
                return action;
            }
        }
    }
    return (AppAction){0};
}

static void test_matrix_tabs_dates_and_hits(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t overdue = add_task(&tasks, "overdue deploy", TASK_PRIORITY_HIGH,
                                      "2026-07-10", false);
    (void)add_task(&tasks, "today review", TASK_PRIORITY_NORMAL, "2026-07-11", false);
    const uint64_t future = add_task(&tasks, "future plan", TASK_PRIORITY_LOW,
                                     "2026-07-12", false);
    (void)add_task(&tasks, "unscheduled note", TASK_PRIORITY_NORMAL, NULL, false);
    (void)add_task(&tasks, "completed item", TASK_PRIORITY_LOW, "2026-07-09", true);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    Renderer renderer;
    assert(renderer_init(&renderer, 96U, 24U, true));
    TuiViewState view = {
        .app = &state, .selected = 0U, .scroll = 0.0F, .panel_progress = 1.0F,
        .mode = TUI_MODE_NORMAL, .status = state.status,
    };

    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "lowtask"));
    assert(screen_contains(&renderer, "ALL"));
    assert(screen_contains(&renderer, "TODAY"));
    assert(screen_contains(&renderer, "UPCOMING"));
    assert(screen_contains(&renderer, "COMPLETED"));
    assert(screen_contains(&renderer, "overdue"));
    assert(screen_contains(&renderer, "today"));
    assert(find_action(96U, 24U, &view, APP_ACTION_SET_TAB, 0U,
                       APP_TAB_UPCOMING).type == APP_ACTION_SET_TAB);
    assert(find_action(96U, 24U, &view, APP_ACTION_TOGGLE_TASK, overdue,
                       APP_TAB_ALL).type == APP_ACTION_TOGGLE_TASK);
    assert(find_action(96U, 24U, &view, APP_ACTION_EDIT_SCHEDULE, overdue,
                       APP_TAB_ALL).type == APP_ACTION_EDIT_SCHEDULE);

    app_state_set_tab(&state, APP_TAB_UPCOMING);
    view.effect = TUI_EFFECT_TAB;
    view.effect_progress = 0.0F;
    tui_draw(&renderer, &tasks, &view);
    const uint64_t tab_start_hash = screen_hash(&renderer);
    view.effect_progress = 0.5F;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_hash(&renderer) != tab_start_hash);
    assert(screen_contains(&renderer, "future plan"));
    assert(!screen_contains(&renderer, "overdue deploy"));
    assert(find_action(96U, 24U, &view, APP_ACTION_SELECT_TASK, future,
                       APP_TAB_ALL).type == APP_ACTION_SELECT_TASK);

    view.ascii = true;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_is_ascii(&renderer));
    assert(screen_contains(&renderer, "[UPCOMING 1]"));
    assert(renderer_resize(&renderer, 24U, 8U));
    view.ascii = false;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "lowtask"));
    assert(screen_contains(&renderer, "All"));
    assert(screen_contains(&renderer, "Today"));
    assert(screen_contains(&renderer, "Soon"));
    assert(screen_contains(&renderer, "Done"));
    TuiLayout layout;
    assert(tui_layout_compute(24U, 8U, &view, &layout));
    assert(layout.visible_rows > 0U);
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_design_tokens_responsive_fallbacks_and_dates(void) {
    assert(color_token_rgb(TUI_COLOR_CANVAS) == 0x010201U);
    assert(color_token_rgb(TUI_COLOR_PANEL) == 0x030604U);
    assert(color_token_rgb(TUI_COLOR_RAISED) == 0x040806U);
    assert(color_token_rgb(TUI_COLOR_ROW_ALT) == 0x030704U);
    assert(color_token_rgb(TUI_COLOR_HOVER) == 0x051109U);
    assert(color_token_rgb(TUI_COLOR_SELECTED) == 0x06180eU);
    assert(color_token_rgb(TUI_COLOR_PRESSED) == 0x041109U);
    assert(color_token_rgb(TUI_COLOR_ACCENT) == 0x4ade80U);
    assert(color_token_xterm(TUI_COLOR_SELECTED) == 23U);
    assert(color_token_xterm(TUI_COLOR_DANGER) == 210U);
    assert(color_blend(0x123456U, 0xffffffU, NAN) == 0x123456U);

    TaskList tasks;
    task_list_init(&tasks);
    (void)add_task(&tasks, "overdue", TASK_PRIORITY_HIGH, "2026-07-10", false);
    (void)add_task(&tasks, "tomorrow task", TASK_PRIORITY_NORMAL, "2026-07-12", false);
    (void)add_task(&tasks, "finished", TASK_PRIORITY_LOW, "2026-07-09", true);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    Renderer renderer;
    assert(renderer_init(&renderer, 112U, 16U, true));
    TuiViewState view = {
        .app = &state, .panel_progress = 1.0F, .mode = TUI_MODE_NORMAL,
        .status = state.status,
    };
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "tomorrow"));
    assert(screen_contains(&renderer, "done"));
    assert(screen_has_foreground(&renderer, color_token_rgb(TUI_COLOR_GRID)));
    assert(setenv("LOWTASK_REDUCE_MOTION", "1", 1) == 0);
    tui_draw(&renderer, &tasks, &view);
    assert(!screen_has_foreground(&renderer, color_token_rgb(TUI_COLOR_GRID)));
    assert(unsetenv("LOWTASK_REDUCE_MOTION") == 0);

    view.ascii = true;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_is_ascii(&renderer));

    assert(renderer_resize(&renderer, 30U, 8U));
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "Soon"));
    assert(screen_contains(&renderer, "!"));

    assert(renderer_resize(&renderer, 20U, 5U));
    app_state_set_tab(&state, APP_TAB_ALL);
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "All 3"));
    assert(screen_contains(&renderer, "overdue"));
    assert(!screen_contains(&renderer, "tomorrow task"));
    assert(!screen_contains(&renderer, "Today"));
    assert(!screen_contains(&renderer, "+"));

    (void)snprintf(state.status, sizeof(state.status), "date must be YYYY-MM-DD");
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "date must be YYYY"));
    assert(!screen_contains(&renderer, "3 tasks"));
    const RendererCell *error_status = &renderer.back[(renderer.height - 1U) * renderer.width];
    assert(error_status->foreground == color_token_rgb(TUI_COLOR_DANGER));
    assert((error_status->attributes & RENDER_ATTR_BOLD) != 0U);

    assert(renderer_resize(&renderer, 20U, 3U));
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "more height required"));
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_hit_padding_empty_add_and_pointer_precedence(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t task_id = add_task(&tasks, "target", TASK_PRIORITY_NORMAL,
                                      "2026-07-12", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    TuiViewState view = {.app = &state, .panel_progress = 1.0F};
    assert(tui_date_column_width(39U) == 0U);
    assert(tui_date_column_width(40U) == 0U);
    assert(tui_date_column_width(51U) == 0U);
    assert(tui_date_column_width(52U) == 11U);
    assert(!tui_layout_compute(96U, 16U, &(TuiViewState){0}, &(TuiLayout){0}));
    AppState invalid_state;
    assert(app_state_init(&invalid_state, &tasks));
    invalid_state.tab = APP_TAB_COUNT;
    TuiViewState invalid_view = view;
    invalid_view.app = &invalid_state;

    TuiLayout tab_gap_layout;
    assert(tui_layout_compute(64U, 12U, &view, &tab_gap_layout));
    const TuiRect first_tab = tab_gap_layout.tab_targets[APP_TAB_ALL];
    assert(first_tab.width > 0U);
    const size_t gap_x = first_tab.x + first_tab.width;
    assert(tab_gap_layout.tab_targets[APP_TAB_TODAY].x == gap_x + 1U);
    assert(tui_hit_test(64U, 12U, gap_x, first_tab.y, &view).kind == TUI_HIT_NONE);
    assert(!tui_layout_compute(96U, 16U, &invalid_view, &(TuiLayout){0}));
    app_state_dispose(&invalid_state);
    assert(app_state_init(&invalid_state, &tasks));
    invalid_state.tasks = NULL;
    invalid_view.app = &invalid_state;
    assert(!tui_layout_compute(96U, 16U, &invalid_view, &(TuiLayout){0}));
    view.scroll = FLT_MAX;
    TuiLayout huge_scroll_layout;
    assert(tui_layout_compute(96U, 16U, &view, &huge_scroll_layout));
    assert(huge_scroll_layout.first_visible == 0U);
    view.scroll = 0.0F;
    TuiLayout layout;
    assert(tui_layout_compute(96U, 16U, &view, &layout));
    TuiHit hit = tui_hit_test(96U, 16U, layout.rows.x + 1U, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_CHECK && hit.action.type == APP_ACTION_TOGGLE_TASK);
    hit = tui_hit_test(96U, 16U, layout.rows.x + 3U, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_CHECK && hit.action.task_id == task_id);
    hit = tui_hit_test(96U, 16U, layout.rows.x + 4U, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_PRIORITY && hit.action.type == APP_ACTION_OPEN_PRIORITY_PICKER);
    hit = tui_hit_test(96U, 16U, layout.rows.x + 6U, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_PRIORITY && hit.action.task_id == task_id);

    state.effect = APP_EFFECT_ADD;
    state.effect_task_id = task_id;
    state.effect_duration = 0.28F;
    state.effect_elapsed = 0.0F;
    view.effect = TUI_EFFECT_ADD;
    view.effect_progress = 0.0F;
    hit = tui_hit_test(96U, 16U, layout.rows.x + 5U, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_CHECK && hit.action.type == APP_ACTION_TOGGLE_TASK);
    state.effect = APP_EFFECT_NONE;
    state.effect_task_id = 0U;
    view.effect = TUI_EFFECT_NONE;

    Renderer renderer;
    assert(renderer_init(&renderer, 80U, 12U, true));
    assert(tui_layout_compute(80U, 12U, &view, &layout));
    state.hovered_action = (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = task_id};
    tui_draw(&renderer, &tasks, &view);
    const RendererCell *selected = &renderer.back[layout.rows.y * renderer.width + layout.rows.x + 7U];
    assert(selected->background == color_token_rgb(TUI_COLOR_SELECTED));
    assert((selected->attributes & RENDER_ATTR_BOLD) != 0U);
    state.pressed_action = state.hovered_action;
    tui_draw(&renderer, &tasks, &view);
    const RendererCell *pressed = &renderer.back[layout.rows.y * renderer.width + layout.rows.x + 7U];
    assert(pressed->background == color_token_rgb(TUI_COLOR_PRESSED));

    state.pressed_action = (AppAction){0};
    state.hovered_action = (AppAction){0};
    state.effect = APP_EFFECT_ADD;
    state.effect_task_id = task_id;
    state.effect_duration = 0.28F;
    state.effect_elapsed = 0.14F;
    view.effect = TUI_EFFECT_ADD;
    view.effect_progress = 0.5F;
    const size_t shifted_check = layout.rows.x + 1U + tui_task_row_shift(&view, task_id, 0U);
    hit = tui_hit_test(80U, 12U, shifted_check, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_CHECK && hit.action.task_id == task_id);
    state.pending_delete_id = task_id;
    state.effect = APP_EFFECT_DELETE;
    view.effect = TUI_EFFECT_DELETE;
    hit = tui_hit_test(80U, 12U, shifted_check, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_NONE && hit.action.type == APP_ACTION_NONE);

    task_list_free(&tasks);
    task_list_init(&tasks);
    state.tasks = &tasks;
    state.selected = 0U;
    state.hovered_action = (AppAction){0};
    state.pressed_action = (AppAction){0};
    assert(app_state_refresh(&state));
    assert(tui_layout_compute(80U, 12U, &view, &layout));
    const size_t empty_group_y = layout.rows.y + (layout.rows.height - 2U) / 2U;
    hit = tui_hit_test(80U, 12U, layout.rows.x + layout.rows.width / 2U,
                       empty_group_y + 1U, &view);
    assert(hit.kind == TUI_HIT_ADD && hit.action.type == APP_ACTION_ADD_TASK);
    state.pending_delete_id = 0U;
    state.effect = APP_EFFECT_NONE;
    view.effect = TUI_EFFECT_NONE;
    assert(tui_layout_compute(20U, 5U, &view, &layout));
    hit = tui_hit_test(20U, 5U, layout.rows.x, layout.rows.y, &view);
    assert(hit.kind == TUI_HIT_NONE && hit.action.type == APP_ACTION_NONE);
    renderer_free(&renderer);
    app_state_dispose(&invalid_state);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_modal_cjk_cursor_and_meaningful_motion(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t task_id = add_task(&tasks, "animated", TASK_PRIORITY_NORMAL, NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    Renderer renderer;
    assert(renderer_init(&renderer, 80U, 12U, true));
    TuiViewState view = {
        .app = &state, .panel_progress = 1.0F, .mode = TUI_MODE_EDIT,
        .input = "中a", .effect = TUI_EFFECT_ADD,
    };
    tui_draw(&renderer, &tasks, &view);
    const size_t cursor_x = glyph_x(&renderer, renderer.height / 2U, "▏");
    const size_t input_x = glyph_x(&renderer, renderer.height / 2U, "中");
    assert(cursor_x == input_x + 3U);
    view.input = "がa";
    tui_draw(&renderer, &tasks, &view);
    const size_t combined_x = glyph_x(&renderer, renderer.height / 2U, "が");
    assert(combined_x != SIZE_MAX);
    assert(glyph_x(&renderer, renderer.height / 2U, "▏") == combined_x + 3U);
    view.input = "一〪a";
    tui_draw(&renderer, &tasks, &view);
    const size_t tone_x = glyph_x(&renderer, renderer.height / 2U, "一〪");
    assert(tone_x != SIZE_MAX);
    assert(glyph_x(&renderer, renderer.height / 2U, "▏") == tone_x + 3U);
    char long_input[80];
    memset(long_input, 'x', sizeof(long_input));
    memcpy(long_input + sizeof(long_input) - 4U, "END", 4U);
    view.input = long_input;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "END"));
    assert(glyph_x(&renderer, renderer.height / 2U, "▏") != SIZE_MAX);
    view.input = "中a";
    TuiViewState malformed = view;
    const char c1_control[] = {'A', (char)0xc2, (char)0x9b, 'B', '\0'};
    malformed.input = c1_control;
    tui_draw(&renderer, &tasks, &malformed);
    assert(screen_contains(&renderer, "A?B"));
    const char c0_control[] = {'A', 0x1b, '[', '2', 'J', 'B', '\0'};
    malformed.input = c0_control;
    tui_draw(&renderer, &tasks, &malformed);
    assert(screen_contains(&renderer, "A?[2JB"));
    const char malformed_byte[] = {'A', (char)0x80, 'B', '\0'};
    malformed.input = malformed_byte;
    tui_draw(&renderer, &tasks, &malformed);
    assert(screen_contains(&renderer, "A?B"));
    const char overlong_escape[] = {'A', (char)0xe0, (char)0x80, (char)0x9b, 'B', '\0'};
    malformed.input = overlong_escape;
    tui_draw(&renderer, &tasks, &malformed);
    assert(screen_contains(&renderer, "A?B"));
    const char truncated[] = {(char)0xf0, '\0'};
    malformed.input = truncated;
    tui_draw(&renderer, &tasks, &malformed);
    assert(tui_hit_test(80U, 12U, 1U, 1U, &view).kind == TUI_HIT_NONE);

    state.effect = APP_EFFECT_ADD;
    state.effect_task_id = task_id;
    state.effect_duration = 0.28F;
    state.effect_elapsed = 0.0F;
    view.mode = TUI_MODE_NORMAL;
    view.input = NULL;
    view.effect = TUI_EFFECT_ADD;
    view.effect_progress = 0.0F;
    view.panel_progress = 0.0F;
    assert(setenv("LOWTASK_REDUCE_MOTION", "1", 1) == 0);
    TuiLayout reduced_layout;
    assert(tui_layout_compute(80U, 12U, &view, &reduced_layout));
    assert(reduced_layout.panel.x == 4U);
    assert(tui_task_row_shift(&view, task_id, 0U) == 0U);
    tui_draw(&renderer, &tasks, &view);
    const uint64_t reduced_start = screen_hash(&renderer);
    view.effect_progress = 1.0F;
    view.panel_progress = 1.0F;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_hash(&renderer) == reduced_start);
    assert(unsetenv("LOWTASK_REDUCE_MOTION") == 0);

    TuiLayout layout;
    assert(tui_layout_compute(80U, 12U, &view, &layout));
    state.effect = APP_EFFECT_EDIT;
    state.effect_task_id = task_id;
    state.effect_duration = 0.22F;
    state.effect_elapsed = 0.0F;
    view.mode = TUI_MODE_NORMAL;
    view.input = NULL;
    view.effect = TUI_EFFECT_EDIT;
    view.effect_progress = 0.0F;
    tui_draw(&renderer, &tasks, &view);
    const size_t edit_start_x = glyph_x(&renderer, layout.rows.y, "a");
    state.effect_elapsed = state.effect_duration;
    view.effect_progress = 1.0F;
    tui_draw(&renderer, &tasks, &view);
    assert(edit_start_x == glyph_x(&renderer, layout.rows.y, "a"));

    state.effect = APP_EFFECT_TAB;
    state.effect_task_id = 0U;
    state.effect_duration = 0.18F;
    state.effect_elapsed = 0.0F;
    view.effect = TUI_EFFECT_TAB;
    view.effect_progress = NAN;
    tui_draw(&renderer, &tasks, &view);
    view.effect_progress = 0.0F;
    tui_draw(&renderer, &tasks, &view);
    const uint64_t tab_start = screen_hash(&renderer);
    state.effect_elapsed = state.effect_duration;
    view.effect_progress = 1.0F;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_hash(&renderer) == tab_start);

    malformed = view;
    malformed.mode = (TuiMode)99;
    malformed.effect = (TuiEffect)99;
    tui_draw(&renderer, &tasks, &malformed);
    assert(!screen_contains(&renderer, "SCHEDULE TASK"));
    malformed.mode = TUI_MODE_NORMAL;
    assert(tui_hit_test(80U, 12U, 1U, 1U, &malformed).kind == TUI_HIT_NONE);
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_motion_and_large_list(void) {
    TaskList tasks;
    task_list_init(&tasks);
    char text[64];
    for (size_t index = 0U; index < 10000U; ++index) {
        const int written = snprintf(text, sizeof(text), "task %zu", index);
        assert(written > 0 && (size_t)written < sizeof(text));
        assert(task_list_add(&tasks, text, TASK_PRIORITY_NORMAL, NULL));
    }
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_select_visible(&state, 9999U));
    state.effect = APP_EFFECT_ADD;
    state.effect_task_id = tasks.items[9999U].id;
    state.effect_duration = 0.28F;
    Renderer renderer;
    assert(renderer_init(&renderer, 80U, 24U, true));
    TuiViewState view = {
        .app = &state, .selected = 9999U, .scroll = 9984.0F, .panel_progress = 1.0F,
        .mode = TUI_MODE_NORMAL, .effect = TUI_EFFECT_ADD, .effect_index = 9999U,
    };
    TuiLayout focus_layout;
    view.scroll = 0.0F;
    assert(tui_layout_compute(80U, 24U, &view, &focus_layout));
    assert(focus_layout.first_visible == 0U);
    assert(state.selected >= focus_layout.first_visible + focus_layout.visible_rows);
    assert(focus_layout.selection_pinned);
    assert(focus_layout.pinned_row == focus_layout.visible_rows - 1U);
    assert(focus_layout.pinned_visible_index == state.selected);
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "task 0"));
    assert(screen_contains(&renderer, "task 9999"));
    const uint64_t scroll_start_hash = screen_hash(&renderer);
    TuiHit pinned_hit = tui_hit_test(80U, 24U, focus_layout.rows.x,
                                     focus_layout.rows.y + focus_layout.pinned_row, &view);
    assert(pinned_hit.kind == TUI_HIT_TASK);
    assert(pinned_hit.visible_index == state.selected);
    assert(pinned_hit.action.task_id == tasks.items[state.selected].id);
    view.scroll = 9979.4F;
    assert(tui_layout_compute(80U, 24U, &view, &focus_layout));
    assert(focus_layout.first_visible == 9979U);
    assert(state.selected >= focus_layout.first_visible + focus_layout.visible_rows);
    assert(focus_layout.selection_pinned);
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "task 9999"));
    assert(screen_hash(&renderer) != scroll_start_hash);
    view.scroll = 9981.0F;
    assert(tui_layout_compute(80U, 24U, &view, &focus_layout));
    assert(!focus_layout.selection_pinned);

    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "task 9999"));
    const uint64_t start_hash = screen_hash(&renderer);
    state.effect_elapsed = 0.14F;
    view.effect_progress = 0.5F;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_hash(&renderer) != start_hash);
    const int null_fd = open("/dev/null", O_WRONLY);
    assert(null_fd >= 0);
    assert(renderer_present(&renderer, null_fd) > 0);
    assert(close(null_fd) == 0);
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_truncation_status_and_pluralization(void) {
    TaskList tasks;
    task_list_init(&tasks);
    (void)add_task(&tasks, "a very long 中文 task title that cannot fit", TASK_PRIORITY_NORMAL,
                   NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    (void)snprintf(state.status, sizeof(state.status),
                   "important failure details remain visible through the end");
    Renderer renderer;
    assert(renderer_init(&renderer, 30U, 8U, true));
    TuiViewState view = {
        .app = &state, .panel_progress = 1.0F, .mode = TUI_MODE_NORMAL, .status = state.status,
    };
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "…"));
    view.ascii = true;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "a"));
    assert(renderer_resize(&renderer, 96U, 12U));
    view.ascii = false;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "important failure details remain visible through the end"));
    assert(screen_contains(&renderer, "1 task · ALL"));
    assert(!screen_contains(&renderer, "1 tasks"));

    app_state_set_tab(&state, APP_TAB_UPCOMING);
    assert(renderer_resize(&renderer, 30U, 8U));
    view.effect = TUI_EFFECT_TAB;
    view.ascii = false;
    view.effect_progress = 0.0F;
    tui_draw(&renderer, &tasks, &view);
    const size_t rail_start = glyph_x(&renderer, 1U, "▌");
    view.effect_progress = 0.5F;
    tui_draw(&renderer, &tasks, &view);
    const size_t rail_middle = glyph_x(&renderer, 1U, "▌");
    assert(glyph_count_on_row(&renderer, 1U, "▌") == 1U);
    view.effect_progress = 1.0F;
    tui_draw(&renderer, &tasks, &view);
    const size_t rail_end = glyph_x(&renderer, 1U, "▌");
    assert(rail_start < rail_middle && rail_middle < rail_end);
    assert(screen_contains(&renderer, "All"));
    assert(screen_contains(&renderer, "Today"));
    assert(screen_contains(&renderer, "Soon"));
    assert(screen_contains(&renderer, "Done"));
    app_state_set_tab(&state, APP_TAB_TODAY);
    view.effect = TUI_EFFECT_NONE;
    tui_draw(&renderer, &tasks, &view);
    const size_t today_stable = glyph_x(&renderer, 1U, "▌");
    app_state_set_tab(&state, APP_TAB_ALL);
    view.effect = TUI_EFFECT_TAB;
    view.effect_progress = 0.0F;
    tui_draw(&renderer, &tasks, &view);
    assert(glyph_x(&renderer, 1U, "▌") == today_stable);
    view.ascii = true;
    tui_draw(&renderer, &tasks, &view);
    assert(glyph_count_on_row(&renderer, 1U, ">") == 1U);

    state.tab = APP_TAB_COMPLETED;
    state.previous_tab = APP_TAB_COMPLETED;
    view.effect = TUI_EFFECT_NONE;
    assert(renderer_resize(&renderer, 40U, 8U));
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "COMPLETED"));
    assert(glyph_count_on_row(&renderer, 1U, ">") == 1U);

    state.tab = APP_TAB_ALL;
    state.previous_tab = APP_TAB_ALL;
    state.effect = APP_EFFECT_NONE;
    view.effect = TUI_EFFECT_NONE;
    assert(renderer_resize(&renderer, 9U, 4U));
    tui_draw(&renderer, &tasks, &view);
    assert(glyph_x(&renderer, 2U, ".") != SIZE_MAX);
    assert(renderer_resize(&renderer, 10U, 4U));
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "a"));

    assert(task_list_edit(&tasks, tasks.items[0].id, "AB中文"));
    view.ascii = false;
    tui_draw(&renderer, &tasks, &view);
    const size_t final_ascii_x = glyph_x(&renderer, 2U, "B");
    const size_t ellipsis_x = glyph_x(&renderer, 2U, "…");
    assert(ellipsis_x != SIZE_MAX);
    assert(final_ascii_x == SIZE_MAX || ellipsis_x == final_ascii_x + 1U);

    assert(renderer_resize(&renderer, 11U, 4U));
    view.ascii = false;
    view.mode = TUI_MODE_EDIT;
    view.input = "中abcdef";
    tui_draw(&renderer, &tasks, &view);
    assert(glyph_x(&renderer, 2U, "▏") != SIZE_MAX);
    view.ascii = true;
    tui_draw(&renderer, &tasks, &view);
    assert(glyph_x(&renderer, 2U, "_") != SIZE_MAX);
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_todo7_projection_controls_and_compact_metadata(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t low = add_task(&tasks, "low created first", TASK_PRIORITY_LOW, NULL, false);
    const uint64_t urgent = add_task(&tasks, "urgent created second", TASK_PRIORITY_URGENT,
                                     "2026-07-11", false);
    const uint64_t high = add_task(&tasks, "high created third", TASK_PRIORITY_HIGH,
                                   "2026-07-10", false);
    const uint64_t done = add_task(&tasks, "completed marker", TASK_PRIORITY_NORMAL,
                                   "2026-07-09", true);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    assert(app_state_set_sort(&state, APP_SORT_CREATED));
    Renderer renderer;
    assert(renderer_init(&renderer, 96U, 16U, true));
    TuiViewState view = {.app = &state, .panel_progress = 1.0F, .status = state.status};
    tui_draw(&renderer, &tasks, &view);
    assert(text_row(&renderer, "completed marker") < text_row(&renderer, "high created third"));
    assert(text_row(&renderer, "high created third") < text_row(&renderer, "urgent created second"));
    assert(text_row(&renderer, "urgent created second") < text_row(&renderer, "low created first"));
    assert(screen_contains(&renderer, "FILTER:Any"));
    assert(screen_contains(&renderer, "SORT:Created"));
    assert(screen_contains(&renderer, "HELP"));
    assert(find_action(96U, 16U, &view, APP_ACTION_CYCLE_PRIORITY_FILTER, 0U,
                       APP_TAB_ALL).type == APP_ACTION_CYCLE_PRIORITY_FILTER);
    assert(find_action(96U, 16U, &view, APP_ACTION_CYCLE_SORT, 0U,
                       APP_TAB_ALL).type == APP_ACTION_CYCLE_SORT);
    assert(find_action(96U, 16U, &view, APP_ACTION_OPEN_HELP, 0U,
                       APP_TAB_ALL).type == APP_ACTION_OPEN_HELP);
    assert(find_action(96U, 16U, &view, APP_ACTION_OPEN_PRIORITY_PICKER, urgent,
                       APP_TAB_ALL).type == APP_ACTION_OPEN_PRIORITY_PICKER);
    assert(screen_has_foreground(&renderer, color_token_rgb(TUI_COLOR_URGENT)));

    static const size_t widths[] = {95U, 64U, 63U, 52U, 51U, 40U, 39U, 32U, 31U, 24U, 23U};
    for (size_t index = 0U; index < sizeof(widths) / sizeof(widths[0]); ++index) {
        const size_t width = widths[index];
        assert(renderer_resize(&renderer, width, 9U));
        tui_draw(&renderer, &tasks, &view);
        assert(screen_contains(&renderer, width >= 64U ? "HELP" : "?"));
        assert(find_action(width, 9U, &view, APP_ACTION_OPEN_HELP, 0U,
                           APP_TAB_ALL).type == APP_ACTION_OPEN_HELP);
        if (width >= 24U) {
            for (size_t tab = 0U; tab < APP_TAB_COUNT; ++tab) {
                assert(find_action(width, 9U, &view, APP_ACTION_SET_TAB, 0U,
                                   (AppTab)tab).type == APP_ACTION_SET_TAB);
            }
        }
    }
    assert(renderer_resize(&renderer, 51U, 9U));
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "x"));
    assert(find_action(51U, 9U, &view, APP_ACTION_EDIT_SCHEDULE, done,
                       APP_TAB_ALL).type == APP_ACTION_EDIT_SCHEDULE);
    assert(find_action(51U, 9U, &view, APP_ACTION_EDIT_SCHEDULE, low,
                       APP_TAB_ALL).type == APP_ACTION_EDIT_SCHEDULE);
    assert(find_action(51U, 9U, &view, APP_ACTION_EDIT_SCHEDULE, high,
                       APP_TAB_ALL).type == APP_ACTION_EDIT_SCHEDULE);
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_todo7_groups_pickers_and_help_overlay(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t overdue = add_task(&tasks, "old", TASK_PRIORITY_HIGH, "2026-07-10", false);
    (void)add_task(&tasks, "now", TASK_PRIORITY_NORMAL, "2026-07-11", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    Renderer renderer;
    assert(renderer_init(&renderer, 64U, 12U, true));
    TuiViewState view = {.app = &state, .panel_progress = 1.0F, .status = state.status};
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "OVERDUE"));
    assert(screen_contains(&renderer, "DUE TODAY"));
    const size_t group_row = text_row(&renderer, "OVERDUE");
    assert(group_row != SIZE_MAX);
    assert(tui_hit_test(64U, 12U, 4U, group_row, &view).kind == TUI_HIT_NONE);
    assert(find_action(64U, 12U, &view, APP_ACTION_SELECT_TASK, overdue,
                       APP_TAB_ALL).type == APP_ACTION_SELECT_TASK);

    state.mode = APP_MODE_PRIORITY_PICKER;
    state.modal_task_id = overdue;
    state.focused_option = 1U;
    view.mode = TUI_MODE_PRIORITY_PICKER;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "PRIORITY"));
    assert(screen_contains(&renderer, "Urgent [4]"));
    assert(screen_contains(&renderer, "High [3]"));
    assert(screen_contains(&renderer, "Normal [2]"));
    assert(screen_contains(&renderer, "Low [1]"));
    AppAction option = find_action(64U, 12U, &view, APP_ACTION_APPLY_OPTION, 0U, APP_TAB_ALL);
    assert(option.type == APP_ACTION_APPLY_OPTION && option.option.kind == APP_OPTION_PRIORITY);

    state.mode = APP_MODE_SCHEDULE_PICKER;
    state.focused_option = 0U;
    view.mode = TUI_MODE_SCHEDULE_PICKER;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "Today [1]"));
    assert(screen_contains(&renderer, "Tomorrow [2]"));
    assert(screen_contains(&renderer, "+7 Days [3]"));
    assert(screen_contains(&renderer, "Custom [4]"));
    assert(screen_contains(&renderer, "Clear [5]"));

    state.mode = APP_MODE_NORMAL;
    view.mode = TUI_MODE_NORMAL;
    for (size_t height = 5U; height <= 7U; ++height) {
        TuiLayout short_layout;
        assert(tui_layout_compute(64U, height, &view, &short_layout));
        for (size_t tab = 0U; tab < APP_TAB_COUNT; ++tab) {
            assert((short_layout.tab_targets[tab].width > 0U) ==
                   (tab == (size_t)state.tab));
        }
    }

    state.mode = APP_MODE_HELP;
    state.help_scroll = 0U;
    view.mode = TUI_MODE_HELP;
    size_t lines = 0U;
    size_t page_rows = 0U;
    tui_help_metrics(64U, 12U, false, &lines, &page_rows);
    assert(lines > page_rows && page_rows > 0U);
    app_state_set_help_metrics(&state, lines, page_rows);
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "HELP"));
    assert(screen_contains(&renderer, "Navigation and views"));
    assert(screen_contains(&renderer, "HELP 1-"));
    assert(find_action(64U, 12U, &view, APP_ACTION_CLOSE_HELP, 0U,
                       APP_TAB_ALL).type == APP_ACTION_CLOSE_HELP);
    assert(find_action(64U, 12U, &view, APP_ACTION_SET_TAB, 0U,
                       APP_TAB_ALL).type == APP_ACTION_NONE);
    for (size_t height = 3U; height <= 9U; ++height) {
        tui_help_metrics(24U, height, false, &lines, &page_rows);
        if (height <= 3U) assert(page_rows == 0U);
        if (height == 4U) assert(page_rows == 1U);
        assert(renderer_resize(&renderer, 24U, height));
        app_state_set_help_metrics(&state, lines, page_rows);
        tui_draw(&renderer, &tasks, &view);
        if (height <= 3U) assert(screen_contains(&renderer, "help needs 4 rows"));
        else assert(screen_contains(&renderer, "HELP"));
    }
    view.ascii = true;
    assert(renderer_resize(&renderer, 40U, 8U));
    tui_help_metrics(40U, 8U, true, &lines, &page_rows);
    app_state_set_help_metrics(&state, lines, page_rows);
    tui_draw(&renderer, &tasks, &view);
    assert(screen_is_ascii(&renderer));
    assert(screen_contains(&renderer, "X"));
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_todo7_drag_visuals_and_display_row_scroll(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "drag source", TASK_PRIORITY_NORMAL,
                                    "2026-07-10", false);
    (void)add_task(&tasks, "today row", TASK_PRIORITY_NORMAL, "2026-07-11", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    Renderer renderer;
    assert(renderer_init(&renderer, 64U, 12U, true));
    TuiViewState view = {.app = &state, .panel_progress = 1.0F, .status = state.status};
    TuiLayout layout;
    assert(tui_layout_compute(64U, 12U, &view, &layout));
    assert(app_state_display_row_count(&state, layout.visible_rows) == 4U);
    assert(tui_task_display_row(&state, first, layout.visible_rows) == 1U);
    state.drag_candidate = true;
    state.drag_task_id = first;
    state.drag_source_action = (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = first};
    state.pressed_action = state.drag_source_action;
    memcpy(state.drag_source_title, "drag source", sizeof("drag source"));
    tui_draw(&renderer, &tasks, &view);
    assert(screen_has_foreground(&renderer, color_token_rgb(TUI_COLOR_ACCENT_STRONG)));
    state.drag_active = true;
    state.drag_candidate = false;
    state.drag_current_column = 30U;
    state.drag_current_row = (uint16_t)(layout.rows.y + 1U);
    state.drag_press_column = 10U;
    state.drag_press_row = (uint16_t)(layout.rows.y);
    state.drag_target_tab = APP_TAB_COMPLETED;
    state.drag_target_valid = true;
    state.drag_lift_duration = 0.10F;
    state.drag_lift_elapsed = 0.05F;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "DRAG"));
    assert(screen_contains(&renderer, "COMPLETED"));
    view.ascii = true;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "[DRAG]"));
    assert(screen_is_ascii(&renderer));
    view.ascii = false;
    state.drag_active = false;
    state.effect = APP_EFFECT_MOVE;
    state.effect_task_id = first;
    state.effect_duration = 0.22F;
    state.effect_elapsed = 0.11F;
    view.effect = TUI_EFFECT_MOVE;
    view.effect_progress = 0.5F;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_contains(&renderer, "drag source"));
    assert(setenv("LOWTASK_REDUCE_MOTION", "1", 1) == 0);
    state.drag_active = true;
    state.effect = APP_EFFECT_NONE;
    view.effect = TUI_EFFECT_NONE;
    tui_draw(&renderer, &tasks, &view);
    const uint64_t reduced = screen_hash(&renderer);
    state.drag_lift_elapsed = state.drag_lift_duration;
    tui_draw(&renderer, &tasks, &view);
    assert(screen_hash(&renderer) == reduced);
    assert(unsetenv("LOWTASK_REDUCE_MOTION") == 0);
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

int main(void) {
    test_matrix_tabs_dates_and_hits();
    test_design_tokens_responsive_fallbacks_and_dates();
    test_hit_padding_empty_add_and_pointer_precedence();
    test_modal_cjk_cursor_and_meaningful_motion();
    test_motion_and_large_list();
    test_truncation_status_and_pluralization();
    test_todo7_projection_controls_and_compact_metadata();
    test_todo7_groups_pickers_and_help_overlay();
    test_todo7_drag_visuals_and_display_row_scroll();
    puts("test_view: PASS");
    return 0;
}

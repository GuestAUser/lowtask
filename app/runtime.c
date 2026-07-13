#include "app/runtime.h"

#include "input/controller.h"
#include "input/input.h"
#include "tui/animation.h"
#include "tui/view.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#define FRAME_SECONDS (1.0 / 60.0)

typedef struct {
    AppState *state;
    Terminal *terminal;
    Renderer *renderer;
    InputDecoder decoder;
    Tween panel;
    Tween scroll;
    bool reduced_motion;
} AppRuntime;

static TuiMode view_mode(AppMode mode) {
    if (mode == APP_MODE_ADD) return TUI_MODE_ADD;
    if (mode == APP_MODE_EDIT) return TUI_MODE_EDIT;
    if (mode == APP_MODE_SCHEDULE) return TUI_MODE_SCHEDULE;
    if (mode == APP_MODE_PRIORITY_PICKER) return TUI_MODE_PRIORITY_PICKER;
    if (mode == APP_MODE_SCHEDULE_PICKER) return TUI_MODE_SCHEDULE_PICKER;
    if (mode == APP_MODE_HELP) return TUI_MODE_HELP;
    return TUI_MODE_NORMAL;
}

static TuiEffect view_effect(AppEffect effect) {
    if (effect == APP_EFFECT_ADD) return TUI_EFFECT_ADD;
    if (effect == APP_EFFECT_EDIT) return TUI_EFFECT_EDIT;
    if (effect == APP_EFFECT_COMPLETE) return TUI_EFFECT_COMPLETE;
    if (effect == APP_EFFECT_DELETE) return TUI_EFFECT_DELETE;
    if (effect == APP_EFFECT_TAB) return TUI_EFFECT_TAB;
    if (effect == APP_EFFECT_MOVE) return TUI_EFFECT_MOVE;
    return TUI_EFFECT_NONE;
}

static TuiViewState make_view(const AppRuntime *runtime) {
    return (TuiViewState){
        .app = runtime->state,
        .selected = runtime->state->selected,
        .scroll = runtime->scroll.value,
        .panel_progress = runtime->panel.value,
        .ascii = !runtime->terminal->capabilities.unicode,
        .mode = view_mode(runtime->state->mode),
        .input = runtime->state->input,
        .status = runtime->state->status,
        .effect = view_effect(runtime->state->effect),
        .effect_index = runtime->state->effect_index,
        .effect_progress = app_state_effect_progress(runtime->state),
    };
}

static size_t visible_rows(const AppRuntime *runtime) {
    const TuiViewState view = make_view(runtime);
    TuiLayout layout;
    return tui_layout_compute(runtime->terminal->columns, runtime->terminal->rows, &view, &layout) &&
           layout.visible_rows > 0U ? layout.visible_rows : 1U;
}

static void retarget_scroll(AppRuntime *runtime, size_t rows) {
    const size_t count = app_state_display_row_count(runtime->state, rows);
    if (count == 0U) {
        if (runtime->scroll.target != 0.0F) {
            tween_to(&runtime->scroll, 0.0F, runtime->reduced_motion ? 0.0F : 0.12F);
        }
        return;
    }
    const size_t selected = tui_task_display_row(
        runtime->state, app_state_selected_task_id(runtime->state), rows);
    if (selected == SIZE_MAX) return;
    size_t target = runtime->scroll.target > 0.0F ? (size_t)runtime->scroll.target : 0U;
    if (selected < target) {
        target = selected;
    } else if (selected >= target + rows) {
        target = selected - rows + 1U;
    }
    const size_t maximum = count > rows ? count - rows : 0U;
    if (target > maximum) target = maximum;
    target = app_state_display_window_start(runtime->state, target, rows);
    if ((float)target != runtime->scroll.target) {
        tween_to(&runtime->scroll, (float)target, runtime->reduced_motion ? 0.0F : 0.14F);
    }
}

static void update_help_metrics(AppRuntime *runtime) {
    size_t line_count = 0U;
    size_t page_rows = 0U;
    tui_help_metrics(runtime->terminal->columns, runtime->terminal->rows,
                     !runtime->terminal->capabilities.unicode, &line_count, &page_rows);
    app_state_set_help_metrics(runtime->state, line_count, page_rows);
}

static bool resize_renderer(AppRuntime *runtime) {
    (void)terminal_refresh_size(runtime->terminal);
    if (!renderer_resize(runtime->renderer, runtime->terminal->columns, runtime->terminal->rows)) {
        (void)snprintf(runtime->state->status, sizeof(runtime->state->status),
                       "terminal size is unsupported");
        return false;
    }
    return true;
}

static bool render_frame(AppRuntime *runtime) {
    const TuiViewState view = make_view(runtime);
    tui_draw(runtime->renderer, runtime->state->tasks, &view);
    return renderer_present(runtime->renderer, runtime->terminal->output_fd) >= 0;
}

static bool is_animating(const AppRuntime *runtime) {
    const bool drag_lifting = runtime->state->drag_active &&
        runtime->state->drag_lift_elapsed < runtime->state->drag_lift_duration;
    return runtime->panel.active || runtime->scroll.active ||
           runtime->state->effect != APP_EFFECT_NONE || drag_lifting ||
           renderer_has_pending_output(runtime->renderer);
}

static void handle_event(AppRuntime *runtime, InputEvent event) {
    if (event.type == INPUT_KEY_MOUSE && event.mouse_action != INPUT_MOUSE_WHEEL) {
        const TuiViewState view = make_view(runtime);
        const TuiHit hit = tui_hit_test(runtime->terminal->columns, runtime->terminal->rows,
                                       event.mouse_column, event.mouse_row, &view);
        controller_handle_mouse_action(runtime->state, hit.action, event);
    } else {
        if (event.type != INPUT_KEY_MOUSE) {
            runtime->state->hovered_action = (AppAction){0};
            runtime->state->pressed_action = (AppAction){0};
        }
        controller_handle(runtime->state, event);
    }
    if (runtime->reduced_motion && runtime->state->effect != APP_EFFECT_NONE) {
        app_state_update(runtime->state, runtime->state->effect_duration);
    }
}

static int poll_timeout(const AppRuntime *runtime, bool needs_frame, double next_frame) {
    /* Incomplete input prefixes need a short deadline; otherwise the loop may sleep indefinitely. */
    if (runtime->decoder.length > 0U || runtime->decoder.discarding_control_sequence) return 25;
    if (!needs_frame && !is_animating(runtime)) return 250;
    const double remaining = next_frame - terminal_monotonic_seconds();
    int timeout = remaining <= 0.0 ? 0 : (int)(remaining * 1000.0 + 0.999);
    return timeout > 17 ? 17 : timeout;
}

int app_runtime_run(AppState *state, Terminal *terminal, Renderer *renderer) {
    AppRuntime runtime = {
        .state = state,
        .terminal = terminal,
        .renderer = renderer,
        .reduced_motion = animation_reduced_motion_enabled(),
    };
    input_decoder_init(&runtime.decoder);
    tween_init(&runtime.panel, 0.0F);
    tween_to(&runtime.panel, 1.0F, runtime.reduced_motion ? 0.0F : 0.38F);
    tween_init(&runtime.scroll, 0.0F);
    update_help_metrics(&runtime);

    double previous = terminal_monotonic_seconds();
    double next_frame = previous;
    bool needs_frame = true;
    int exit_code = 0;
    while (!state->quit && !terminal_stop_requested()) {
        double now = terminal_monotonic_seconds();
        if (terminal_take_resize()) {
            if (!resize_renderer(&runtime)) {
                exit_code = 1;
                break;
            }
            update_help_metrics(&runtime);
            retarget_scroll(&runtime, visible_rows(&runtime));
            needs_frame = true;
        }
        /* Monotonic elapsed time skips missed intermediate frames instead of accumulating lag. */
        if ((needs_frame || is_animating(&runtime)) && now >= next_frame) {
            float delta = (float)(now - previous);
            if (delta < 0.0F) delta = 0.0F;
            app_state_update(state, delta);
            (void)tween_update(&runtime.panel, delta);
            retarget_scroll(&runtime, visible_rows(&runtime));
            (void)tween_update(&runtime.scroll, delta);
            app_state_set_list_scroll(state, runtime.scroll.value);
            if (!render_frame(&runtime)) {
                exit_code = 1;
                break;
            }
            previous = now;
            next_frame = now + FRAME_SECONDS;
            needs_frame = renderer_has_pending_output(renderer);
        }

        /* The renderer retains partial frames; POLLOUT merely re-arms its bounded drain path. */
        struct pollfd descriptors[2] = {
            {.fd = terminal->input_fd, .events = POLLIN},
            {.fd = terminal->output_fd,
             .events = renderer_has_pending_output(renderer) ? POLLOUT : 0},
        };
        const nfds_t descriptor_count = descriptors[1].events == 0 ? 1U : 2U;
        const int poll_result = poll(descriptors, descriptor_count,
                                     poll_timeout(&runtime, needs_frame, next_frame));
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            exit_code = 1;
            break;
        }
        InputEvent events[64];
        size_t event_count = 0U;
        const struct pollfd input = descriptors[0];
        if (descriptor_count == 2U &&
            (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            !((descriptors[1].revents & POLLHUP) != 0 && (input.revents & POLLHUP) != 0)) {
            exit_code = 1;
            break;
        }
        if (descriptor_count == 2U && (descriptors[1].revents & POLLOUT) != 0) {
            needs_frame = true;
            next_frame = terminal_monotonic_seconds();
        }
        if (poll_result > 0 && ((input.revents & POLLNVAL) != 0 ||
            ((input.revents & POLLERR) != 0 && (input.revents & POLLHUP) == 0))) {
            exit_code = 1;
            break;
        }
        if (poll_result > 0 && (input.revents & POLLIN) != 0) {
            unsigned char bytes[64];
            const ssize_t count = read(terminal->input_fd, bytes, sizeof(bytes));
            if (count > 0) {
                event_count = input_decoder_feed(&runtime.decoder, bytes, (size_t)count,
                                                  events, sizeof(events) / sizeof(events[0]));
            } else if (count == 0) {
                break;
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                exit_code = 1;
                break;
            }
        } else if (poll_result > 0 && (input.revents & POLLHUP) != 0) {
            break;
        } else if (poll_result == 0 &&
                   (runtime.decoder.length > 0U || runtime.decoder.discarding_control_sequence)) {
            event_count = input_decoder_flush(&runtime.decoder, events,
                                               sizeof(events) / sizeof(events[0]));
        }
        const AppEffect effect_before_events = state->effect;
        for (size_t index = 0U; index < event_count; ++index) {
            app_state_set_list_scroll(state, runtime.scroll.value);
            update_help_metrics(&runtime);
            handle_event(&runtime, events[index]);
            if (state->quit || terminal_stop_requested()) break;
        }
        if (event_count > 0U) {
            retarget_scroll(&runtime, visible_rows(&runtime));
            if (effect_before_events == APP_EFFECT_NONE && state->effect != APP_EFFECT_NONE) {
                previous = terminal_monotonic_seconds();
                next_frame = previous;
            }
            needs_frame = true;
        }
    }
    return exit_code;
}

#ifndef LOWTASK_INPUT_CONTROLLER_INTERNAL_H
#define LOWTASK_INPUT_CONTROLLER_INTERNAL_H

#include "core/state.h"

#include <stdio.h>

static inline void controller_set_status(AppState *state, const char *message) {
    (void)snprintf(state->status, sizeof(state->status), "%s", message);
}

static inline void controller_clear_pointer(AppState *state) {
    state->hovered_action = (AppAction){0};
    state->pressed_action = (AppAction){0};
}

static inline void controller_start_effect(AppState *state, AppEffect effect, uint64_t task_id,
                                           float duration) {
    if (state->effect == APP_EFFECT_DELETE && effect != APP_EFFECT_DELETE) return;
    if (state->effect == APP_EFFECT_MOVE && effect != APP_EFFECT_MOVE) {
        app_state_clear_move_feedback(state);
    }
    state->effect = effect;
    state->effect_index = state->selected;
    state->effect_task_id = task_id;
    state->effect_elapsed = 0.0F;
    state->effect_duration = duration;
}

static inline bool controller_refresh_mutation(AppState *state) {
    if (app_state_refresh(state)) return true;
    controller_set_status(state, "unable to refresh tasks");
    return false;
}

static inline bool controller_action_equal(AppAction left, AppAction right) {
    return left.type == right.type && left.tab == right.tab && left.task_id == right.task_id &&
           left.option.kind == right.option.kind && left.option.value == right.option.value;
}

#endif

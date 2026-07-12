#ifndef LOWTASK_INPUT_CONTROLLER_MODAL_H
#define LOWTASK_INPUT_CONTROLLER_MODAL_H

#include "core/state.h"
#include "input/input.h"

void controller_modal_reset(AppState *state);
void controller_modal_open_picker(AppState *state, AppMode mode, uint64_t task_id);
void controller_modal_apply_option(AppState *state, AppOptionPayload option);
void controller_modal_handle_picker(AppState *state, InputEvent event);

#endif

#ifndef LOWTASK_INPUT_CONTROLLER_H
#define LOWTASK_INPUT_CONTROLLER_H

#include "core/state.h"
#include "input/input.h"

void controller_handle(AppState *state, InputEvent event);
void controller_handle_action(AppState *state, AppAction action);
void controller_handle_mouse_action(AppState *state, AppAction action, InputEvent event);

#endif

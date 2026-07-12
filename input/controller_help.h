#ifndef LOWTASK_INPUT_CONTROLLER_HELP_H
#define LOWTASK_INPUT_CONTROLLER_HELP_H

#include "core/state.h"
#include "input/input.h"

void controller_help_open(AppState *state);
void controller_help_close(AppState *state);
void controller_help_handle(AppState *state, InputEvent event);

#endif

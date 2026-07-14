#ifndef LOWTASK_INPUT_CONTROLLER_DRAG_H
#define LOWTASK_INPUT_CONTROLLER_DRAG_H

#include "core/state.h"
#include "input/input.h"

void controller_drag_clear(AppState *state, bool preserve_source);
void controller_drag_cancel(AppState *state, const char *message);
void controller_drag_begin(AppState *state, AppAction action, InputEvent event);
void controller_drag_track(AppState *state, AppAction action, InputEvent event);
AppAction controller_drag_resolve_release(AppState *state, AppAction action, InputEvent event);

#endif

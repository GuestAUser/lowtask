#ifndef LOWTASK_INPUT_CONTROLLER_TEXT_H
#define LOWTASK_INPUT_CONTROLLER_TEXT_H

#include "core/state.h"
#include "input/input.h"

void controller_text_enter_input(AppState *state, AppMode mode, uint64_t task_id);
void controller_text_enter_editor(AppState *state, uint64_t task_id, AppEditField field);
void controller_text_switch_field(AppState *state, bool backwards);
void controller_text_move(AppState *state, InputKeyType key);
void controller_text_append_character(AppState *state, uint32_t codepoint);
void controller_text_remove_character(AppState *state);
void controller_text_delete_character(AppState *state);
void controller_text_submit(AppState *state);

#endif

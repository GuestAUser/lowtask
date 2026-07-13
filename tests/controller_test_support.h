#ifndef LOWTASK_CONTROLLER_TEST_SUPPORT_H
#define LOWTASK_CONTROLLER_TEST_SUPPORT_H

#include "core/state.h"
#include "input/controller.h"

#include <stdbool.h>
#include <stdint.h>

InputEvent controller_test_character(uint32_t codepoint);
InputEvent controller_test_mouse(InputMouseAction action, InputMouseButton button,
                                 uint16_t column, uint16_t row);
AppAction controller_test_option_action(AppOptionKind kind, unsigned int value);
uint64_t controller_test_add_task(TaskList *tasks, const char *text, const char *due_date,
                                  bool completed);
void controller_test_drag_from_to(AppState *state, AppAction source, AppAction target,
                                  uint16_t press_column, uint16_t press_row,
                                  uint16_t target_column, uint16_t target_row);

#endif

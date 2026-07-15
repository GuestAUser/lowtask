#ifndef LOWTASK_CORE_TEXT_INPUT_H
#define LOWTASK_CORE_TEXT_INPUT_H

#include "core/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char value[LOWTASK_TEXT_MAX + 1U];
    size_t length;
    size_t cursor;
} AppTextInput;

typedef struct {
    size_t start;
    size_t end;
    size_t caret_cell;
    bool draw_caret;
} AppTextViewport;

void app_text_input_clear(AppTextInput *input);
bool app_text_input_set(AppTextInput *input, const char *value);
bool app_text_input_insert(AppTextInput *input, uint32_t codepoint);
void app_text_input_left(AppTextInput *input);
void app_text_input_right(AppTextInput *input);
void app_text_input_home(AppTextInput *input);
void app_text_input_end(AppTextInput *input);
void app_text_input_backspace(AppTextInput *input);
void app_text_input_delete(AppTextInput *input);
AppTextViewport app_text_input_viewport(const AppTextInput *input, size_t cells);

#endif

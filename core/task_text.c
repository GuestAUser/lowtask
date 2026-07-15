#include "core/task.h"

#include "core/text.h"

bool task_text_is_valid(const char *text) {
    return text_utf8_is_valid(text, LOWTASK_TEXT_MAX, false);
}

bool task_description_is_valid(const char *description) {
    return description == NULL || text_utf8_is_valid(description, LOWTASK_DESCRIPTION_MAX, true);
}

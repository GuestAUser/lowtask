#ifndef LOWTASK_CORE_PERSISTENCE_FORMAT_H
#define LOWTASK_CORE_PERSISTENCE_FORMAT_H

#include "core/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

bool persistence_format_load(FILE *file, TaskList *loaded, char *error, size_t error_size);
bool persistence_format_write(FILE *file, const TaskList *list);
bool persistence_format_state_is_valid(const TaskList *list);

#endif

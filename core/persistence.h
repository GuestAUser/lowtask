#ifndef LOWTASK_CORE_PERSISTENCE_H
#define LOWTASK_CORE_PERSISTENCE_H

#include "core/task.h"

#include <stdbool.h>
#include <stddef.h>

bool persistence_default_path(char *output, size_t output_size);
bool persistence_lock(const char *path, int *lock_fd, char *error, size_t error_size);
void persistence_unlock(int lock_fd);
bool persistence_load(const char *path, TaskList *list, char *error, size_t error_size);
bool persistence_save(const char *path, const TaskList *list, char *error, size_t error_size);

#endif

#ifndef LOWTASK_CORE_TASK_H
#define LOWTASK_CORE_TASK_H

#include "core/date.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOWTASK_TEXT_MAX 255U
#define LOWTASK_DESCRIPTION_MAX 255U
#define LOWTASK_DUE_DATE_LENGTH LOWTASK_DATE_LENGTH
#define LOWTASK_MAX_TASKS 1000000U

typedef enum {
    TASK_PRIORITY_LOW = 1,
    TASK_PRIORITY_NORMAL = 2,
    TASK_PRIORITY_HIGH = 3,
    TASK_PRIORITY_URGENT = 4
} TaskPriority;

typedef struct {
    uint64_t id;
    char text[LOWTASK_TEXT_MAX + 1U];
    char due_date[LOWTASK_DUE_DATE_LENGTH + 1U];
    char *description;
    TaskPriority priority;
    bool completed;
} Task;

typedef struct {
    Task *items;
    size_t length;
    size_t capacity;
    uint64_t next_id;
    uint64_t revision;
} TaskList;

void task_list_init(TaskList *list);
void task_list_free(TaskList *list);
bool task_text_is_valid(const char *text);
bool task_description_is_valid(const char *description);
bool task_due_date_is_valid(const char *due_date);
bool task_priority_is_valid(TaskPriority priority);
bool task_list_add(TaskList *list, const char *text, TaskPriority priority, uint64_t *id_out);
bool task_list_add_configured(TaskList *list, const char *text, TaskPriority priority,
                              const char *due_date, bool completed, uint64_t *id_out);
bool task_list_import(TaskList *list, uint64_t id, const char *text, TaskPriority priority, bool completed);
bool task_list_import_full(TaskList *list, uint64_t id, const char *text,
                           const char *description, TaskPriority priority, bool completed);
bool task_list_edit(TaskList *list, uint64_t id, const char *text);
bool task_list_edit_fields(TaskList *list, uint64_t id, const char *text,
                           const char *description);
bool task_list_delete(TaskList *list, uint64_t id);
bool task_list_toggle_complete(TaskList *list, uint64_t id);
bool task_list_set_priority(TaskList *list, uint64_t id, TaskPriority priority);
bool task_list_set_due_date(TaskList *list, uint64_t id, const char *due_date);
Task *task_list_get(TaskList *list, uint64_t id);
const Task *task_list_get_const(const TaskList *list, uint64_t id);

#endif

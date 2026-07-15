#include "core/task.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool task_priority_is_valid(TaskPriority priority) {
    return priority >= TASK_PRIORITY_LOW && priority <= TASK_PRIORITY_URGENT;
}

bool task_due_date_is_valid(const char *due_date) {
    return date_is_valid(due_date);
}

static bool reserve(TaskList *list, size_t needed) {
    if (needed <= list->capacity) {
        return true;
    }
    if (needed > LOWTASK_MAX_TASKS) {
        return false;
    }
    size_t capacity = list->capacity == 0U ? 16U : list->capacity;
    while (capacity < needed) {
        if (capacity > LOWTASK_MAX_TASKS / 2U) {
            capacity = LOWTASK_MAX_TASKS;
            break;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*list->items)) {
        return false;
    }
    Task *items = realloc(list->items, capacity * sizeof(*items));
    if (items == NULL) {
        return false;
    }
    list->items = items;
    list->capacity = capacity;
    return true;
}

void task_list_init(TaskList *list) {
    if (list != NULL) {
        *list = (TaskList){.next_id = 1U};
    }
}

void task_list_free(TaskList *list) {
    if (list != NULL) {
        for (size_t index = 0U; index < list->length; ++index) {
            free(list->items[index].description);
        }
        free(list->items);
        *list = (TaskList){.next_id = 1U};
    }
}

static char *copy_description(const char *description) {
    if (description == NULL || description[0] == '\0') return NULL;
    const size_t length = strlen(description);
    char *copy = malloc(length + 1U);
    if (copy != NULL) memcpy(copy, description, length + 1U);
    return copy;
}

bool task_list_import_full(TaskList *list, uint64_t id, const char *text,
                           const char *description, TaskPriority priority, bool completed) {
    if (list == NULL || id == 0U || id >= UINT64_MAX - 1U || !task_text_is_valid(text) ||
        !task_description_is_valid(description) || !task_priority_is_valid(priority) ||
        (list->length > 0U && id <= list->items[list->length - 1U].id)) {
        return false;
    }
    char *owned_description = copy_description(description);
    if (description != NULL && description[0] != '\0' && owned_description == NULL) return false;
    if (!reserve(list, list->length + 1U)) {
        free(owned_description);
        return false;
    }
    Task *task = &list->items[list->length++];
    *task = (Task){.id = id, .description = owned_description,
                   .priority = priority, .completed = completed};
    const size_t length = strlen(text);
    memcpy(task->text, text, length + 1U);
    if (id >= list->next_id) {
        list->next_id = id + 1U;
    }
    ++list->revision;
    return true;
}

bool task_list_import(TaskList *list, uint64_t id, const char *text, TaskPriority priority,
                      bool completed) {
    return task_list_import_full(list, id, text, NULL, priority, completed);
}

bool task_list_add_configured(TaskList *list, const char *text, TaskPriority priority,
                              const char *due_date, bool completed, uint64_t *id_out) {
    if (list == NULL || list->next_id == 0U || list->next_id >= UINT64_MAX - 1U) {
        return false;
    }
    const uint64_t id = list->next_id;
    if (!task_text_is_valid(text) || !task_priority_is_valid(priority) ||
        (due_date != NULL && due_date[0] != '\0' && !task_due_date_is_valid(due_date)) ||
        !reserve(list, list->length + 1U)) {
        return false;
    }
    Task *task = &list->items[list->length++];
    *task = (Task){.id = id, .priority = priority, .completed = completed};
    memcpy(task->text, text, strlen(text) + 1U);
    if (due_date != NULL && due_date[0] != '\0') {
        memcpy(task->due_date, due_date, LOWTASK_DUE_DATE_LENGTH + 1U);
    }
    list->next_id = id + 1U;
    ++list->revision;
    if (id_out != NULL) *id_out = id;
    return true;
}

bool task_list_add(TaskList *list, const char *text, TaskPriority priority, uint64_t *id_out) {
    return task_list_add_configured(list, text, priority, NULL, false, id_out);
}

Task *task_list_get(TaskList *list, uint64_t id) {
    if (list == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < list->length; ++index) {
        if (list->items[index].id == id) {
            return &list->items[index];
        }
    }
    return NULL;
}

const Task *task_list_get_const(const TaskList *list, uint64_t id) {
    if (list == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < list->length; ++index) {
        if (list->items[index].id == id) {
            return &list->items[index];
        }
    }
    return NULL;
}

bool task_list_edit(TaskList *list, uint64_t id, const char *text) {
    Task *task = task_list_get(list, id);
    return task == NULL ? false : task_list_edit_fields(list, id, text, task->description);
}

bool task_list_edit_fields(TaskList *list, uint64_t id, const char *text,
                           const char *description) {
    Task *task = task_list_get(list, id);
    if (task == NULL || !task_text_is_valid(text) || !task_description_is_valid(description)) {
        return false;
    }
    const size_t length = strlen(text);
    char staged_text[LOWTASK_TEXT_MAX + 1U];
    memcpy(staged_text, text, length + 1U);
    const char *normalized = description != NULL && description[0] != '\0' ? description : NULL;
    const bool description_unchanged =
        (normalized == NULL && task->description == NULL) ||
        (normalized != NULL && task->description != NULL &&
         strcmp(normalized, task->description) == 0);
    char *owned_description = description_unchanged ? task->description : copy_description(normalized);
    if (normalized != NULL && owned_description == NULL) return false;
    memcpy(task->text, staged_text, length + 1U);
    if (!description_unchanged) {
        free(task->description);
        task->description = owned_description;
    }
    ++list->revision;
    return true;
}

bool task_list_delete(TaskList *list, uint64_t id) {
    Task *task = task_list_get(list, id);
    if (task == NULL) {
        return false;
    }
    const size_t index = (size_t)(task - list->items);
    free(task->description);
    if (index + 1U < list->length) {
        memmove(&list->items[index], &list->items[index + 1U],
                (list->length - index - 1U) * sizeof(*list->items));
    }
    --list->length;
    if (list->length < list->capacity) list->items[list->length] = (Task){0};
    ++list->revision;
    return true;
}

bool task_list_toggle_complete(TaskList *list, uint64_t id) {
    Task *task = task_list_get(list, id);
    if (task == NULL) {
        return false;
    }
    task->completed = !task->completed;
    ++list->revision;
    return true;
}

bool task_list_set_priority(TaskList *list, uint64_t id, TaskPriority priority) {
    Task *task = task_list_get(list, id);
    if (task == NULL || !task_priority_is_valid(priority)) {
        return false;
    }
    task->priority = priority;
    ++list->revision;
    return true;
}

bool task_list_set_due_date(TaskList *list, uint64_t id, const char *due_date) {
    Task *task = task_list_get(list, id);
    if (task == NULL) {
        return false;
    }
    if (due_date == NULL || due_date[0] == '\0') {
        task->due_date[0] = '\0';
        ++list->revision;
        return true;
    }
    if (!task_due_date_is_valid(due_date)) {
        return false;
    }
    memcpy(task->due_date, due_date, LOWTASK_DUE_DATE_LENGTH + 1U);
    ++list->revision;
    return true;
}

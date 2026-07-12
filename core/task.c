#include "core/task.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool task_priority_is_valid(TaskPriority priority) {
    return priority >= TASK_PRIORITY_LOW && priority <= TASK_PRIORITY_URGENT;
}

static size_t bounded_length(const char *text, size_t limit) {
    size_t length = 0U;
    if (text == NULL) {
        return 0U;
    }
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool utf8_valid(const unsigned char *bytes, size_t length) {
    size_t index = 0U;
    while (index < length) {
        const unsigned char first = bytes[index];
        size_t count = 0U;
        uint32_t value = 0U;
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7fU) {
                return false;
            }
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            count = 2U;
            value = (uint32_t)(first & 0x1fU);
        } else if (first >= 0xe0U && first <= 0xefU) {
            count = 3U;
            value = (uint32_t)(first & 0x0fU);
        } else if (first >= 0xf0U && first <= 0xf4U) {
            count = 4U;
            value = (uint32_t)(first & 0x07U);
        } else {
            return false;
        }
        if (index + count > length) {
            return false;
        }
        for (size_t offset = 1U; offset < count; ++offset) {
            const unsigned char continuation = bytes[index + offset];
            if ((continuation & 0xc0U) != 0x80U) {
                return false;
            }
            value = (value << 6U) | (uint32_t)(continuation & 0x3fU);
        }
        if ((count == 3U && value < 0x800U) || (count == 4U && value < 0x10000U) ||
            (value >= 0xd800U && value <= 0xdfffU) || value > 0x10ffffU) {
            return false;
        }
        index += count;
    }
    return true;
}

bool task_text_is_valid(const char *text) {
    const size_t length = bounded_length(text, LOWTASK_TEXT_MAX + 1U);
    return length > 0U && length <= LOWTASK_TEXT_MAX &&
           utf8_valid((const unsigned char *)text, length);
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
        free(list->items);
        *list = (TaskList){.next_id = 1U};
    }
}

bool task_list_import(TaskList *list, uint64_t id, const char *text, TaskPriority priority, bool completed) {
    if (list == NULL || id == 0U || id == UINT64_MAX || !task_text_is_valid(text) ||
        !task_priority_is_valid(priority) ||
        (list->length > 0U && id <= list->items[list->length - 1U].id) || !reserve(list, list->length + 1U)) {
        return false;
    }
    Task *task = &list->items[list->length++];
    *task = (Task){.id = id, .priority = priority, .completed = completed};
    const size_t length = strlen(text);
    memcpy(task->text, text, length + 1U);
    if (id >= list->next_id) {
        list->next_id = id + 1U;
    }
    ++list->revision;
    return true;
}

bool task_list_add(TaskList *list, const char *text, TaskPriority priority, uint64_t *id_out) {
    if (list == NULL || list->next_id == 0U || list->next_id == UINT64_MAX) {
        return false;
    }
    const uint64_t id = list->next_id;
    if (!task_list_import(list, id, text, priority, false)) {
        return false;
    }
    if (id_out != NULL) {
        *id_out = id;
    }
    return true;
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
    if (task == NULL || !task_text_is_valid(text)) {
        return false;
    }
    const size_t length = strlen(text);
    memcpy(task->text, text, length + 1U);
    ++list->revision;
    return true;
}

bool task_list_delete(TaskList *list, uint64_t id) {
    Task *task = task_list_get(list, id);
    if (task == NULL) {
        return false;
    }
    const size_t index = (size_t)(task - list->items);
    if (index + 1U < list->length) {
        memmove(&list->items[index], &list->items[index + 1U],
                (list->length - index - 1U) * sizeof(*list->items));
    }
    --list->length;
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

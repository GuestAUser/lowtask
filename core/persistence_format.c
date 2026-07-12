#include "core/persistence_format.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#define RECORD_MAX (LOWTASK_TEXT_MAX * 2U + 128U)

static void set_error(char *error, size_t size, const char *format, ...) {
    if (error == NULL || size == 0U) return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static bool parse_u64(const char *text, uint64_t *value) {
    if (text == NULL || text[0] == '\0' || value == NULL) return false;
    uint64_t parsed = 0U;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        const uint64_t digit = (uint64_t)(text[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    if (parsed == 0U) return false;
    *value = parsed;
    return true;
}

static int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
}

static bool decode_text(const char *hex, char output[LOWTASK_TEXT_MAX + 1U]) {
    const size_t length = strlen(hex);
    if (length == 0U || (length % 2U) != 0U || length / 2U > LOWTASK_TEXT_MAX) return false;
    for (size_t index = 0U; index < length; index += 2U) {
        const int high = hex_value(hex[index]);
        const int low = hex_value(hex[index + 1U]);
        if (high < 0 || low < 0) return false;
        output[index / 2U] = (char)((high << 4) | low);
    }
    output[length / 2U] = '\0';
    return task_text_is_valid(output);
}

static bool split_task(char *line, unsigned int version, uint64_t *id, TaskPriority *priority,
                       bool *completed, char due_date[LOWTASK_DUE_DATE_LENGTH + 1U],
                       char text[LOWTASK_TEXT_MAX + 1U]) {
    const bool has_due_date = version == 2U || version == 3U;
    const size_t expected = has_due_date ? 6U : 5U;
    char *fields[6] = {line};
    size_t count = 1U;
    for (char *cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor != '\t') continue;
        *cursor = '\0';
        if (count >= expected) return false;
        fields[count++] = cursor + 1U;
    }
    if (count != expected || strcmp(fields[0], "TASK") != 0 || !parse_u64(fields[1], id)) {
        return false;
    }
    char *priority_text = fields[2];
    char *completed_text = fields[3];
    char *due_date_text = has_due_date ? fields[4] : NULL;
    char *hex = fields[has_due_date ? 5U : 4U];
    uint64_t priority_value = 0U;
    const uint64_t maximum_priority = version == 3U ? TASK_PRIORITY_URGENT : TASK_PRIORITY_HIGH;
    if (!parse_u64(priority_text, &priority_value) || priority_value > maximum_priority ||
        !task_priority_is_valid((TaskPriority)priority_value) ||
        (strcmp(completed_text, "0") != 0 && strcmp(completed_text, "1") != 0) ||
        !decode_text(hex, text)) {
        return false;
    }
    due_date[0] = '\0';
    if (has_due_date && strcmp(due_date_text, "-") != 0) {
        if (!task_due_date_is_valid(due_date_text)) return false;
        memcpy(due_date, due_date_text, LOWTASK_DUE_DATE_LENGTH + 1U);
    }
    *priority = (TaskPriority)priority_value;
    *completed = completed_text[0] == '1';
    return true;
}

static bool read_line(FILE *file, char *line, size_t size, size_t number, char *error,
                      size_t error_size) {
    if (fgets(line, (int)size, file) == NULL) {
        set_error(error, error_size, "unexpected end of file at line %zu", number);
        return false;
    }
    const size_t length = strlen(line);
    if (length == 0U || line[length - 1U] != '\n') {
        set_error(error, error_size, "record too long or unterminated at line %zu", number);
        return false;
    }
    line[length - 1U] = '\0';
    return true;
}

static bool parse_header(const char *line, unsigned int *version) {
    if (strcmp(line, "LOWTASK\t1") == 0) {
        *version = 1U;
    } else if (strcmp(line, "LOWTASK\t2") == 0) {
        *version = 2U;
    } else if (strcmp(line, "LOWTASK\t3") == 0) {
        *version = 3U;
    } else {
        return false;
    }
    return true;
}

bool persistence_format_load(FILE *file, TaskList *loaded, char *error, size_t error_size) {
    char line[RECORD_MAX];
    bool ok = read_line(file, line, sizeof(line), 1U, error, error_size);
    unsigned int version = 0U;
    if (ok && !parse_header(line, &version)) {
        set_error(error, error_size, "unsupported state header");
        ok = false;
    }
    uint64_t next_id = 0U;
    if (ok) {
        ok = read_line(file, line, sizeof(line), 2U, error, error_size) &&
             strncmp(line, "NEXT\t", 5U) == 0 && parse_u64(line + 5U, &next_id);
        if (!ok && error != NULL && error[0] == '\0') set_error(error, error_size, "invalid next id");
    }
    size_t line_number = 2U;
    while (ok && fgets(line, (int)sizeof(line), file) != NULL) {
        ++line_number;
        const size_t length = strlen(line);
        if (length == 0U || line[length - 1U] != '\n') {
            set_error(error, error_size, "record too long or unterminated at line %zu", line_number);
            ok = false;
            break;
        }
        line[length - 1U] = '\0';
        uint64_t id = 0U;
        TaskPriority priority = TASK_PRIORITY_NORMAL;
        bool completed = false;
        char due_date[LOWTASK_DUE_DATE_LENGTH + 1U];
        char text[LOWTASK_TEXT_MAX + 1U];
        if (!split_task(line, version, &id, &priority, &completed, due_date, text) ||
            !task_list_import(loaded, id, text, priority, completed)) {
            set_error(error, error_size, "invalid task at line %zu", line_number);
            ok = false;
        } else {
            memcpy(loaded->items[loaded->length - 1U].due_date, due_date, sizeof(due_date));
        }
    }
    if (ok && ferror(file) != 0) {
        set_error(error, error_size, "error reading state: %s", strerror(errno));
        ok = false;
    }
    const uint64_t minimum_next =
        loaded->length == 0U ? 1U : loaded->items[loaded->length - 1U].id + 1U;
    if (ok && (next_id < minimum_next || next_id == 0U || next_id == UINT64_MAX)) {
        set_error(error, error_size, "next id does not follow task ids");
        ok = false;
    }
    if (ok) loaded->next_id = next_id;
    return ok;
}

bool persistence_format_write(FILE *file, const TaskList *list) {
    if (fprintf(file, "LOWTASK\t3\nNEXT\t%" PRIu64 "\n", list->next_id) < 0) return false;
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < list->length; ++index) {
        const Task *task = &list->items[index];
        const char *due_date = task->due_date[0] == '\0' ? "-" : task->due_date;
        if (fprintf(file, "TASK\t%" PRIu64 "\t%d\t%d\t%s\t", task->id, (int)task->priority,
                    task->completed ? 1 : 0, due_date) < 0) {
            return false;
        }
        for (size_t offset = 0U; task->text[offset] != '\0'; ++offset) {
            const unsigned char byte = (unsigned char)task->text[offset];
            if (fputc(hex[byte >> 4U], file) == EOF || fputc(hex[byte & 0x0fU], file) == EOF) {
                return false;
            }
        }
        if (fputc('\n', file) == EOF) return false;
    }
    return true;
}

bool persistence_format_state_is_valid(const TaskList *list) {
    if (list == NULL || list->length > LOWTASK_MAX_TASKS || list->capacity > LOWTASK_MAX_TASKS ||
        list->length > list->capacity || (list->capacity == 0U) != (list->items == NULL) ||
        list->next_id == 0U || list->next_id == UINT64_MAX) {
        return false;
    }
    uint64_t previous_id = 0U;
    for (size_t index = 0U; index < list->length; ++index) {
        const Task *task = &list->items[index];
        if (task->id == 0U || task->id == UINT64_MAX || task->id <= previous_id ||
            !task_text_is_valid(task->text) || !task_priority_is_valid(task->priority) ||
            (task->due_date[0] != '\0' && !task_due_date_is_valid(task->due_date))) {
            return false;
        }
        previous_id = task->id;
    }
    return list->next_id > previous_id;
}

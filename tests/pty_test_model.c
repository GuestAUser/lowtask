#define _XOPEN_SOURCE 700

#include "tests/pty_test_api.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

bool local_dates(char today[LOWTASK_DATE_LENGTH + 1U],
                 char tomorrow[LOWTASK_DATE_LENGTH + 1U],
                 char next_week[LOWTASK_DATE_LENGTH + 1U],
                 char future[LOWTASK_DATE_LENGTH + 1U],
                 char overdue[LOWTASK_DATE_LENGTH + 1U]) {
    const time_t now = time(NULL);
    struct tm local;
    if (now == (time_t)-1 || localtime_r(&now, &local) == NULL ||
        strftime(today, LOWTASK_DATE_LENGTH + 1U, "%Y-%m-%d", &local) != LOWTASK_DATE_LENGTH) {
        return false;
    }
    if (!date_add_days(today, 1U, tomorrow) || !date_add_days(today, 7U, next_week) ||
        !date_add_days(today, 14U, future)) return false;
    if (strcmp(today, "0001-01-01") == 0) return false;
    unsigned int year = (unsigned int)(today[0] - '0') * 1000U + (unsigned int)(today[1] - '0') * 100U +
                        (unsigned int)(today[2] - '0') * 10U + (unsigned int)(today[3] - '0');
    unsigned int month = (unsigned int)(today[5] - '0') * 10U + (unsigned int)(today[6] - '0');
    unsigned int day = (unsigned int)(today[8] - '0') * 10U + (unsigned int)(today[9] - '0');
    if (day > 1U) {
        --day;
    } else {
        if (month > 1U) {
            --month;
        } else {
            --year;
            month = 12U;
        }
        static const unsigned int month_lengths[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                                     31U, 31U, 30U, 31U, 30U, 31U};
        day = month_lengths[month - 1U];
        if (month == 2U && (year % 400U == 0U || (year % 4U == 0U && year % 100U != 0U))) ++day;
    }
    const int written = snprintf(overdue, LOWTASK_DATE_LENGTH + 1U, "%04u-%02u-%02u", year, month, day);
    return written == (int)LOWTASK_DATE_LENGTH && date_is_valid(overdue);
}

static bool write_hex(FILE *file, const char *text) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        const unsigned char byte = (unsigned char)text[index];
        if (fputc(digits[byte >> 4U], file) == EOF || fputc(digits[byte & 0x0fU], file) == EOF) return false;
    }
    return true;
}

static bool write_task(FILE *file, uint64_t id, int priority, bool completed,
                       const char *due, const char *text) {
    if (fprintf(file, "TASK\t%" PRIu64 "\t%d\t%d\t%s\t", id, priority,
                completed ? 1 : 0, due == NULL ? "-" : due) < 0 ||
        !write_hex(file, text) || fputc('\n', file) == EOF) return false;
    return true;
}

bool seed_v3(const char *path, bool fillers) {
    char today[11], tomorrow[11], next_week[11], future[11], overdue[11];
    if (!local_dates(today, tomorrow, next_week, future, overdue)) return false;
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    const uint64_t next = fillers ? 31U : 9U;
    bool ok = fprintf(file, "LOWTASK\t3\nNEXT\t%" PRIu64 "\n", next) > 0 &&
        write_task(file, 1U, 4, false, overdue, "overdue urgent alpha") &&
        write_task(file, 2U, 3, false, today, "today high beta") &&
        write_task(file, 3U, 2, false, future, "future normal gamma") &&
        write_task(file, 4U, 1, false, NULL, "unscheduled low delta") &&
        write_task(file, 5U, 1, true, overdue, "completed low epsilon") &&
        write_task(file, 6U, 2, false, tomorrow,
                   "long ASCII 中文 e\xcc\x81 title 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ tail") &&
        write_task(file, 7U, 3, true, future, "completed future source") &&
        write_task(file, 8U, 2, false, NULL, "schedule target");
    for (uint64_t id = 9U; ok && fillers && id <= 30U; ++id) {
        char title[64];
        const int written = snprintf(title, sizeof(title), "filler task %02" PRIu64, id);
        ok = written > 0 && (size_t)written < sizeof(title) &&
             write_task(file, id, (int)(id % 4U) + 1, false, NULL, title);
    }
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool hex_decode(const char *hex, char *output, size_t capacity) {
    const size_t length = strlen(hex);
    if ((length % 2U) != 0U || length / 2U >= capacity) return false;
    for (size_t index = 0U; index < length; index += 2U) {
        int high = hex[index] >= '0' && hex[index] <= '9' ? hex[index] - '0' : hex[index] - 'a' + 10;
        int low = hex[index + 1U] >= '0' && hex[index + 1U] <= '9' ?
                  hex[index + 1U] - '0' : hex[index + 1U] - 'a' + 10;
        if (high < 0 || high > 15 || low < 0 || low > 15) return false;
        output[index / 2U] = (char)((high << 4) | low);
    }
    output[length / 2U] = '\0';
    return true;
}

bool load_model(const char *path, Model *model) {
    *model = (Model){0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    char line[1200];
    bool ok = fgets(line, sizeof(line), file) != NULL &&
              sscanf(line, "LOWTASK\t%u", &model->version) == 1 &&
              fgets(line, sizeof(line), file) != NULL &&
              sscanf(line, "NEXT\t%" SCNu64, &model->next_id) == 1;
    while (ok && fgets(line, sizeof(line), file) != NULL) {
        if (model->count >= MAX_MODEL_TASKS) {
            ok = false;
            break;
        }
        ModelTask *task = &model->tasks[model->count];
        char completed[4];
        char due[16];
        char hex[600];
        char description_hex[600] = "-";
        const int fields = model->version == 4U ?
            sscanf(line, "TASK\t%" SCNu64 "\t%d\t%3[^\t]\t%15[^\t]\t%599[^\t]\t%599s",
                   &task->id, &task->priority, completed, due, hex, description_hex) :
            sscanf(line, "TASK\t%" SCNu64 "\t%d\t%3[^\t]\t%15[^\t]\t%599s",
                   &task->id, &task->priority, completed, due, hex);
        if (fields != (model->version == 4U ? 6 : 5) ||
            (strcmp(completed, "0") != 0 && strcmp(completed, "1") != 0) ||
            !hex_decode(hex, task->text, sizeof(task->text)) ||
            (model->version == 4U && strcmp(description_hex, "-") != 0 &&
             !hex_decode(description_hex, task->description, sizeof(task->description)))) {
            ok = false;
            break;
        }
        task->completed = completed[0] == '1';
        if (strcmp(due, "-") != 0) memcpy(task->due, due, LOWTASK_DATE_LENGTH + 1U);
        ++model->count;
    }
    if (ferror(file) != 0 || fclose(file) != 0) ok = false;
    return ok;
}

const ModelTask *model_task(const Model *model, uint64_t id) {
    for (size_t index = 0U; index < model->count; ++index) {
        if (model->tasks[index].id == id) return &model->tasks[index];
    }
    return NULL;
}

bool write_literal_file(const char *path, const char *contents) {
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) return false;
    const bool ok = write_all(descriptor, contents, strlen(contents));
    return close(descriptor) == 0 && ok;
}

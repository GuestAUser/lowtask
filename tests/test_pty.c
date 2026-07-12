#define _XOPEN_SOURCE 700

#include "core/date.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum {
    SESSION_DEADLINE_MS = 1800,
    QUIET_MS = 20,
    TERMINATE_GRACE_MS = 400,
    TRANSCRIPT_LIMIT = 8 * 1024 * 1024,
    MAX_SCREEN_CELLS = 1000 * 1000,
    MAX_MODEL_TASKS = 96
};

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
} Transcript;

typedef struct {
    char glyph[8];
} ScreenCell;

typedef struct {
    ScreenCell *cells;
    size_t columns;
    size_t rows;
    size_t column;
    size_t row;
    unsigned int parser_state;
    char csi[64];
    size_t csi_length;
    unsigned char utf8[4];
    size_t utf8_length;
    size_t utf8_needed;
} Screen;

typedef struct {
    pid_t pid;
    int status;
    bool reaped;
} Child;

typedef struct {
    uint64_t id;
    int priority;
    bool completed;
    char due[LOWTASK_DATE_LENGTH + 1U];
    char text[256];
} ModelTask;

typedef struct {
    unsigned int version;
    uint64_t next_id;
    ModelTask tasks[MAX_MODEL_TASKS];
    size_t count;
} Model;

typedef struct {
    char root[128];
    char data_directory[256];
    char state_path[320];
    int master;
    int slave;
    struct termios original_termios;
    Child child;
    Transcript transcript;
    Screen screen;
    size_t entry_offset;
    size_t leave_offset;
    bool started;
} Session;

typedef struct {
    uint32_t startup_hash;
    uint32_t lift_hash;
    uint32_t target_hash;
    uint32_t success_hash;
    size_t transcript_bytes;
    size_t csi_count;
    bool saw_drag;
    bool saw_moved;
    bool saw_urgent_256;
} WorkflowEvidence;

static volatile sig_atomic_t interrupted;
static WorkflowEvidence evidence;
static Session *active_session;
static Child *active_extra_child;

static bool session_cleanup(Session *session);
static void screen_dump(const Screen *screen);
static bool wait_transcript_since(Session *session, size_t offset, const char *needle,
                                  int64_t timeout_ms);
static void terminate_and_reap(Child *child);

static const char terminal_enter[] =
    "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[2J\x1b[H";
static const char terminal_leave[] =
    "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l";

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "test_pty: FAIL: %s (%s:%d)\n", (message), __FILE__, __LINE__); \
        if (active_extra_child != NULL) { \
            terminate_and_reap(active_extra_child); \
            active_extra_child = NULL; \
        } \
        if (active_session != NULL) { \
            screen_dump(&active_session->screen); \
            (void)session_cleanup(active_session); \
            active_session = NULL; \
        } \
        return false; \
    } \
} while (0)

static int64_t monotonic_milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000LL + (int64_t)now.tv_nsec / 1000000LL;
}

static void interruption_handler(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}

static bool install_interruption_handlers(void) {
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = interruption_handler;
    if (sigemptyset(&action.sa_mask) != 0) return false;
    for (size_t index = 0U; index < sizeof(signals) / sizeof(signals[0]); ++index) {
        if (sigaction(signals[index], &action, NULL) != 0) return false;
    }
    return true;
}

static bool transcript_append(Transcript *transcript, const char *bytes, size_t length) {
    if (length > TRANSCRIPT_LIMIT - transcript->length) return false;
    const size_t needed = transcript->length + length + 1U;
    if (needed > transcript->capacity) {
        size_t capacity = transcript->capacity == 0U ? 16384U : transcript->capacity;
        while (capacity < needed) capacity *= 2U;
        char *grown = realloc(transcript->bytes, capacity);
        if (grown == NULL) return false;
        transcript->bytes = grown;
        transcript->capacity = capacity;
    }
    memcpy(transcript->bytes + transcript->length, bytes, length);
    transcript->length += length;
    transcript->bytes[transcript->length] = '\0';
    return true;
}

static bool screen_resize(Screen *screen, size_t columns, size_t rows) {
    if (columns == 0U || rows == 0U || columns > MAX_SCREEN_CELLS / rows) return false;
    ScreenCell *cells = calloc(columns * rows, sizeof(*cells));
    if (cells == NULL) return false;
    free(screen->cells);
    screen->cells = cells;
    screen->columns = columns;
    screen->rows = rows;
    screen->column = 0U;
    screen->row = 0U;
    screen->parser_state = 0U;
    screen->csi_length = 0U;
    screen->utf8_length = 0U;
    screen->utf8_needed = 0U;
    return true;
}

static void screen_clear(Screen *screen) {
    memset(screen->cells, 0, screen->columns * screen->rows * sizeof(*screen->cells));
    screen->column = 0U;
    screen->row = 0U;
}

static unsigned int utf8_width(const unsigned char *bytes, size_t length) {
    uint32_t codepoint = bytes[0];
    if (length == 2U) codepoint &= 0x1fU;
    if (length == 3U) codepoint &= 0x0fU;
    if (length == 4U) codepoint &= 0x07U;
    for (size_t index = 1U; index < length; ++index) {
        codepoint = (codepoint << 6U) | (uint32_t)(bytes[index] & 0x3fU);
    }
    if ((codepoint >= 0x0300U && codepoint <= 0x036fU) ||
        (codepoint >= 0x200bU && codepoint <= 0x200fU) ||
        (codepoint >= 0x202aU && codepoint <= 0x202eU) ||
        (codepoint >= 0x2060U && codepoint <= 0x206fU)) return 0U;
    if ((codepoint >= 0x1100U && codepoint <= 0x115fU) ||
        (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) ||
        (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
        (codepoint >= 0xff01U && codepoint <= 0xff60U) ||
        (codepoint >= 0x1f300U && codepoint <= 0x1faffU)) return 2U;
    return 1U;
}

static void screen_put(Screen *screen, const unsigned char *bytes, size_t length) {
    const unsigned int width = utf8_width(bytes, length);
    if (width == 0U) {
        if (screen->column > 0U && screen->row < screen->rows) {
            ScreenCell *cell = &screen->cells[screen->row * screen->columns + screen->column - 1U];
            const size_t used = strlen(cell->glyph);
            if (used + length < sizeof(cell->glyph)) {
                memcpy(cell->glyph + used, bytes, length);
                cell->glyph[used + length] = '\0';
            }
        }
        return;
    }
    if (screen->column >= screen->columns || screen->row >= screen->rows) return;
    ScreenCell *cell = &screen->cells[screen->row * screen->columns + screen->column];
    const size_t copy = length < sizeof(cell->glyph) - 1U ? length : sizeof(cell->glyph) - 1U;
    memcpy(cell->glyph, bytes, copy);
    cell->glyph[copy] = '\0';
    if (width == 2U && screen->column + 1U < screen->columns) {
        screen->cells[screen->row * screen->columns + screen->column + 1U].glyph[0] = '\0';
    }
    screen->column += width;
}

static unsigned long csi_parameter(const Screen *screen, size_t ordinal, unsigned long fallback) {
    size_t current = 0U;
    unsigned long value = 0U;
    bool has_digit = false;
    for (size_t index = 0U; index <= screen->csi_length; ++index) {
        const char byte = index < screen->csi_length ? screen->csi[index] : ';';
        if (byte >= '0' && byte <= '9') {
            value = value * 10U + (unsigned long)(byte - '0');
            has_digit = true;
        } else if (byte == ';') {
            if (current == ordinal) return has_digit ? value : fallback;
            ++current;
            value = 0U;
            has_digit = false;
        }
    }
    return fallback;
}

static void screen_apply_csi(Screen *screen, unsigned char final) {
    if (final == 'H' || final == 'f') {
        const unsigned long row = csi_parameter(screen, 0U, 1U);
        const unsigned long column = csi_parameter(screen, 1U, 1U);
        screen->row = row > 0U && row <= screen->rows ? (size_t)row - 1U : 0U;
        screen->column = column > 0U && column <= screen->columns ? (size_t)column - 1U : 0U;
    } else if (final == 'J' && csi_parameter(screen, 0U, 0U) == 2U) {
        screen_clear(screen);
    }
}

static void screen_feed(Screen *screen, const char *bytes, size_t length) {
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char byte = (unsigned char)bytes[index];
        if (screen->utf8_needed > 0U) {
            screen->utf8[screen->utf8_length++] = byte;
            if (screen->utf8_length == screen->utf8_needed) {
                screen_put(screen, screen->utf8, screen->utf8_length);
                screen->utf8_length = 0U;
                screen->utf8_needed = 0U;
            }
            continue;
        }
        if (screen->parser_state == 1U) {
            screen->parser_state = byte == '[' ? 2U : 0U;
            screen->csi_length = 0U;
            continue;
        }
        if (screen->parser_state == 2U) {
            if (byte >= 0x40U && byte <= 0x7eU) {
                screen_apply_csi(screen, byte);
                screen->parser_state = 0U;
            } else if (screen->csi_length + 1U < sizeof(screen->csi)) {
                screen->csi[screen->csi_length++] = (char)byte;
            }
            continue;
        }
        if (byte == 0x1bU) {
            screen->parser_state = 1U;
        } else if (byte == '\r') {
            screen->column = 0U;
        } else if (byte == '\n') {
            if (screen->row + 1U < screen->rows) ++screen->row;
        } else if (byte >= 0x20U && byte < 0x7fU) {
            screen_put(screen, &byte, 1U);
        } else if (byte >= 0xc2U && byte <= 0xf4U) {
            screen->utf8[0] = byte;
            screen->utf8_length = 1U;
            screen->utf8_needed = byte <= 0xdfU ? 2U : (byte <= 0xefU ? 3U : 4U);
        }
    }
}

static bool screen_row_text(const Screen *screen, size_t row, char *output, size_t capacity) {
    if (row >= screen->rows || capacity == 0U) return false;
    size_t used = 0U;
    for (size_t column = 0U; column < screen->columns && used + 1U < capacity; ++column) {
        const ScreenCell *cell = &screen->cells[row * screen->columns + column];
        const size_t length = strlen(cell->glyph);
        if (length == 0U) {
            output[used++] = ' ';
        } else if (used + length < capacity) {
            memcpy(output + used, cell->glyph, length);
            used += length;
        }
    }
    while (used > 0U && output[used - 1U] == ' ') --used;
    output[used] = '\0';
    return true;
}

static void screen_dump(const Screen *screen) {
    char row[8192];
    for (size_t index = 0U; index < screen->rows; ++index) {
        if (screen_row_text(screen, index, row, sizeof(row))) {
            fprintf(stderr, "screen[%zu]=%s\n", index, row);
        }
    }
}

static bool screen_contains(const Screen *screen, const char *needle) {
    char row[8192];
    for (size_t index = 0U; index < screen->rows; ++index) {
        if (screen_row_text(screen, index, row, sizeof(row)) && strstr(row, needle) != NULL) return true;
    }
    return false;
}

static bool screen_find_ascii(const Screen *screen, const char *needle, size_t *x, size_t *y) {
    const size_t length = strlen(needle);
    for (size_t row = 0U; row < screen->rows; ++row) {
        for (size_t column = 0U; column + length <= screen->columns; ++column) {
            bool match = true;
            for (size_t offset = 0U; offset < length; ++offset) {
                const ScreenCell *cell = &screen->cells[row * screen->columns + column + offset];
                if (cell->glyph[0] != needle[offset] || cell->glyph[1] != '\0') {
                    match = false;
                    break;
                }
            }
            if (match) {
                *x = column;
                *y = row;
                return true;
            }
        }
    }
    return false;
}

static bool screen_find_row_title(const Screen *screen, const char *needle, size_t *x, size_t *y) {
    const size_t length = strlen(needle);
    bool found = false;
    size_t best_x = SIZE_MAX;
    size_t best_y = 0U;
    for (size_t row = 2U; row < screen->rows; ++row) {
        for (size_t column = 0U; column + length <= screen->columns; ++column) {
            bool match = true;
            for (size_t offset = 0U; offset < length; ++offset) {
                const ScreenCell *cell = &screen->cells[row * screen->columns + column + offset];
                if (cell->glyph[0] != needle[offset] || cell->glyph[1] != '\0') {
                    match = false;
                    break;
                }
            }
            if (match && column < best_x) {
                best_x = column;
                best_y = row;
                found = true;
            }
        }
    }
    if (found) {
        *x = best_x;
        *y = best_y;
    }
    return found;
}

static uint32_t screen_hash(const Screen *screen) {
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < screen->columns * screen->rows; ++index) {
        for (size_t byte = 0U; byte < sizeof(screen->cells[index].glyph); ++byte) {
            hash ^= (unsigned char)screen->cells[index].glyph[byte];
            hash *= 16777619U;
        }
    }
    return hash;
}

static uint32_t transcript_hash(const Transcript *transcript) {
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < transcript->length; ++index) {
        hash ^= (unsigned char)transcript->bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t transcript_csi_count(const Transcript *transcript) {
    size_t count = 0U;
    for (size_t index = 0U; index + 1U < transcript->length; ++index) {
        if ((unsigned char)transcript->bytes[index] == 0x1bU && transcript->bytes[index + 1U] == '[') ++count;
    }
    return count;
}

static bool child_poll_status(Child *child) {
    if (child->reaped) return true;
    const pid_t result = waitpid(child->pid, &child->status, WNOHANG);
    if (result == child->pid) child->reaped = true;
    return result >= 0 || (result < 0 && errno == EINTR);
}

static bool session_read(Session *session, bool *closed) {
    char buffer[16384];
    for (;;) {
        const ssize_t count = read(session->master, buffer, sizeof(buffer));
        if (count > 0) {
            if (!transcript_append(&session->transcript, buffer, (size_t)count)) return false;
            screen_feed(&session->screen, buffer, (size_t)count);
            continue;
        }
        if (count == 0 || (count < 0 && errno == EIO)) {
            *closed = true;
            return true;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        return false;
    }
}

static bool session_wait(Session *session, const char *needle, int64_t timeout_ms) {
    const int64_t start = monotonic_milliseconds();
    if (start < 0) return false;
    const int64_t deadline = start + timeout_ms;
    bool closed = false;
    for (;;) {
        if (interrupted != 0 || !session_read(session, &closed) ||
            !child_poll_status(&session->child)) return false;
        if (screen_contains(&session->screen, needle)) return true;
        if (closed || session->child.reaped) return false;
        const int64_t now = monotonic_milliseconds();
        if (now < 0 || now >= deadline) return false;
        struct pollfd input = {.fd = session->master, .events = POLLIN};
        const int remaining = (int)(deadline - now);
        const int ready = poll(&input, 1U, remaining > 25 ? 25 : remaining);
        if (ready < 0 && errno != EINTR) return false;
    }
}

static bool session_settle(Session *session, int64_t timeout_ms) {
    const int64_t start = monotonic_milliseconds();
    if (start < 0) return false;
    const int64_t deadline = start + timeout_ms;
    bool closed = false;
    for (;;) {
        if (!session_read(session, &closed) || !child_poll_status(&session->child)) return false;
        if (closed || session->child.reaped) return true;
        const int64_t now = monotonic_milliseconds();
        if (now < 0 || now >= deadline) return false;
        struct pollfd input = {.fd = session->master, .events = POLLIN};
        const int remaining = (int)(deadline - now);
        const int ready = poll(&input, 1U, remaining > QUIET_MS ? QUIET_MS : remaining);
        if (ready == 0) return true;
        if (ready < 0 && errno != EINTR) return false;
    }
}

static bool write_all(int descriptor, const char *bytes, size_t length) {
    const int64_t start = monotonic_milliseconds();
    if (start < 0) return false;
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t count = write(descriptor, bytes + offset, length - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const int64_t now = monotonic_milliseconds();
            if (now < 0 || now - start >= 500LL) return false;
            struct pollfd output = {.fd = descriptor, .events = POLLOUT};
            if (poll(&output, 1U, 25) < 0 && errno != EINTR) return false;
        } else {
            return false;
        }
    }
    return true;
}

static bool session_send(Session *session, const char *bytes) {
    return write_all(session->master, bytes, strlen(bytes));
}

static bool session_send_expect(Session *session, const char *bytes, const char *needle) {
    return session_send(session, bytes) && session_wait(session, needle, SESSION_DEADLINE_MS);
}

static bool transcript_contains_since(const Session *session, size_t offset, const char *needle) {
    return session->transcript.bytes != NULL && offset <= session->transcript.length &&
           strstr(session->transcript.bytes + offset, needle) != NULL;
}

static bool wait_transcript_since(Session *session, size_t offset, const char *needle,
                                  int64_t timeout_ms) {
    const int64_t start = monotonic_milliseconds();
    if (start < 0) return false;
    while (monotonic_milliseconds() - start < timeout_ms) {
        bool closed = false;
        if (!session_read(session, &closed)) return false;
        if (transcript_contains_since(session, offset, needle)) return true;
        if (closed || !child_poll_status(&session->child) || session->child.reaped) return false;
        struct pollfd input = {.fd = session->master, .events = POLLIN};
        if (poll(&input, 1U, 20) < 0 && errno != EINTR) return false;
    }
    return false;
}

static bool local_dates(char today[LOWTASK_DATE_LENGTH + 1U],
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

static bool seed_v3(const char *path, bool fillers) {
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

static bool load_model(const char *path, Model *model) {
    *model = (Model){0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    char line[1024];
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
        if (sscanf(line, "TASK\t%" SCNu64 "\t%d\t%3[^\t]\t%15[^\t]\t%599s",
                   &task->id, &task->priority, completed, due, hex) != 5 ||
            (strcmp(completed, "0") != 0 && strcmp(completed, "1") != 0) ||
            !hex_decode(hex, task->text, sizeof(task->text))) {
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

static const ModelTask *model_task(const Model *model, uint64_t id) {
    for (size_t index = 0U; index < model->count; ++index) {
        if (model->tasks[index].id == id) return &model->tasks[index];
    }
    return NULL;
}

static bool configure_child_environment(const char *root, bool reduced, bool ascii) {
    return setenv("XDG_DATA_HOME", root, 1) == 0 && setenv("HOME", root, 1) == 0 &&
           setenv("TERM", "xterm-256color", 1) == 0 && setenv("LC_ALL", "C.UTF-8", 1) == 0 &&
           setenv("LOWTASK_REDUCE_MOTION", reduced ? "1" : "0", 1) == 0 &&
           setenv("LOWTASK_ASCII", ascii ? "1" : "0", 1) == 0 && unsetenv("COLORTERM") == 0;
}

static Child spawn_child(int master, int slave, const char *root, bool reduced, bool ascii) {
    Child child = {.pid = fork()};
    if (child.pid != 0) return child;
    (void)close(master);
    if (dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
        dup2(slave, STDERR_FILENO) < 0) _exit(126);
    if (slave > STDERR_FILENO) (void)close(slave);
    if (!configure_child_environment(root, reduced, ascii)) _exit(126);
    execl("./lowtask", "lowtask", (char *)NULL);
    _exit(127);
}

static void terminate_and_reap(Child *child) {
    if (child->pid <= 0 || child->reaped) return;
    (void)kill(child->pid, SIGTERM);
    const int64_t deadline = monotonic_milliseconds() + TERMINATE_GRACE_MS;
    while (!child->reaped && monotonic_milliseconds() < deadline) {
        (void)child_poll_status(child);
        if (!child->reaped) (void)poll(NULL, 0U, 10);
    }
    if (!child->reaped) {
        (void)kill(child->pid, SIGKILL);
        while (waitpid(child->pid, &child->status, 0) < 0 && errno == EINTR) {}
        child->reaped = true;
    }
}

static bool remove_tree(const char *root, const char *data_directory) {
    bool ok = true;
    DIR *directory = opendir(data_directory);
    if (directory != NULL) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char path[512];
            const int written = snprintf(path, sizeof(path), "%s/%s", data_directory, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(path) || unlink(path) != 0) ok = false;
        }
        if (closedir(directory) != 0 || rmdir(data_directory) != 0) ok = false;
    } else if (errno != ENOENT) {
        ok = false;
    }
    if (rmdir(root) != 0) ok = false;
    return ok;
}

static bool termios_equal(const struct termios *left, const struct termios *right) {
    return left->c_iflag == right->c_iflag && left->c_oflag == right->c_oflag &&
           left->c_cflag == right->c_cflag && left->c_lflag == right->c_lflag &&
           memcmp(left->c_cc, right->c_cc, sizeof(left->c_cc)) == 0;
}

static bool session_prepare(Session *session, size_t columns, size_t rows) {
    *session = (Session){.master = -1, .slave = -1, .child = {.pid = -1}};
    active_session = session;
    memcpy(session->root, "/tmp/lowtask-pty-XXXXXX", sizeof("/tmp/lowtask-pty-XXXXXX"));
    CHECK(mkdtemp(session->root) != NULL, "mkdtemp failed");
    CHECK(snprintf(session->data_directory, sizeof(session->data_directory), "%s/lowtask",
                   session->root) > 0, "data path failed");
    CHECK(snprintf(session->state_path, sizeof(session->state_path), "%s/tasks.db",
                   session->data_directory) > 0, "state path failed");
    CHECK(mkdir(session->data_directory, 0700) == 0, "data directory creation failed");
    session->master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    CHECK(session->master >= 0 && grantpt(session->master) == 0 && unlockpt(session->master) == 0,
          "PTY allocation failed");
    const char *slave_name = ptsname(session->master);
    CHECK(slave_name != NULL, "ptsname failed");
    session->slave = open(slave_name, O_RDWR | O_NOCTTY);
    CHECK(session->slave >= 0 && tcgetattr(session->slave, &session->original_termios) == 0,
          "PTY slave setup failed");
    const struct winsize window = {.ws_row = (unsigned short)rows, .ws_col = (unsigned short)columns};
    CHECK(ioctl(session->slave, TIOCSWINSZ, &window) == 0 &&
          screen_resize(&session->screen, columns, rows), "initial PTY size failed");
    return true;
}

static bool session_start(Session *session, bool reduced, bool ascii) {
    session->child = spawn_child(session->master, session->slave, session->root, reduced, ascii);
    CHECK(session->child.pid > 0, "application fork failed");
    const int64_t deadline = monotonic_milliseconds() + SESSION_DEADLINE_MS;
    bool closed = false;
    while (monotonic_milliseconds() < deadline) {
        CHECK(session_read(session, &closed), "startup read failed");
        if (session->transcript.bytes != NULL) {
            const char *entry = strstr(session->transcript.bytes, terminal_enter);
            if (entry != NULL && screen_contains(&session->screen, "lowtask")) {
                session->entry_offset = (size_t)(entry - session->transcript.bytes);
                session->started = true;
                return true;
            }
        }
        CHECK(!closed && child_poll_status(&session->child) && !session->child.reaped,
              "application exited during startup");
        struct pollfd input = {.fd = session->master, .events = POLLIN};
        if (poll(&input, 1U, 20) < 0 && errno != EINTR) return false;
    }
    CHECK(false, "terminal entry/startup frame deadline exceeded");
}

static bool session_restart(Session *session, bool reduced, bool ascii) {
    CHECK(session->child.reaped, "restart attempted with live child");
    evidence.transcript_bytes += session->transcript.length;
    evidence.csi_count += transcript_csi_count(&session->transcript);
    session->transcript.length = 0U;
    if (session->transcript.bytes != NULL) session->transcript.bytes[0] = '\0';
    screen_clear(&session->screen);
    session->child = (Child){.pid = -1};
    session->started = false;
    session->entry_offset = 0U;
    session->leave_offset = 0U;
    return session_start(session, reduced, ascii);
}

static bool session_resize_pty(Session *session, size_t columns, size_t rows) {
    const struct winsize window = {.ws_row = (unsigned short)rows, .ws_col = (unsigned short)columns};
    const size_t output_before = session->transcript.length;
    CHECK(screen_resize(&session->screen, columns, rows), "screen resize failed");
    CHECK(ioctl(session->slave, TIOCSWINSZ, &window) == 0 && kill(session->child.pid, SIGWINCH) == 0,
          "PTY resize signal failed");
    const int64_t deadline = monotonic_milliseconds() + SESSION_DEADLINE_MS;
    bool closed = false;
    while (session->transcript.length == output_before && monotonic_milliseconds() < deadline) {
        CHECK(session_read(session, &closed) && !closed && child_poll_status(&session->child) &&
              !session->child.reaped, "resize output wait failed");
        if (session->transcript.length == output_before) {
            struct pollfd input = {.fd = session->master, .events = POLLIN};
            if (poll(&input, 1U, 20) < 0 && errno != EINTR) return false;
        }
    }
    CHECK(session->transcript.length > output_before && session_settle(session, 300),
          "resized frame missing");
    return true;
}

static bool session_wait_exit(Session *session, int expected_exit) {
    const int64_t deadline = monotonic_milliseconds() + SESSION_DEADLINE_MS;
    bool closed = false;
    while (!session->child.reaped && monotonic_milliseconds() < deadline) {
        CHECK(session_read(session, &closed) && child_poll_status(&session->child), "exit read/wait failed");
        if (!session->child.reaped) {
            struct pollfd input = {.fd = session->master, .events = POLLIN};
            if (poll(&input, 1U, 20) < 0 && errno != EINTR) return false;
        }
    }
    CHECK(session->child.reaped && WIFEXITED(session->child.status) &&
          WEXITSTATUS(session->child.status) == expected_exit, "unexpected application exit");
    (void)session_read(session, &closed);
    const char *leave = session->transcript.bytes == NULL ? NULL :
                        strstr(session->transcript.bytes, terminal_leave);
    CHECK(leave != NULL, "terminal leave sequence missing");
    session->leave_offset = (size_t)(leave - session->transcript.bytes);
    struct termios restored;
    CHECK(tcgetattr(session->slave, &restored) == 0 &&
          termios_equal(&session->original_termios, &restored), "termios was not restored");
    errno = 0;
    int status = 0;
    CHECK(waitpid(session->child.pid, &status, WNOHANG) == -1 && errno == ECHILD,
          "application child was not fully reaped");
    return true;
}

static bool session_quit(Session *session) {
    CHECK(session_send(session, "q"), "quit write failed");
    return session_wait_exit(session, 0);
}

static bool session_cleanup(Session *session) {
    terminate_and_reap(&session->child);
    if (session->slave >= 0) (void)close(session->slave);
    if (session->master >= 0) (void)close(session->master);
    const bool cleaned = remove_tree(session->root, session->data_directory);
    evidence.transcript_bytes += session->transcript.length;
    evidence.csi_count += transcript_csi_count(&session->transcript);
    free(session->transcript.bytes);
    free(session->screen.cells);
    *session = (Session){0};
    if (active_session == session) active_session = NULL;
    return cleaned;
}

static bool mouse_event(Session *session, unsigned int encoded, size_t x, size_t y, char final) {
    char sequence[64];
    const int written = snprintf(sequence, sizeof(sequence), "\x1b[<%u;%zu;%zu%c",
                                 encoded, x + 1U, y + 1U, final);
    return written > 0 && (size_t)written < sizeof(sequence) &&
           write_all(session->master, sequence, (size_t)written);
}

static bool mouse_click(Session *session, size_t x, size_t y) {
    return mouse_event(session, 0U, x, y, 'M') && mouse_event(session, 0U, x, y, 'm');
}

static bool click_label(Session *session, const char *label) {
    size_t x = 0U;
    size_t y = 0U;
    CHECK(screen_find_ascii(&session->screen, label, &x, &y), "mouse label not found");
    CHECK(mouse_click(session, x + strlen(label) / 2U, y), "mouse click failed");
    return true;
}

static bool keyboard_schedule_option(Session *session, char option, const char *status) {
    char input[2] = {option, '\0'};
    return session_send(session, "s") && session_wait(session, "Today [1]", SESSION_DEADLINE_MS) &&
           session_send(session, input) && session_wait(session, status, SESSION_DEADLINE_MS);
}

static bool drag_title_to_label(Session *session, const char *title, const char *label,
                                bool motion, bool expect_drag) {
    size_t source_x = 0U, source_y = 0U, target_x = 0U, target_y = 0U;
    CHECK(screen_find_row_title(&session->screen, title, &source_x, &source_y), "drag source not visible");
    CHECK(screen_find_ascii(&session->screen, label, &target_x, &target_y), "drag target not visible");
    source_x += strlen(title) > 8U ? 8U : 1U;
    const size_t offset = session->transcript.length;
    CHECK(mouse_event(session, 0U, source_x, source_y, 'M'), "drag press failed");
    if (motion) {
        CHECK(mouse_event(session, 32U, target_x + 1U, target_y, 'M'), "drag motion failed");
        if (expect_drag) CHECK(wait_transcript_since(session, offset, "DRAG", SESSION_DEADLINE_MS),
                               "drag ghost feedback missing");
    }
    CHECK(mouse_event(session, 0U, target_x + 1U, target_y, 'm'), "drag release failed");
    return true;
}

static bool scenario_keyboard_workflow(void) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "keyboard session prepare failed");
    CHECK(seed_v3(session.state_path, false), "keyboard fixture seed failed");
    CHECK(session_start(&session, true, false), "keyboard session start failed");
    CHECK(screen_contains(&session.screen, "FILTER:Any") && screen_contains(&session.screen, "SORT:Smart") &&
          screen_contains(&session.screen, "overdue urgent alpha") &&
          screen_contains(&session.screen, "long ASCII") &&
          session.transcript.bytes != NULL && strstr(session.transcript.bytes, "\xe4\xb8\xad\xe6\x96\x87") != NULL,
          "seeded Unicode workflow frame incomplete");
    CHECK(strstr(session.transcript.bytes, "38;5;205") != NULL, "Urgent ANSI-256 sequence missing");

    CHECK(session_send_expect(&session, "aadded by pty\r", "task added"),
          "add workflow failed");
    CHECK(session_send_expect(&session, "e edited\r", "task updated"),
          "edit workflow failed");
    CHECK(session_send_expect(&session, "4", "priority changed"),
          "direct Urgent workflow failed");
    CHECK(session_send(&session, "p") && session_wait(&session, "Urgent [4]", SESSION_DEADLINE_MS),
          "priority picker did not open");
    CHECK(session_send_expect(&session, "3", "priority changed"),
          "priority picker apply failed");
    CHECK(session_send_expect(&session, "4", "priority changed"),
          "Urgent restore failed");

    CHECK(keyboard_schedule_option(&session, '1', "schedule updated"),
          "Today schedule failed");
    CHECK(keyboard_schedule_option(&session, '2', "schedule updated"),
          "Tomorrow schedule failed");
    CHECK(keyboard_schedule_option(&session, '3', "schedule updated"),
          "+7 schedule failed");
    CHECK(session_send(&session, "s") && session_wait(&session, "Today [1]", SESSION_DEADLINE_MS) &&
          session_send(&session, "4"), "Custom schedule open failed");
    CHECK(session_wait(&session, "SCHEDULE TASK", SESSION_DEADLINE_MS), "Custom input modal missing");
    CHECK(session_send(&session, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f"),
          "Custom date clear failed");
    char today[11], tomorrow[11], next_week[11], future[11], overdue[11];
    CHECK(local_dates(today, tomorrow, next_week, future, overdue), "dynamic dates failed");
    CHECK(session_send(&session, future) && session_send_expect(&session, "\r", "schedule updated"),
          "Custom date apply failed");
    CHECK(keyboard_schedule_option(&session, '5', "schedule cleared"),
          "Clear schedule failed");
    CHECK(keyboard_schedule_option(&session, '2', "schedule updated"),
          "final Tomorrow schedule failed");

    CHECK(session_send(&session, "f") && session_wait(&session, "FILTER:Urgent", SESSION_DEADLINE_MS),
          "keyboard filter failed");
    CHECK(session_send(&session, "o") && session_wait(&session, "SORT:Created", SESSION_DEADLINE_MS),
          "keyboard sort failed");
    CHECK(session_send(&session, "ffffooo") && session_wait(&session, "SORT:Smart", SESSION_DEADLINE_MS) &&
          screen_contains(&session.screen, "FILTER:Any"), "filter/sort cycle reset failed");
    CHECK(session_send(&session, "g") && session_settle(&session, 200),
          "selection reset before completion failed");
    CHECK(session_send_expect(&session, "x", "task completed"),
          "complete failed");
    CHECK(session_send_expect(&session, "x", "task reopened"),
          "reopen failed");

    CHECK(session_send(&session, "?") && session_wait(&session, "HELP 1-", SESSION_DEADLINE_MS),
          "Help key open failed");
    CHECK(session_send(&session, "\x1b[6~\x1b[F\x1b[H") && session_settle(&session, 300),
          "Help navigation failed");
    CHECK(screen_contains(&session.screen, "HELP 1-"), "Help Home clamp failed");
    CHECK(session_send_expect(&session, "?", "help closed"),
          "Help key close failed");
    CHECK(session_quit(&session), "keyboard workflow quit failed");
    CHECK(session_restart(&session, true, false) &&
          session_wait(&session, "added by pty edited", SESSION_DEADLINE_MS),
          "production reload did not show durable task");
    CHECK(session_quit(&session), "production reload quit failed");

    Model model;
    CHECK(load_model(session.state_path, &model) && model.version == 3U && model.next_id == 10U &&
          model.count == 9U,
          "keyboard persisted model unreadable");
    const ModelTask *added = model_task(&model, 9U);
    CHECK(added != NULL && strcmp(added->text, "added by pty edited") == 0 && added->priority == 4 &&
          !added->completed && strcmp(added->due, tomorrow) == 0,
          "keyboard durable fields differ");
    CHECK(model_task(&model, 1U) != NULL && model_task(&model, 1U)->priority == 4 &&
          !model_task(&model, 1U)->completed && strcmp(model_task(&model, 1U)->due, overdue) == 0 &&
          model_task(&model, 2U) != NULL && model_task(&model, 2U)->priority == 3 &&
          !model_task(&model, 2U)->completed && strcmp(model_task(&model, 2U)->due, today) == 0 &&
          model_task(&model, 3U) != NULL && strcmp(model_task(&model, 3U)->due, future) == 0 &&
          model_task(&model, 4U) != NULL && model_task(&model, 4U)->due[0] == '\0' &&
          model_task(&model, 5U) != NULL && model_task(&model, 5U)->completed &&
          model_task(&model, 6U) != NULL && strcmp(model_task(&model, 6U)->due, tomorrow) == 0 &&
          model_task(&model, 7U) != NULL && model_task(&model, 7U)->completed &&
          model_task(&model, 8U) != NULL && model_task(&model, 8U)->due[0] == '\0',
          "keyboard workflow changed unrelated fixture fields");
    evidence.saw_urgent_256 = true;
    CHECK(session_cleanup(&session), "keyboard session cleanup failed");
    return true;
}

static bool scenario_mouse_help_modal(void) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "mouse session prepare failed");
    CHECK(seed_v3(session.state_path, true), "mouse fixture seed failed");
    CHECK(session_start(&session, true, false), "mouse session start failed");

    size_t filter_x = 0U, filter_y = 0U;
    CHECK(screen_find_ascii(&session.screen, "FILTER:Any", &filter_x, &filter_y), "filter target missing");
    CHECK(mouse_event(&session, 0U, filter_x + 3U, filter_y, 'M') &&
          mouse_event(&session, 0U, 0U, filter_y, 'm') && session_settle(&session, 200) &&
          screen_contains(&session.screen, "FILTER:Any"), "filter release-outside mutated state");
    CHECK(click_label(&session, "FILTER:Any") && session_wait(&session, "FILTER:Urgent", SESSION_DEADLINE_MS),
          "filter release-inside did not apply exactly once");
    CHECK(click_label(&session, "SORT:Smart") && session_wait(&session, "SORT:Created", SESSION_DEADLINE_MS),
          "sort release-inside failed");

    size_t help_x = 0U, help_y = 0U;
    CHECK(screen_find_ascii(&session.screen, "HELP", &help_x, &help_y), "Help header target missing");
    CHECK(mouse_event(&session, 0U, help_x + 1U, help_y, 'M') &&
          mouse_event(&session, 0U, 0U, help_y, 'm') && session_settle(&session, 200) &&
          !screen_contains(&session.screen, "HELP 1-"), "Help release-outside opened overlay");
    CHECK(mouse_click(&session, help_x + 1U, help_y) &&
          session_wait(&session, "HELP 1-", SESSION_DEADLINE_MS), "Help header release-inside failed");
    CHECK(mouse_click(&session, 85U, 2U) && session_wait(&session, "help closed", SESSION_DEADLINE_MS),
          "Help close same-target failed");

    size_t row_x = 0U, row_y = 0U;
    CHECK(screen_find_ascii(&session.screen, "overdue urgent alpha", &row_x, &row_y),
          "urgent row missing after filter");
    CHECK(mouse_click(&session, 9U, row_y) && session_wait(&session, "Urgent [4]", SESSION_DEADLINE_MS),
          "priority marker did not open picker");
    size_t urgent_x = 0U, urgent_y = 0U;
    CHECK(screen_find_ascii(&session.screen, "Urgent [4]", &urgent_x, &urgent_y) &&
          mouse_click(&session, urgent_x + 2U, urgent_y) &&
          session_wait(&session, "priority changed", SESSION_DEADLINE_MS),
          "picker same-option release failed");
    CHECK(session_send(&session, "p") && session_wait(&session, "High [3]", SESSION_DEADLINE_MS),
          "priority picker reopen failed");
    size_t high_x = 0U, high_y = 0U, normal_x = 0U, normal_y = 0U;
    CHECK(screen_find_ascii(&session.screen, "High [3]", &high_x, &high_y) &&
          screen_find_ascii(&session.screen, "Normal [2]", &normal_x, &normal_y),
          "picker options missing");
    CHECK(mouse_event(&session, 0U, high_x + 2U, high_y, 'M') &&
          mouse_event(&session, 0U, normal_x + 2U, normal_y, 'm') && session_settle(&session, 200) &&
          screen_contains(&session.screen, "PRIORITY"), "picker cross-option release applied");
    CHECK(session_send(&session, "\x1b") && session_wait(&session, "cancelled", SESSION_DEADLINE_MS),
          "picker cancel failed");

    CHECK(session_send(&session, "ffffG") && session_wait(&session, "FILTER:Any", SESSION_DEADLINE_MS),
          "mouse session filter reset failed");
    CHECK(session_send(&session, "?") && session_wait(&session, "HELP 1-", SESSION_DEADLINE_MS),
          "Help context open failed");
    const uint32_t help_top = screen_hash(&session.screen);
    CHECK(session_send(&session, "\x1b[6~") && session_settle(&session, 250) &&
          screen_hash(&session.screen) != help_top, "Help PageDown did not change page");
    CHECK(session_send(&session, "\x1b[F\x1b[F\x1b[<65;2;5M") && session_settle(&session, 250),
          "Help End/wheel clamp failed");
    CHECK(session_send(&session, "a\t") && mouse_click(&session, 30U, 5U) && session_settle(&session, 200) &&
          screen_contains(&session.screen, "HELP"), "Help failed to block background input");
    CHECK(session_resize_pty(&session, 24U, 8U) && screen_contains(&session.screen, "HELP"),
          "Help resize lost overlay");
    CHECK(session_resize_pty(&session, 96U, 24U), "Help restore resize failed");
    CHECK(session_send(&session, "\x1b[H?") && session_wait(&session, "help closed", SESSION_DEADLINE_MS),
          "Help close after Home failed");
    CHECK(screen_contains(&session.screen, "SORT:Created") &&
          screen_contains(&session.screen, "overdue urgent alpha") &&
          screen_contains(&session.screen, "filler task 19"),
          "Help did not restore sort/selection/list scroll");
    CHECK(session_send(&session, "?") && session_wait(&session, "HELP 1-", SESSION_DEADLINE_MS),
          "Help reopen did not reset top");
    CHECK(session_send(&session, "\x1b"), "Help escape failed");
    CHECK(session_wait(&session, "help closed", SESSION_DEADLINE_MS), "Help escape close missing");

    CHECK(session_send(&session, "p") && session_wait(&session, "PRIORITY", SESSION_DEADLINE_MS),
          "picker resize open failed");
    CHECK(session_resize_pty(&session, 24U, 8U) && screen_contains(&session.screen, "Urgent [4]"),
          "picker resize lost options");
    CHECK(session_resize_pty(&session, 96U, 24U) && session_send(&session, "\x1b"),
          "picker resize restore failed");
    CHECK(session_wait(&session, "cancelled", SESSION_DEADLINE_MS), "picker resize cancel missing");

    CHECK(session_send(&session, "a") && session_wait(&session, "ADD TASK", SESSION_DEADLINE_MS),
          "text modal open failed");
    CHECK(click_label(&session, "FILTER:Any") && session_send(&session, "\t") &&
          session_settle(&session, 200) && screen_contains(&session.screen, "ADD TASK") &&
          screen_contains(&session.screen, "FILTER:Any"), "modal allowed background input");
    CHECK(session_send(&session, "\x1b") && session_wait(&session, "cancelled", SESSION_DEADLINE_MS),
          "text modal cancel failed");

    static const char malformed[] = "\x1b[<0;0;1M";
    CHECK(write_all(session.master, malformed, sizeof(malformed) - 1U) && session_settle(&session, 200),
          "malformed SGR handling failed");
    CHECK(screen_find_ascii(&session.screen, "FILTER:Any", &filter_x, &filter_y),
          "filter missing before split SGR");
    char press[64], release[64];
    const int press_length = snprintf(press, sizeof(press), "\x1b[<0;%zu;%zuM", filter_x + 3U, filter_y + 1U);
    const int release_length = snprintf(release, sizeof(release), "\x1b[<0;%zu;%zum", filter_x + 3U, filter_y + 1U);
    CHECK(press_length > 4 && release_length > 4 &&
          write_all(session.master, press, 4U) &&
          write_all(session.master, press + 4U, (size_t)press_length - 4U) &&
          write_all(session.master, release, 3U) &&
          write_all(session.master, release + 3U, (size_t)release_length - 3U) &&
          session_wait(&session, "FILTER:Urgent", SESSION_DEADLINE_MS), "split SGR activation failed");

    CHECK(session_quit(&session), "mouse session quit failed");
    Model model;
    CHECK(load_model(session.state_path, &model) && model_task(&model, 1U)->priority == 4 &&
          model.next_id == 31U, "mouse scenario persistence changed unexpectedly");
    CHECK(session_cleanup(&session), "mouse session cleanup failed");
    return true;
}

static bool set_all_tab(Session *session) {
    CHECK(click_label(session, "ALL") &&
          session_wait(session, "filter changed", SESSION_DEADLINE_MS),
          "return to All failed");
    return true;
}

static bool scenario_drag_normal(void) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "drag session prepare failed");
    CHECK(seed_v3(session.state_path, false), "drag fixture seed failed");
    CHECK(session_start(&session, false, false), "drag session start failed");
    CHECK(session_settle(&session, 800), "startup animation did not settle");
    evidence.startup_hash = transcript_hash(&session.transcript);

    size_t source_x = 0U, source_y = 0U, target_x = 0U, target_y = 0U;
    CHECK(screen_find_row_title(&session.screen, "overdue urgent alpha", &source_x, &source_y) &&
          screen_find_ascii(&session.screen, "COMPLETED", &target_x, &target_y),
          "normal drag geometry missing");
    size_t offset = session.transcript.length;
    CHECK(mouse_event(&session, 0U, source_x + 8U, source_y, 'M') && session_settle(&session, 150),
          "drag rest/candidate frame failed");
    CHECK(mouse_event(&session, 32U, target_x + 2U, target_y, 'M') &&
          wait_transcript_since(&session, offset, "DRAG", SESSION_DEADLINE_MS), "lift DRAG frame missing");
    evidence.lift_hash = transcript_hash(&session.transcript);
    CHECK(session_settle(&session, 180), "drag target animation settle failed");
    evidence.target_hash = transcript_hash(&session.transcript);
    CHECK(mouse_event(&session, 0U, target_x + 2U, target_y, 'm') &&
          session_wait(&session, "moved to Completed", SESSION_DEADLINE_MS),
          "Completed drop failed");
    evidence.success_hash = transcript_hash(&session.transcript);
    evidence.saw_drag = transcript_contains_since(&session, offset, "DRAG");
    evidence.saw_moved = screen_contains(&session.screen, "moved to Completed");

    CHECK(drag_title_to_label(&session, "overdue urgent alpha", "TODAY", true, true) &&
          session_wait(&session, "moved to Today", SESSION_DEADLINE_MS), "Today reopen drop failed");
    CHECK(set_all_tab(&session), "All after Today failed");
    CHECK(drag_title_to_label(&session, "completed future source", "UPCOMING", true, true) &&
          session_wait(&session, "moved to Upcoming", SESSION_DEADLINE_MS),
          "Upcoming preserve-future drop failed");
    CHECK(set_all_tab(&session), "All after future drop failed");
    CHECK(drag_title_to_label(&session, "unscheduled low delta", "UPCOMING", true, true) &&
          session_wait(&session, "moved to Upcoming", SESSION_DEADLINE_MS),
          "Upcoming tomorrow drop failed");
    CHECK(set_all_tab(&session), "All after unscheduled drop failed");

    CHECK(drag_title_to_label(&session, "future normal gamma", "ALL", true, true) &&
          session_wait(&session, "moved to All", SESSION_DEADLINE_MS), "All no-op drop failed");
    CHECK(drag_title_to_label(&session, "schedule target", "COMPLETED", false, false) &&
          session_wait(&session, "moved to Completed", SESSION_DEADLINE_MS),
          "authoritative no-motion far release failed");
    CHECK(set_all_tab(&session), "All after far release failed");

    CHECK(screen_find_row_title(&session.screen, "future normal gamma", &source_x, &source_y) &&
          screen_find_ascii(&session.screen, "COMPLETED", &target_x, &target_y),
          "changed-target geometry missing");
    CHECK(mouse_event(&session, 0U, source_x + 8U, source_y, 'M') &&
          mouse_event(&session, 32U, target_x + 2U, target_y, 'M'), "changed-target press/motion failed");
    size_t today_x = 0U, today_y = 0U;
    CHECK(screen_find_ascii(&session.screen, "TODAY", &today_x, &today_y) &&
          mouse_event(&session, 0U, today_x + 2U, today_y, 'm') &&
          session_wait(&session, "moved to Today", SESSION_DEADLINE_MS),
          "release target was not authoritative");
    CHECK(set_all_tab(&session), "All after authoritative release failed");

    size_t first_x = 0U, first_y = 0U, second_x = 0U, second_y = 0U;
    CHECK(screen_find_row_title(&session.screen, "unscheduled low delta", &first_x, &first_y) &&
          screen_find_row_title(&session.screen, "schedule target", &second_x, &second_y),
          "threshold rows missing");
    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') &&
          mouse_event(&session, 0U, first_x + 9U, first_y, 'm') && session_settle(&session, 150),
          "below-threshold same-row selection failed");
    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') &&
          mouse_event(&session, 0U, second_x + 8U, second_y, 'm') && session_settle(&session, 150),
          "below-threshold cross-row cancel failed");

    offset = session.transcript.length;
    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') &&
          mouse_event(&session, 32U, first_x + 10U, first_y, 'M') &&
          mouse_event(&session, 0U, 95U, 23U, 'm') &&
          session_wait(&session, "drag cancelled", SESSION_DEADLINE_MS),
          "outside release did not cancel drag");

    CHECK(screen_find_row_title(&session.screen, "unscheduled low delta", &first_x, &first_y),
          "hidden-target source moved out of view");
    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') && session_settle(&session, 200) &&
          mouse_event(&session, 32U, first_x + 10U, first_y, 'M') &&
          session_wait(&session, "DRAG", SESSION_DEADLINE_MS), "hidden-target drag start failed");
    CHECK(session_resize_pty(&session, 23U, 8U), "drag resize to hidden tabs failed");
    CHECK(session_wait(&session, "DRAG", SESSION_DEADLINE_MS),
          "resize cancelled active drag before release");
    CHECK(mouse_event(&session, 0U, 20U, 1U, 'm') &&
          session_wait(&session, "target unavailable", SESSION_DEADLINE_MS),
          "height/width-hidden target did not reject");
    CHECK(session_resize_pty(&session, 96U, 24U), "drag resize restore failed");

    CHECK(screen_find_row_title(&session.screen, "unscheduled low delta", &first_x, &first_y) &&
          screen_find_ascii(&session.screen, "COMPLETED", &target_x, &target_y),
          "cancel drag geometry missing");
    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') &&
          mouse_event(&session, 32U, target_x + 2U, target_y, 'M') &&
          mouse_event(&session, 65U, target_x, target_y, 'M') &&
          session_wait(&session, "drag cancelled", SESSION_DEADLINE_MS), "wheel drag cancel failed");
    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') &&
          mouse_event(&session, 32U, target_x + 2U, target_y, 'M') && session_send(&session, "\x1b") &&
          session_wait(&session, "drag cancelled", SESSION_DEADLINE_MS), "Escape drag cancel failed");

    offset = session.transcript.length;
    CHECK(mouse_event(&session, 0U, 6U, first_y, 'M') &&
          mouse_event(&session, 32U, target_x + 2U, target_y, 'M') &&
          mouse_event(&session, 0U, target_x + 2U, target_y, 'm') && session_settle(&session, 150) &&
          !transcript_contains_since(&session, offset, "DRAG"), "checkbox incorrectly became drag source");

    CHECK(mouse_event(&session, 0U, first_x + 8U, first_y, 'M') &&
          mouse_event(&session, 32U, target_x + 2U, target_y, 'M') && session_send(&session, "q"),
          "q active-drag path failed");
    CHECK(session_wait_exit(&session, 0), "q active-drag exit failed");

    char today[11], tomorrow[11], next_week[11], future[11], overdue[11];
    CHECK(local_dates(today, tomorrow, next_week, future, overdue), "drag dates failed");
    Model model;
    CHECK(load_model(session.state_path, &model), "drag model load failed");
    const ModelTask *one = model_task(&model, 1U);
    const ModelTask *three = model_task(&model, 3U);
    const ModelTask *four = model_task(&model, 4U);
    const ModelTask *seven = model_task(&model, 7U);
    const ModelTask *eight = model_task(&model, 8U);
    CHECK(one != NULL && !one->completed && strcmp(one->due, today) == 0,
          "Today drop durable model differs");
    CHECK(three != NULL && !three->completed && strcmp(three->due, today) == 0,
          "release-authoritative Today model differs");
    CHECK(four != NULL && !four->completed && strcmp(four->due, tomorrow) == 0,
          "Upcoming tomorrow durable model differs");
    CHECK(seven != NULL && !seven->completed && strcmp(seven->due, future) == 0,
          "Upcoming future-preserving durable model differs");
    CHECK(eight != NULL && eight->completed, "far-release Completed durable model differs");
    CHECK(session_cleanup(&session), "drag session cleanup failed");
    return true;
}

static bool scenario_reduced_narrow_and_signal(void) {
    Session session;
    CHECK(session_prepare(&session, 24U, 8U), "reduced session prepare failed");
    CHECK(seed_v3(session.state_path, false), "reduced fixture seed failed");
    CHECK(session_start(&session, true, false), "reduced session start failed");
    CHECK(screen_contains(&session.screen, "F:A") && screen_contains(&session.screen, "S:S") &&
          screen_contains(&session.screen, "Done"), "24x8 responsive frame incomplete");
    const uint32_t rest = screen_hash(&session.screen);
    CHECK(drag_title_to_label(&session, "overdue", "Done", true, true) &&
          session_wait(&session, "moved to Completed", SESSION_DEADLINE_MS),
          "reduced-motion drag failed");
    CHECK(screen_hash(&session.screen) != rest, "reduced-motion final frame did not settle");
    CHECK(session_send(&session, "[") && session_wait(&session, "All", SESSION_DEADLINE_MS),
          "narrow All return failed");
    CHECK(session_send(&session, "4") && session_wait(&session, "priority changed", SESSION_DEADLINE_MS),
          "signal save mutation failed");
    CHECK(kill(session.child.pid, SIGWINCH) == 0 && kill(session.child.pid, SIGTERM) == 0,
          "repeated signal injection failed");
    CHECK(session_wait_exit(&session, 0), "SIGTERM graceful exit failed");
    Model model;
    CHECK(load_model(session.state_path, &model) && model_task(&model, 1U)->completed,
          "SIGTERM did not persist reduced drag model");
    CHECK(session_cleanup(&session), "reduced session cleanup failed");
    return true;
}

static bool write_literal_file(const char *path, const char *contents) {
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) return false;
    const bool ok = write_all(descriptor, contents, strlen(contents));
    return close(descriptor) == 0 && ok;
}

static bool scenario_legacy(const char *contents, const char *visible_title) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "legacy session prepare failed");
    CHECK(write_literal_file(session.state_path, contents), "legacy seed failed");
    CHECK(session_start(&session, true, true), "legacy session start failed");
    CHECK(session_wait(&session, visible_title, SESSION_DEADLINE_MS), "legacy task not visible");
    CHECK(session_send(&session, "e!\r") && session_wait(&session, "task updated", SESSION_DEADLINE_MS),
          "legacy dirty mutation failed");
    CHECK(session_quit(&session), "legacy quit failed");
    Model model;
    CHECK(load_model(session.state_path, &model) && model.version == 3U && model.count == 1U &&
          model.tasks[0].text[strlen(model.tasks[0].text) - 1U] == '!',
          "legacy dirty write did not canonicalize v3");
    CHECK(session_cleanup(&session), "legacy session cleanup failed");
    return true;
}

static Child spawn_contender(const char *root, int output_fd) {
    Child child = {.pid = fork()};
    if (child.pid != 0) return child;
    const int null_input = open("/dev/null", O_RDONLY);
    if (null_input < 0 || dup2(null_input, STDIN_FILENO) < 0 ||
        dup2(output_fd, STDOUT_FILENO) < 0 || dup2(output_fd, STDERR_FILENO) < 0 ||
        !configure_child_environment(root, true, true)) _exit(126);
    execl("./lowtask", "lowtask", (char *)NULL);
    _exit(127);
}

static bool scenario_lock_loser(void) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "lock session prepare failed");
    CHECK(seed_v3(session.state_path, false), "lock fixture seed failed");
    CHECK(session_start(&session, true, true), "lock owner start failed");
    struct stat before, after;
    CHECK(stat(session.state_path, &before) == 0, "lock pre-stat failed");
    int pipe_fds[2];
    CHECK(pipe(pipe_fds) == 0, "contender pipe failed");
    const int pipe_flags = fcntl(pipe_fds[0], F_GETFL);
    CHECK(pipe_flags >= 0 && fcntl(pipe_fds[0], F_SETFL, pipe_flags | O_NONBLOCK) == 0,
          "contender pipe nonblocking setup failed");
    Child contender = spawn_contender(session.root, pipe_fds[1]);
    active_extra_child = &contender;
    CHECK(contender.pid > 0 && close(pipe_fds[1]) == 0, "contender spawn failed");
    char diagnostic[512] = {0};
    size_t used = 0U;
    const int64_t deadline = monotonic_milliseconds() + SESSION_DEADLINE_MS;
    while (!contender.reaped && monotonic_milliseconds() < deadline) {
        const ssize_t count = read(pipe_fds[0], diagnostic + used, sizeof(diagnostic) - used - 1U);
        if (count > 0) used += (size_t)count;
        else if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) break;
        (void)child_poll_status(&contender);
        if (!contender.reaped) {
            struct pollfd input = {.fd = pipe_fds[0], .events = POLLIN};
            if (poll(&input, 1U, 20) < 0 && errno != EINTR) break;
        }
    }
    CHECK(close(pipe_fds[0]) == 0 && contender.reaped && WIFEXITED(contender.status) &&
          WEXITSTATUS(contender.status) == 1 &&
          strstr(diagnostic, "state is already open or cannot be locked") != NULL,
          "lock loser contract failed");
    active_extra_child = NULL;
    CHECK(stat(session.state_path, &after) == 0 && before.st_dev == after.st_dev &&
          before.st_ino == after.st_ino && before.st_mode == after.st_mode &&
          before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
          before.st_mtim.tv_nsec == after.st_mtim.tv_nsec, "lock loser mutated database");
    CHECK(session_quit(&session), "lock owner quit failed");
    CHECK(session_cleanup(&session), "lock session cleanup failed");
    return true;
}

int main(void) {
    struct rlimit core_limit;
    if (!install_interruption_handlers() || getrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        core_limit.rlim_cur != 0 || setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        fputs("test_pty: FAIL: process preconditions\n", stderr);
        return 1;
    }
    bool ok = scenario_keyboard_workflow() && scenario_mouse_help_modal() &&
              scenario_drag_normal() && scenario_reduced_narrow_and_signal() &&
              scenario_legacy("LOWTASK\t1\nNEXT\t2\nTASK\t1\t3\t0\t6c6567616379206f6e65\n",
                              "legacy one") &&
              scenario_legacy("LOWTASK\t2\nNEXT\t2\nTASK\t1\t1\t0\t2026-07-11\t6c65676163792074776f\n",
                              "legacy two") && scenario_lock_loser();
    if (!ok) return 1;
    int stray_status = 0;
    errno = 0;
    if (waitpid(-1, &stray_status, WNOHANG) != -1 || errno != ECHILD) {
        fputs("test_pty: FAIL: an untracked child remains\n", stderr);
        return 1;
    }
    if (!evidence.saw_drag || !evidence.saw_moved || !evidence.saw_urgent_256 ||
        evidence.startup_hash == evidence.lift_hash || evidence.lift_hash == evidence.target_hash ||
        evidence.target_hash == evidence.success_hash) {
        fputs("test_pty: FAIL: semantic frame evidence incomplete\n", stderr);
        return 1;
    }
    printf("test_pty: workflow startup=%08x lift=%08x target=%08x success=%08x drag=%s moved=%s urgent256=%s\n",
           evidence.startup_hash, evidence.lift_hash, evidence.target_hash, evidence.success_hash,
           evidence.saw_drag ? "yes" : "no", evidence.saw_moved ? "yes" : "no",
           evidence.saw_urgent_256 ? "yes" : "no");
    printf("test_pty: scenarios=keyboard,header-mouse,help,picker,modal,drag,animation,reduced,legacy-v1,legacy-v2,lock,signals bytes=%zu csi=%zu\n",
           evidence.transcript_bytes, evidence.csi_count);
    puts("test_pty: geometry=96x24,24x8 resize=help,picker,drag split-sgr=yes malformed-sgr=yes");
    puts("test_pty: persistence=exact-v3 dynamic-dates=yes lock-unchanged=yes sigterm-save=yes");
    puts("test_pty: termios=restored children=all-reaped temp=clean core-dumps=disabled");
    puts("test_pty: PASS");
    return 0;
}

#undef CHECK

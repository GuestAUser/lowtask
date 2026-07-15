#ifndef LOWTASK_PTY_TEST_API_H
#define LOWTASK_PTY_TEST_API_H

#include "core/date.h"

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>

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
    char description[256];
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

void pty_test_fail(const char *message, const char *file, int line);
bool install_interruption_handlers(void);
bool pty_test_interrupted(void);
const char *select_utf8_locale(void);
const char *pty_test_utf8_locale(void);
const WorkflowEvidence *pty_test_evidence(void);
void pty_test_track_session(Session *session);
void pty_test_untrack_session(Session *session);
void pty_test_track_extra_child(Child *child);
void pty_test_accumulate_transcript(size_t bytes, size_t csi_count);
void pty_test_record_urgent_256(void);
void pty_test_record_drag_startup(uint32_t hash);
void pty_test_record_drag_lift(uint32_t hash);
void pty_test_record_drag_target(uint32_t hash);
void pty_test_record_drag_success(uint32_t hash, bool saw_drag, bool saw_moved);

bool transcript_append(Transcript *transcript, const char *bytes, size_t length);
uint32_t transcript_hash(const Transcript *transcript);
size_t transcript_csi_count(const Transcript *transcript);
bool transcript_contains_since(const Session *session, size_t offset, const char *needle);

bool screen_resize(Screen *screen, size_t columns, size_t rows);
void screen_clear(Screen *screen);
void screen_feed(Screen *screen, const char *bytes, size_t length);
void screen_dump(const Screen *screen);
bool screen_contains(const Screen *screen, const char *needle);
bool screen_find_ascii(const Screen *screen, const char *needle, size_t *x, size_t *y);
bool screen_find_row_title(const Screen *screen, const char *needle, size_t *x, size_t *y);
uint32_t screen_hash(const Screen *screen);

int64_t monotonic_milliseconds(void);
bool child_poll_status(Child *child);
bool stat_mtime_equal(const struct stat *left, const struct stat *right);
bool session_read(Session *session, bool *closed);
bool session_wait(Session *session, const char *needle, int64_t timeout_ms);
bool session_settle(Session *session, int64_t timeout_ms);
bool write_all(int descriptor, const char *bytes, size_t length);
bool session_send(Session *session, const char *bytes);
bool session_send_expect(Session *session, const char *bytes, const char *needle);
bool wait_transcript_since(Session *session, size_t offset, const char *needle,
                           int64_t timeout_ms);

bool local_dates(char today[LOWTASK_DATE_LENGTH + 1U],
                 char tomorrow[LOWTASK_DATE_LENGTH + 1U],
                 char next_week[LOWTASK_DATE_LENGTH + 1U],
                 char future[LOWTASK_DATE_LENGTH + 1U],
                 char overdue[LOWTASK_DATE_LENGTH + 1U]);
bool seed_v3(const char *path, bool fillers);
bool load_model(const char *path, Model *model);
const ModelTask *model_task(const Model *model, uint64_t id);
bool write_literal_file(const char *path, const char *contents);

Child spawn_child(int master, int slave, const char *root, bool reduced, bool ascii);
Child spawn_contender(const char *root, int output_fd);
void terminate_and_reap(Child *child);
bool pty_test_remove_tree(const char *root, const char *data_directory);
bool session_prepare(Session *session, size_t columns, size_t rows);
bool session_start(Session *session, bool reduced, bool ascii);
bool session_restart(Session *session, bool reduced, bool ascii);
bool session_resize_pty(Session *session, size_t columns, size_t rows);
bool session_wait_exit(Session *session, int expected_exit);
bool session_quit(Session *session);
bool session_cleanup(Session *session);

bool mouse_event(Session *session, unsigned int encoded, size_t x, size_t y, char final);
bool mouse_click(Session *session, size_t x, size_t y);
bool click_label(Session *session, const char *label);
bool keyboard_schedule_option(Session *session, char option, const char *status);
bool drag_title_to_label(Session *session, const char *title, const char *label,
                         bool motion, bool expect_drag);
bool set_all_tab(Session *session);

bool scenario_keyboard_workflow(void);
bool scenario_contextual_creation_and_title_edit(void);
bool scenario_mouse_help_modal(void);
bool scenario_drag_normal(void);
bool scenario_reduced_narrow_and_signal(void);
bool scenario_legacy(const char *contents, const char *visible_title);
bool scenario_lock_loser(void);

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        pty_test_fail((message), __FILE__, __LINE__); \
        return false; \
    } \
} while (0)

#endif

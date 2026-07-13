#define _XOPEN_SOURCE 700

#include "tests/pty_test_api.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

static const char terminal_enter[] =
    "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[2J\x1b[H";
static const char terminal_leave[] =
    "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l";

static bool termios_equal(const struct termios *left, const struct termios *right) {
    return left->c_iflag == right->c_iflag && left->c_oflag == right->c_oflag &&
           left->c_cflag == right->c_cflag && left->c_lflag == right->c_lflag &&
           memcmp(left->c_cc, right->c_cc, sizeof(left->c_cc)) == 0;
}

bool session_prepare(Session *session, size_t columns, size_t rows) {
    *session = (Session){.master = -1, .slave = -1, .child = {.pid = -1}};
    pty_test_track_session(session);
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

bool session_start(Session *session, bool reduced, bool ascii) {
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
                CHECK(session_settle(session, SESSION_DEADLINE_MS),
                      "startup frame did not settle");
                CHECK(child_poll_status(&session->child) && !session->child.reaped,
                      "application exited while startup frame settled");
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

bool session_restart(Session *session, bool reduced, bool ascii) {
    CHECK(session->child.reaped, "restart attempted with live child");
    pty_test_accumulate_transcript(session->transcript.length,
                                   transcript_csi_count(&session->transcript));
    session->transcript.length = 0U;
    if (session->transcript.bytes != NULL) session->transcript.bytes[0] = '\0';
    screen_clear(&session->screen);
    session->child = (Child){.pid = -1};
    session->started = false;
    session->entry_offset = 0U;
    session->leave_offset = 0U;
    return session_start(session, reduced, ascii);
}

bool session_resize_pty(Session *session, size_t columns, size_t rows) {
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

bool session_wait_exit(Session *session, int expected_exit) {
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

bool session_quit(Session *session) {
    CHECK(session_send(session, "q"), "quit write failed");
    return session_wait_exit(session, 0);
}

bool session_cleanup(Session *session) {
    terminate_and_reap(&session->child);
    if (session->slave >= 0) (void)close(session->slave);
    if (session->master >= 0) (void)close(session->master);
    const bool cleaned = pty_test_remove_tree(session->root, session->data_directory);
    pty_test_accumulate_transcript(session->transcript.length,
                                   transcript_csi_count(&session->transcript));
    free(session->transcript.bytes);
    free(session->screen.cells);
    *session = (Session){0};
    pty_test_untrack_session(session);
    return cleaned;
}

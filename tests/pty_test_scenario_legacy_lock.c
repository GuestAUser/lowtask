#include "tests/pty_test_api.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

bool scenario_legacy(const char *contents, const char *visible_title) {
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

bool scenario_lock_loser(void) {
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
    pty_test_track_extra_child(&contender);
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
    pty_test_track_extra_child(NULL);
    CHECK(stat(session.state_path, &after) == 0 && before.st_dev == after.st_dev &&
          before.st_ino == after.st_ino && before.st_mode == after.st_mode &&
          before.st_size == after.st_size && stat_mtime_equal(&before, &after),
          "lock loser mutated database");
    CHECK(session_quit(&session), "lock owner quit failed");
    CHECK(session_cleanup(&session), "lock session cleanup failed");
    return true;
}

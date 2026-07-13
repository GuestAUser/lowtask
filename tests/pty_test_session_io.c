#include "tests/pty_test_api.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int64_t monotonic_milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000LL + (int64_t)now.tv_nsec / 1000000LL;
}

bool child_poll_status(Child *child) {
    if (child->reaped) return true;
    const pid_t result = waitpid(child->pid, &child->status, WNOHANG);
    if (result == child->pid) child->reaped = true;
    return result >= 0 || (result < 0 && errno == EINTR);
}

bool stat_mtime_equal(const struct stat *left, const struct stat *right) {
#if defined(__APPLE__)
    return left->st_mtimespec.tv_sec == right->st_mtimespec.tv_sec &&
           left->st_mtimespec.tv_nsec == right->st_mtimespec.tv_nsec;
#else
    return left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
           left->st_mtim.tv_nsec == right->st_mtim.tv_nsec;
#endif
}

bool session_read(Session *session, bool *closed) {
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

bool session_wait(Session *session, const char *needle, int64_t timeout_ms) {
    const int64_t start = monotonic_milliseconds();
    if (start < 0) return false;
    const int64_t deadline = start + timeout_ms;
    bool closed = false;
    for (;;) {
        if (pty_test_interrupted() || !session_read(session, &closed) ||
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

bool session_settle(Session *session, int64_t timeout_ms) {
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

bool write_all(int descriptor, const char *bytes, size_t length) {
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

bool session_send(Session *session, const char *bytes) {
    return write_all(session->master, bytes, strlen(bytes));
}

bool session_send_expect(Session *session, const char *bytes, const char *needle) {
    return session_send(session, bytes) && session_wait(session, needle, SESSION_DEADLINE_MS);
}

bool wait_transcript_since(Session *session, size_t offset, const char *needle,
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

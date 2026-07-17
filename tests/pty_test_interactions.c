#include "tests/pty_test_api.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

bool mouse_event(Session *session, unsigned int encoded, size_t x, size_t y, char final) {
    char sequence[64];
    const int written = snprintf(sequence, sizeof(sequence), "\x1b[<%u;%zu;%zu%c",
                                 encoded, x + 1U, y + 1U, final);
    return written > 0 && (size_t)written < sizeof(sequence) &&
           write_all(session->master, sequence, (size_t)written);
}

bool mouse_click(Session *session, size_t x, size_t y) {
    return mouse_event(session, 0U, x, y, 'M') && mouse_event(session, 0U, x, y, 'm');
}

bool click_label(Session *session, const char *label) {
    size_t x = 0U;
    size_t y = 0U;
    CHECK(screen_find_ascii(&session->screen, label, &x, &y), "mouse label not found");
    CHECK(mouse_click(session, x + strlen(label) / 2U, y), "mouse click failed");
    return true;
}

bool session_wait_tab(Session *session, const char *label, size_t *x, size_t *y) {
    const int64_t start = monotonic_milliseconds();
    if (start < 0) return false;
    const int64_t deadline = start + SESSION_DEADLINE_MS;
    bool closed = false;
    for (;;) {
        if (pty_test_interrupted() || !session_read(session, &closed) ||
            !child_poll_status(&session->child)) return false;
        /* Status and animation text can repeat a label; only row 1 owns tab hit targets. */
        if (screen_find_ascii_row(&session->screen, 1U, label, x)) {
            *y = 1U;
            return true;
        }
        if (closed || session->child.reaped) return false;
        const int64_t now = monotonic_milliseconds();
        if (now < 0 || now >= deadline) return false;
        struct pollfd input = {.fd = session->master, .events = POLLIN};
        const int remaining = (int)(deadline - now);
        const int ready = poll(&input, 1U, remaining > 20 ? 20 : remaining);
        if (ready < 0 && errno != EINTR) return false;
    }
}

bool keyboard_schedule_option(Session *session, char option, const char *status) {
    char input[2] = {option, '\0'};
    return session_send(session, "s") && session_wait(session, "Today [1]", SESSION_DEADLINE_MS) &&
           session_send(session, input) && session_wait(session, status, SESSION_DEADLINE_MS);
}

bool drag_title_to_label(Session *session, const char *title, const char *label,
                         bool motion, bool expect_drag) {
    size_t source_x = 0U, source_y = 0U, target_x = 0U, target_y = 0U;
    CHECK(session_wait_tab(session, label, &target_x, &target_y), "drag target not visible");
    CHECK(screen_find_row_title(&session->screen, title, &source_x, &source_y), "drag source not visible");
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

bool set_all_tab(Session *session) {
    size_t x = 0U;
    size_t y = 0U;
    CHECK(session_wait_tab(session, "ALL", &x, &y) && mouse_click(session, x + 1U, y) &&
          session_wait(session, "filter changed", SESSION_DEADLINE_MS),
          "return to All failed");
    return true;
}

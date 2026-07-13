#define _XOPEN_SOURCE 700

#include "tests/pty_test_api.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    volatile sig_atomic_t interrupted;
    WorkflowEvidence evidence;
    Session *active_session;
    Child *active_extra_child;
    const char *selected_utf8_locale;
} TestRuntime;

static TestRuntime runtime;

static void interruption_handler(int signal_number) {
    (void)signal_number;
    runtime.interrupted = 1;
}

bool install_interruption_handlers(void) {
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

bool pty_test_interrupted(void) {
    return runtime.interrupted != 0;
}

const char *select_utf8_locale(void) {
    static const char *const candidates[] = {
        "C.UTF-8",
        "C.utf8",
        "en_US.UTF-8",
        "en_US.utf8"
    };
    for (size_t index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (setlocale(LC_CTYPE, candidates[index]) != NULL) {
            runtime.selected_utf8_locale = candidates[index];
            return runtime.selected_utf8_locale;
        }
    }
    return NULL;
}

const char *pty_test_utf8_locale(void) {
    return runtime.selected_utf8_locale;
}

const WorkflowEvidence *pty_test_evidence(void) {
    return &runtime.evidence;
}

void pty_test_track_session(Session *session) {
    runtime.active_session = session;
}

void pty_test_untrack_session(Session *session) {
    if (runtime.active_session == session) runtime.active_session = NULL;
}

void pty_test_track_extra_child(Child *child) {
    runtime.active_extra_child = child;
}

void pty_test_accumulate_transcript(size_t bytes, size_t csi_count) {
    runtime.evidence.transcript_bytes += bytes;
    runtime.evidence.csi_count += csi_count;
}

void pty_test_record_urgent_256(void) {
    runtime.evidence.saw_urgent_256 = true;
}

void pty_test_record_drag_startup(uint32_t hash) {
    runtime.evidence.startup_hash = hash;
}

void pty_test_record_drag_lift(uint32_t hash) {
    runtime.evidence.lift_hash = hash;
}

void pty_test_record_drag_target(uint32_t hash) {
    runtime.evidence.target_hash = hash;
}

void pty_test_record_drag_success(uint32_t hash, bool saw_drag, bool saw_moved) {
    runtime.evidence.success_hash = hash;
    runtime.evidence.saw_drag = saw_drag;
    runtime.evidence.saw_moved = saw_moved;
}

void pty_test_fail(const char *message, const char *file, int line) {
    fprintf(stderr, "test_pty: FAIL: %s (%s:%d)\n", message, file, line);
    if (runtime.active_extra_child != NULL) {
        terminate_and_reap(runtime.active_extra_child);
        runtime.active_extra_child = NULL;
    }
    if (runtime.active_session != NULL) {
        screen_dump(&runtime.active_session->screen);
        (void)session_cleanup(runtime.active_session);
    }
}

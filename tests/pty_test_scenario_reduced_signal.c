#include "tests/pty_test_api.h"

#include <signal.h>

bool scenario_reduced_narrow_and_signal(void) {
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

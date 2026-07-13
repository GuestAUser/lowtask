#include "tests/pty_test_api.h"

#include <string.h>

bool scenario_drag_normal(void) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "drag session prepare failed");
    CHECK(seed_v3(session.state_path, false), "drag fixture seed failed");
    CHECK(session_start(&session, false, false), "drag session start failed");
    CHECK(session_settle(&session, 800), "startup animation did not settle");
    pty_test_record_drag_startup(transcript_hash(&session.transcript));

    size_t source_x = 0U, source_y = 0U, target_x = 0U, target_y = 0U;
    CHECK(screen_find_row_title(&session.screen, "overdue urgent alpha", &source_x, &source_y) &&
          screen_find_ascii(&session.screen, "COMPLETED", &target_x, &target_y),
          "normal drag geometry missing");
    size_t offset = session.transcript.length;
    CHECK(mouse_event(&session, 0U, source_x + 8U, source_y, 'M') && session_settle(&session, 150),
          "drag rest/candidate frame failed");
    CHECK(mouse_event(&session, 32U, target_x + 2U, target_y, 'M') &&
          wait_transcript_since(&session, offset, "DRAG", SESSION_DEADLINE_MS), "lift DRAG frame missing");
    pty_test_record_drag_lift(transcript_hash(&session.transcript));
    CHECK(session_settle(&session, 180), "drag target animation settle failed");
    pty_test_record_drag_target(transcript_hash(&session.transcript));
    CHECK(mouse_event(&session, 0U, target_x + 2U, target_y, 'm') &&
          session_wait(&session, "moved to Completed", SESSION_DEADLINE_MS),
          "Completed drop failed");
    pty_test_record_drag_success(
        transcript_hash(&session.transcript),
        transcript_contains_since(&session, offset, "DRAG"),
        screen_contains(&session.screen, "moved to Completed"));

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
    offset = session.transcript.length;
    CHECK(mouse_event(&session, 0U, source_x + 8U, source_y, 'M') &&
          mouse_event(&session, 32U, target_x + 2U, target_y, 'M') &&
          wait_transcript_since(&session, offset, "DRAG", SESSION_DEADLINE_MS) &&
          session_settle(&session, 180),
          "changed-target press/motion failed");
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

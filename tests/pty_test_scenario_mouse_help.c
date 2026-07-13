#include "tests/pty_test_api.h"

#include <stdio.h>

bool scenario_mouse_help_modal(void) {
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
    CHECK(session_send(&session, "p") && session_wait(&session, "Low [1]", SESSION_DEADLINE_MS),
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
    CHECK(session_send(&session, "\x1b[6~") && session_wait(&session, "HELP 19-", SESSION_DEADLINE_MS) &&
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

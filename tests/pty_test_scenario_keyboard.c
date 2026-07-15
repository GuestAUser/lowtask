#include "tests/pty_test_api.h"

#include <string.h>

bool scenario_keyboard_workflow(void) {
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
    CHECK(load_model(session.state_path, &model) && model.version == 4U && model.next_id == 10U &&
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
    pty_test_record_urgent_256();
    CHECK(session_cleanup(&session), "keyboard session cleanup failed");
    return true;
}

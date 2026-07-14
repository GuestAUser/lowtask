#include "tests/pty_test_api.h"

#include <string.h>

bool scenario_contextual_creation_and_title_edit(void) {
    Session session;
    CHECK(session_prepare(&session, 96U, 24U), "context session prepare failed");
    CHECK(session_start(&session, true, false), "context session start failed");

    CHECK(session_send_expect(&session, "aall context\r", "task added"),
          "All contextual add failed");
    CHECK(session_send_expect(&session, "\t", "filter changed") &&
          session_send_expect(&session, "atoday context\r", "task added"),
          "Today contextual add failed");
    CHECK(session_send_expect(&session, "\t", "filter changed") &&
          session_send_expect(&session, "aupcoming context\r", "task added"),
          "Upcoming contextual add failed");
    CHECK(session_send_expect(&session, "\t", "filter changed") &&
          session_send_expect(&session, "acompleted context\r", "task added"),
          "Completed contextual add failed");

    size_t title_x = 0U;
    size_t title_y = 0U;
    CHECK(screen_find_row_title(&session.screen, "completed context", &title_x, &title_y),
          "completed contextual title missing");
    CHECK(mouse_click(&session, title_x + 2U, title_y) &&
          session_wait(&session, "EDIT TASK", SESSION_DEADLINE_MS),
          "title click did not open editor");
    CHECK(session_send_expect(&session, " edited\r", "task updated") &&
          screen_contains(&session.screen, "completed context edited"),
          "mouse title edit did not update row");

    CHECK(session_quit(&session), "context session quit failed");
    char today[11], tomorrow[11], next_week[11], future[11], overdue[11];
    CHECK(local_dates(today, tomorrow, next_week, future, overdue),
          "context dates unavailable");
    Model model;
    CHECK(load_model(session.state_path, &model) && model.version == 3U &&
          model.next_id == 5U && model.count == 4U,
          "context model unreadable");
    const ModelTask *all = model_task(&model, 1U);
    const ModelTask *today_task = model_task(&model, 2U);
    const ModelTask *upcoming = model_task(&model, 3U);
    const ModelTask *completed = model_task(&model, 4U);
    CHECK(all != NULL && !all->completed && all->due[0] == '\0',
          "All add inherited unexpected state");
    CHECK(today_task != NULL && !today_task->completed &&
          strcmp(today_task->due, today) == 0,
          "Today add did not inherit today");
    CHECK(upcoming != NULL && !upcoming->completed &&
          strcmp(upcoming->due, tomorrow) == 0,
          "Upcoming add did not default to tomorrow");
    CHECK(completed != NULL && completed->completed && completed->due[0] == '\0' &&
          strcmp(completed->text, "completed context edited") == 0,
          "Completed add or title edit was not durable");
    CHECK(session_cleanup(&session), "context session cleanup failed");
    return true;
}

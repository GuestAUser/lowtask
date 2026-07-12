#ifndef LOWTASK_CORE_STATE_H
#define LOWTASK_CORE_STATE_H

#include "core/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_ADD,
    APP_MODE_EDIT,
    APP_MODE_SCHEDULE,
    APP_MODE_PRIORITY_PICKER,
    APP_MODE_SCHEDULE_PICKER,
    APP_MODE_HELP
} AppMode;

typedef enum {
    APP_TAB_ALL = 0,
    APP_TAB_TODAY,
    APP_TAB_UPCOMING,
    APP_TAB_COMPLETED,
    APP_TAB_COUNT
} AppTab;

typedef enum {
    APP_PRIORITY_FILTER_ANY = 0,
    APP_PRIORITY_FILTER_URGENT,
    APP_PRIORITY_FILTER_HIGH,
    APP_PRIORITY_FILTER_NORMAL,
    APP_PRIORITY_FILTER_LOW,
    APP_PRIORITY_FILTER_COUNT
} AppPriorityFilter;

typedef enum {
    APP_SORT_SMART = 0,
    APP_SORT_CREATED,
    APP_SORT_DUE,
    APP_SORT_PRIORITY,
    APP_SORT_COUNT
} AppSort;

typedef enum {
    APP_GROUP_NONE = 0,
    APP_GROUP_OVERDUE,
    APP_GROUP_DUE_TODAY,
    APP_GROUP_TOMORROW,
    APP_GROUP_NEXT_SEVEN_DAYS,
    APP_GROUP_LATER
} AppGroup;

typedef enum {
    APP_DISPLAY_ROW_NONE = 0,
    APP_DISPLAY_ROW_TASK,
    APP_DISPLAY_ROW_GROUP
} AppDisplayRowKind;

typedef enum {
    APP_EFFECT_NONE = 0,
    APP_EFFECT_ADD,
    APP_EFFECT_EDIT,
    APP_EFFECT_COMPLETE,
    APP_EFFECT_DELETE,
    APP_EFFECT_TAB,
    APP_EFFECT_MOVE
} AppEffect;

typedef enum {
    APP_OPTION_NONE = 0,
    APP_OPTION_PRIORITY,
    APP_OPTION_SCHEDULE
} AppOptionKind;

typedef enum {
    APP_SCHEDULE_TODAY = 1,
    APP_SCHEDULE_TOMORROW,
    APP_SCHEDULE_NEXT_WEEK,
    APP_SCHEDULE_CUSTOM,
    APP_SCHEDULE_CLEAR
} AppScheduleOption;

typedef struct {
    AppOptionKind kind;
    unsigned int value;
} AppOptionPayload;

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_SET_TAB,
    APP_ACTION_SELECT_TASK,
    APP_ACTION_ADD_TASK,
    APP_ACTION_EDIT_TASK,
    APP_ACTION_EDIT_SCHEDULE,
    APP_ACTION_TOGGLE_TASK,
    APP_ACTION_DELETE_TASK,
    APP_ACTION_OPEN_PRIORITY_PICKER,
    APP_ACTION_APPLY_OPTION,
    APP_ACTION_CYCLE_PRIORITY_FILTER,
    APP_ACTION_CYCLE_SORT,
    APP_ACTION_OPEN_HELP,
    APP_ACTION_CLOSE_HELP,
    APP_ACTION_UNAVAILABLE_TAB_TARGET
} AppActionType;

typedef struct {
    AppActionType type;
    AppTab tab;
    uint64_t task_id;
    AppOptionPayload option;
} AppAction;

typedef struct {
    uint64_t task_id;
    size_t raw_index;
    uint64_t sort_keys[5];
} AppDisplayEntry;

typedef struct {
    AppGroup group;
    size_t first_task;
    size_t task_count;
} AppGroupBoundary;

typedef struct {
    AppDisplayRowKind kind;
    AppGroup group;
    size_t task_ordinal;
    uint64_t task_id;
} AppDisplayRow;

typedef struct AppState {
    struct AppState *owner;
    TaskList *tasks;
    size_t selected;
    uint64_t selected_task_id;
    AppTab tab;
    AppTab previous_tab;
    AppPriorityFilter priority_filter;
    AppSort sort;
    char today[LOWTASK_DUE_DATE_LENGTH + 1U];
    AppDisplayEntry *entries;
    AppDisplayEntry *merge_scratch;
    size_t entry_count;
    size_t entry_capacity;
    uint64_t cache_revision;
    AppTab cache_tab;
    AppPriorityFilter cache_priority_filter;
    AppSort cache_sort;
    char cache_today[LOWTASK_DUE_DATE_LENGTH + 1U];
    AppGroupBoundary groups[3];
    size_t group_count;
    AppMode mode;
    uint64_t modal_task_id;
    size_t focused_option;
    char input[LOWTASK_TEXT_MAX + 1U];
    size_t input_length;
    float list_scroll;
    size_t help_scroll;
    size_t help_line_count;
    size_t help_page_rows;
    uint64_t help_saved_selected_task_id;
    size_t help_saved_selected;
    AppTab help_saved_tab;
    AppPriorityFilter help_saved_priority_filter;
    AppSort help_saved_sort;
    float help_saved_list_scroll;
    bool help_saved_context;
    char status[160];
    bool dirty;
    bool quit;
    AppEffect effect;
    size_t effect_index;
    uint64_t effect_task_id;
    float effect_elapsed;
    float effect_duration;
    uint64_t pending_delete_id;
    AppAction hovered_action;
    AppAction pressed_action;
    bool drag_candidate;
    bool drag_active;
    uint64_t drag_task_id;
    AppAction drag_source_action;
    AppTab drag_candidate_tab;
    AppTab drag_target_tab;
    bool drag_target_valid;
    bool drag_target_date_unavailable;
    uint16_t drag_press_column;
    uint16_t drag_press_row;
    uint16_t drag_current_column;
    uint16_t drag_current_row;
    char drag_source_title[LOWTASK_TEXT_MAX + 1U];
    float drag_lift_elapsed;
    float drag_lift_duration;
} AppState;

bool app_state_init(AppState *state, TaskList *tasks);
void app_state_dispose(AppState *state);
bool app_state_is_initialized(const AppState *state);
bool app_state_reserve(AppState *state, size_t needed);
bool app_state_refresh(AppState *state);
bool app_state_cache_is_current(const AppState *state);
bool app_state_set_today(AppState *state, const char *today);
bool app_state_set_tab(AppState *state, AppTab tab);
bool app_state_cycle_tab(AppState *state, int direction);
bool app_state_set_priority_filter(AppState *state, AppPriorityFilter filter);
bool app_state_cycle_priority_filter(AppState *state);
bool app_state_set_sort(AppState *state, AppSort sort);
bool app_state_cycle_sort(AppState *state);
bool app_state_interaction_locked(const AppState *state);
void app_state_set_list_scroll(AppState *state, float scroll);
void app_state_set_help_metrics(AppState *state, size_t line_count, size_t page_rows);
bool app_state_task_visible(const AppState *state, const Task *task);
size_t app_state_count_tasks(const TaskList *tasks, AppTab tab, AppPriorityFilter filter,
                             const char *today);
size_t app_state_visible_count(const AppState *state);
size_t app_state_visible_task_index(const AppState *state, size_t visible_index);
Task *app_state_visible_task(AppState *state, size_t visible_index);
const Task *app_state_visible_task_const(const AppState *state, size_t visible_index);
Task *app_state_selected_task(AppState *state);
const Task *app_state_selected_task_const(const AppState *state);
uint64_t app_state_selected_task_id(const AppState *state);
bool app_state_select_task_id(AppState *state, uint64_t task_id);
bool app_state_select_visible(AppState *state, size_t visible_index);
bool app_state_move_selection(AppState *state, int direction, size_t steps);
bool app_state_reconcile_selection(AppState *state);
size_t app_state_group_count(const AppState *state);
const AppGroupBoundary *app_state_group(const AppState *state, size_t group_index);
size_t app_state_display_row_count(const AppState *state, size_t viewport_rows);
AppDisplayRow app_state_display_row(const AppState *state, size_t display_row,
                                    size_t viewport_rows);
size_t app_state_display_window_start(const AppState *state, size_t requested_start,
                                      size_t viewport_rows);
void app_state_finish_pending_delete(AppState *state);
void app_state_clear_move_feedback(AppState *state);
void app_state_update(AppState *state, float delta_seconds);
float app_state_effect_progress(const AppState *state);

#endif

#ifndef LOWTASK_TUI_VIEW_H
#define LOWTASK_TUI_VIEW_H

#include "core/state.h"
#include "tui/render.h"

#include <stdbool.h>
#include <stddef.h>

enum {
    TUI_MINIMAL_COLUMNS = 24,
    TUI_NARROW_COLUMNS = 40,
    TUI_STANDARD_COLUMNS = 64,
    TUI_WIDE_COLUMNS = 96,
    TUI_ADD_SHIFT_CELLS = 4,
    TUI_DELETE_SHIFT_CELLS = 6,
    TUI_MODAL_MAX_COLUMNS = 60
};

typedef enum {
    TUI_MODE_NORMAL = 0,
    TUI_MODE_ADD,
    TUI_MODE_EDIT,
    TUI_MODE_SCHEDULE,
    TUI_MODE_PRIORITY_PICKER,
    TUI_MODE_SCHEDULE_PICKER,
    TUI_MODE_HELP
} TuiMode;

typedef enum {
    TUI_EFFECT_NONE = 0,
    TUI_EFFECT_ADD,
    TUI_EFFECT_EDIT,
    TUI_EFFECT_COMPLETE,
    TUI_EFFECT_DELETE,
    TUI_EFFECT_TAB,
    TUI_EFFECT_MOVE
} TuiEffect;

typedef struct {
    const AppState *app;
    size_t selected;
    float scroll;
    float panel_progress;
    bool ascii;
    TuiMode mode;
    const char *input;
    const char *status;
    TuiEffect effect;
    size_t effect_index;
    float effect_progress;
} TuiViewState;

typedef struct {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
} TuiRect;

typedef struct {
    TuiRect header;
    TuiRect tabs;
    TuiRect panel;
    TuiRect rows;
    TuiRect status;
    TuiRect filter_target;
    TuiRect sort_target;
    TuiRect help_target;
    TuiRect tab_targets[APP_TAB_COUNT];
    TuiRect inspector;
    TuiRect context;
    TuiRect help_overlay;
    TuiRect help_body;
    TuiRect help_close;
    TuiRect picker;
    TuiRect picker_options[5];
    size_t first_visible;
    size_t visible_rows;
    size_t pinned_row;
    size_t pinned_visible_index;
    bool panel_framed;
    bool selection_pinned;
    bool tabs_compact;
    bool tabs_show_counts;
} TuiLayout;

typedef enum {
    TUI_HIT_NONE = 0,
    TUI_HIT_TAB,
    TUI_HIT_TASK,
    TUI_HIT_TASK_TITLE,
    TUI_HIT_CHECK,
    TUI_HIT_PRIORITY,
    TUI_HIT_DATE,
    TUI_HIT_ADD,
    TUI_HIT_FILTER,
    TUI_HIT_SORT,
    TUI_HIT_HELP,
    TUI_HIT_HELP_CLOSE,
    TUI_HIT_PICKER_OPTION
} TuiHitKind;

typedef struct {
    TuiHitKind kind;
    size_t visible_index;
    AppAction action;
} TuiHit;

bool tui_layout_compute(size_t width, size_t height, const TuiViewState *view, TuiLayout *layout);
size_t tui_date_column_width(size_t terminal_width);
size_t tui_task_row_shift(const TuiViewState *view, uint64_t task_id, size_t visible_index);
void tui_task_title_bounds(const TuiViewState *view, const TuiLayout *layout,
                           uint64_t task_id, size_t visible_index,
                           size_t *title_x, size_t *title_width);
size_t tui_task_display_row(const AppState *state, uint64_t task_id, size_t viewport_rows);
void tui_help_metrics(size_t width, size_t height, bool ascii, size_t *line_count,
                      size_t *page_rows);
TuiHit tui_hit_test(size_t width, size_t height, size_t x, size_t y,
                    const TuiViewState *view);
void tui_draw(Renderer *renderer, const TaskList *tasks, const TuiViewState *view);

#endif

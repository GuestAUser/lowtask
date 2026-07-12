#ifndef LOWTASK_TUI_VIEW_COMMON_H
#define LOWTASK_TUI_VIEW_COMMON_H

#include "tui/animation.h"
#include "tui/color.h"
#include "tui/view.h"

uint32_t tui_view_color(TuiColorToken token);
RendererStyle tui_view_style(TuiColorToken foreground, TuiColorToken background,
                               unsigned attributes);
uint32_t tui_view_decode_codepoint(const unsigned char **cursor);
size_t tui_view_display_cells(const char *text);
const char *tui_view_suffix_that_fits(const char *text, size_t maximum_cells);
void tui_view_put(Renderer *renderer, size_t x, size_t y, const char *text, size_t limit,
                  RendererStyle cell_style);
void tui_view_put_truncated(Renderer *renderer, size_t x, size_t y, const char *text,
                            size_t limit, bool ascii, RendererStyle cell_style);
void tui_view_draw_box(Renderer *renderer, TuiRect rectangle, bool ascii,
                       RendererStyle box_style);
bool tui_view_action_equal(AppAction first, AppAction second);
const char *tui_view_tab_name(AppTab tab, bool compact);
const char *tui_view_tab_title(AppTab tab, bool compact);
TuiColorToken tui_view_priority_color(TaskPriority priority);
void tui_view_date_label(const AppState *app, const Task *task, size_t width, bool ascii,
                         char output[32], TuiColorToken *token, unsigned *attributes);

void tui_view_draw_header(Renderer *renderer, const TuiLayout *layout,
                          const TuiViewState *view);
void tui_view_draw_tabs(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view);
void tui_view_draw_task(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view, const Task *task,
                        size_t visible_index, size_t row);
void tui_view_draw_selected_context(Renderer *renderer, const TuiLayout *layout,
                                    const TuiViewState *view);
void tui_view_draw_group(Renderer *renderer, const TuiLayout *layout,
                         AppGroup group, size_t row);
void tui_view_draw_drag_ghost(Renderer *renderer, const TuiLayout *layout,
                              const TuiViewState *view);
void tui_view_draw_empty(Renderer *renderer, const TuiLayout *layout,
                         const TuiViewState *view);
void tui_view_draw_grid(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view, size_t used_rows);
void tui_view_draw_picker(Renderer *renderer, const TuiLayout *layout,
                          const TuiViewState *view);
void tui_view_draw_help(Renderer *renderer, const TuiLayout *layout,
                        const TuiViewState *view);
void tui_view_draw_modal(Renderer *renderer, const TuiViewState *view);
void tui_view_draw_status(Renderer *renderer, const TuiLayout *layout,
                          const TuiViewState *view);

#endif

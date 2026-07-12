#ifndef LOWTASK_CORE_VIEW_SORT_H
#define LOWTASK_CORE_VIEW_SORT_H

#include "core/state.h"

bool view_sort_valid(AppSort sort);
void view_sort_fill_keys(AppDisplayEntry *entry, const Task *task, const AppState *state);
void view_sort_entries(AppState *state, size_t count);
void view_sort_rebuild_groups(AppState *state);

#endif

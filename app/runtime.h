#ifndef LOWTASK_APP_RUNTIME_H
#define LOWTASK_APP_RUNTIME_H

#include "core/state.h"
#include "platform/terminal.h"
#include "tui/render.h"

int app_runtime_run(AppState *state, Terminal *terminal, Renderer *renderer);

#endif

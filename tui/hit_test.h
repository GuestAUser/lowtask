#ifndef LOWTASK_TUI_HIT_TEST_H
#define LOWTASK_TUI_HIT_TEST_H

#include "tui/view.h"

TuiHit tui_hit_test(size_t width, size_t height, size_t x, size_t y,
                    const TuiViewState *view);

#endif

#ifndef LOWTASK_TUI_ANIMATION_H
#define LOWTASK_TUI_ANIMATION_H

#include <stdbool.h>

typedef struct {
    float start;
    float value;
    float target;
    float elapsed;
    float duration;
    bool active;
} Tween;

float animation_clamp_progress(float value);
float ease_out_cubic(float progress);
bool animation_reduced_motion_enabled(void);
float animation_motion_progress(float progress);
void tween_init(Tween *tween, float value);
void tween_to(Tween *tween, float target, float duration);
bool tween_update(Tween *tween, float delta_seconds);

#endif

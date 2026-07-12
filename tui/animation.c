#include "tui/animation.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

float animation_clamp_progress(float value) {
    if (!isfinite(value)) return 0.0F;
    if (value < 0.0F) return 0.0F;
    if (value > 1.0F) return 1.0F;
    return value;
}

float ease_out_cubic(float progress) {
    const float inverse = 1.0F - animation_clamp_progress(progress);
    return 1.0F - inverse * inverse * inverse;
}

bool animation_reduced_motion_enabled(void) {
    const char *value = getenv("LOWTASK_REDUCE_MOTION");
    return value != NULL && strcmp(value, "0") != 0;
}

float animation_motion_progress(float progress) {
    return animation_reduced_motion_enabled() ? 1.0F : ease_out_cubic(progress);
}

void tween_init(Tween *tween, float value) {
    if (tween != NULL) {
        *tween = (Tween){.start = value, .value = value, .target = value};
    }
}

void tween_to(Tween *tween, float target, float duration) {
    if (tween == NULL) {
        return;
    }
    tween->start = tween->value;
    tween->target = target;
    tween->elapsed = 0.0F;
    tween->duration = duration > 0.0F ? duration : 0.0F;
    tween->active = tween->duration > 0.0F && tween->start != target;
    if (!tween->active) {
        tween->value = target;
    }
}

bool tween_update(Tween *tween, float delta_seconds) {
    if (tween == NULL || !tween->active) {
        return false;
    }
    if (delta_seconds > 0.0F) {
        tween->elapsed += delta_seconds;
    }
    float progress = tween->elapsed / tween->duration;
    if (progress >= 1.0F) {
        tween->value = tween->target;
        tween->active = false;
    } else {
        const float eased = ease_out_cubic(progress);
        tween->value = tween->start + (tween->target - tween->start) * eased;
    }
    return true;
}

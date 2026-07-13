#include "app/runtime_input.h"

#define INPUT_FLUSH_SECONDS 0.025

bool runtime_input_pending(const InputDecoder *decoder) {
    return decoder->length > 0U || decoder->discarding_control_sequence;
}

double runtime_input_deadline(const InputDecoder *decoder, double now) {
    return runtime_input_pending(decoder) ? now + INPUT_FLUSH_SECONDS : 0.0;
}

static int milliseconds_until(double deadline, double now, int maximum) {
    const double remaining = deadline - now;
    if (remaining <= 0.0) return 0;
    const int milliseconds = (int)(remaining * 1000.0 + 0.999);
    return milliseconds > maximum ? maximum : milliseconds;
}

int runtime_poll_timeout(const InputDecoder *decoder, double input_deadline,
                         bool needs_frame, bool animating, double next_frame, double now) {
    if (runtime_input_pending(decoder)) return milliseconds_until(input_deadline, now, 25);
    if (!needs_frame && !animating) return 250;
    return milliseconds_until(next_frame, now, 17);
}

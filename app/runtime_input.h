#ifndef LOWTASK_APP_RUNTIME_INPUT_H
#define LOWTASK_APP_RUNTIME_INPUT_H

#include "input/input.h"

#include <stdbool.h>

bool runtime_input_pending(const InputDecoder *decoder);
double runtime_input_deadline(const InputDecoder *decoder, double now);
int runtime_poll_timeout(const InputDecoder *decoder, double input_deadline,
                         bool needs_frame, bool animating, double next_frame, double now);

#endif

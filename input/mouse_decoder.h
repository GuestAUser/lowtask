#ifndef LOWTASK_INPUT_MOUSE_DECODER_H
#define LOWTASK_INPUT_MOUSE_DECODER_H

#include "input/input.h"

#include <stdbool.h>

typedef enum {
    INPUT_SGR_MOUSE_NOT_MATCHED = 0,
    INPUT_SGR_MOUSE_INCOMPLETE,
    INPUT_SGR_MOUSE_INVALID,
    INPUT_SGR_MOUSE_EMITTED
} InputSgrMouseResult;

void input_decoder_consume(InputDecoder *decoder, size_t count);
bool input_control_final(unsigned char byte);
bool input_discard_control_candidate(InputDecoder *decoder, bool flushing);
InputSgrMouseResult input_parse_sgr_mouse(InputDecoder *decoder, InputEvent *event, bool flushing);

#endif

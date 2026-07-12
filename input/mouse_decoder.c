#include "input/mouse_decoder.h"

#include <string.h>

enum {
    SGR_MOUSE_MAX_BYTES = 48
};

void input_decoder_consume(InputDecoder *decoder, size_t count) {
    if (count < decoder->length) {
        memmove(decoder->pending, decoder->pending + count, decoder->length - count);
    }
    decoder->length -= count;
}

bool input_control_final(unsigned char byte) {
    return byte >= 0x40U && byte <= 0x7eU;
}

bool input_discard_control_candidate(InputDecoder *decoder, bool flushing) {
    for (size_t index = 2U; index < decoder->length; ++index) {
        if (input_control_final(decoder->pending[index])) {
            input_decoder_consume(decoder, index + 1U);
            return true;
        }
    }
    if (flushing) {
        decoder->length = 0U;
        return true;
    }
    if (decoder->length >= SGR_MOUSE_MAX_BYTES) {
        decoder->length = 0U;
        decoder->discarding_control_sequence = true;
        return true;
    }
    return false;
}

static InputMouseButton pointer_button(uint32_t encoded) {
    static const InputMouseButton buttons[] = {INPUT_MOUSE_BUTTON_LEFT, INPUT_MOUSE_BUTTON_MIDDLE,
                                               INPUT_MOUSE_BUTTON_RIGHT, INPUT_MOUSE_BUTTON_NONE};
    return buttons[encoded & 3U];
}

static InputMouseButton wheel_button(uint32_t encoded) {
    static const InputMouseButton buttons[] = {INPUT_MOUSE_BUTTON_WHEEL_UP,
                                               INPUT_MOUSE_BUTTON_WHEEL_DOWN,
                                               INPUT_MOUSE_BUTTON_WHEEL_LEFT,
                                               INPUT_MOUSE_BUTTON_WHEEL_RIGHT};
    return buttons[encoded & 3U];
}

static bool build_sgr_mouse_event(const uint32_t fields[3], unsigned char final, InputEvent *event) {
    const uint32_t encoded = fields[0];
    if (encoded > 127U || fields[1] == 0U || fields[1] > UINT16_MAX || fields[2] == 0U ||
        fields[2] > UINT16_MAX) {
        return false;
    }
    InputMouseAction action;
    InputMouseButton button;
    if ((encoded & 64U) != 0U) {
        if (final != 'M' || (encoded & 32U) != 0U) return false;
        action = INPUT_MOUSE_WHEEL;
        button = wheel_button(encoded);
    } else if ((encoded & 32U) != 0U) {
        if (final != 'M') return false;
        action = INPUT_MOUSE_MOTION;
        button = pointer_button(encoded);
    } else if (final == 'm') {
        if ((encoded & 3U) == 3U) return false;
        action = INPUT_MOUSE_RELEASE;
        button = pointer_button(encoded);
    } else {
        if ((encoded & 3U) == 3U) return false;
        action = INPUT_MOUSE_PRESS;
        button = pointer_button(encoded);
    }
    uint8_t modifiers = INPUT_MOUSE_MODIFIER_NONE;
    if ((encoded & 4U) != 0U) modifiers |= INPUT_MOUSE_MODIFIER_SHIFT;
    if ((encoded & 8U) != 0U) modifiers |= INPUT_MOUSE_MODIFIER_ALT;
    if ((encoded & 16U) != 0U) modifiers |= INPUT_MOUSE_MODIFIER_CONTROL;
    *event = (InputEvent){.type = INPUT_KEY_MOUSE,
                          .mouse_action = action,
                          .mouse_button = button,
                          .mouse_column = (uint16_t)(fields[1] - 1U),
                          .mouse_row = (uint16_t)(fields[2] - 1U),
                          .mouse_modifiers = modifiers};
    return true;
}

InputSgrMouseResult input_parse_sgr_mouse(InputDecoder *decoder, InputEvent *event, bool flushing) {
    if (decoder->length < 3U || decoder->pending[0] != 0x1bU || decoder->pending[1] != '[' ||
        decoder->pending[2] != '<') {
        return INPUT_SGR_MOUSE_NOT_MATCHED;
    }
    uint32_t fields[3] = {0U, 0U, 0U};
    size_t field = 0U;
    bool has_digit = false;
    for (size_t index = 3U; index < decoder->length; ++index) {
        if (index >= SGR_MOUSE_MAX_BYTES) return INPUT_SGR_MOUSE_INVALID;
        const unsigned char byte = decoder->pending[index];
        if (byte >= '0' && byte <= '9') {
            const uint32_t digit = (uint32_t)(byte - '0');
            const uint32_t limit = field == 0U ? 127U : UINT16_MAX;
            if (fields[field] > (limit - digit) / 10U) return INPUT_SGR_MOUSE_INVALID;
            fields[field] = fields[field] * 10U + digit;
            has_digit = true;
            continue;
        }
        if (byte == ';') {
            if (!has_digit || field >= 2U) return INPUT_SGR_MOUSE_INVALID;
            ++field;
            has_digit = false;
            continue;
        }
        if (byte == 'M' || byte == 'm') {
            if (!has_digit || field != 2U || !build_sgr_mouse_event(fields, byte, event)) {
                return INPUT_SGR_MOUSE_INVALID;
            }
            input_decoder_consume(decoder, index + 1U);
            return INPUT_SGR_MOUSE_EMITTED;
        }
        return INPUT_SGR_MOUSE_INVALID;
    }
    return flushing || decoder->length >= SGR_MOUSE_MAX_BYTES ? INPUT_SGR_MOUSE_INVALID :
                                                                  INPUT_SGR_MOUSE_INCOMPLETE;
}

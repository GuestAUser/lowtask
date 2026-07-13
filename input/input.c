#include "input/input.h"
#include "input/mouse_decoder.h"

#include <stdbool.h>

static bool emit(InputDecoder *decoder, InputEvent *event, bool flushing) {
restart:
    if (decoder->length == 0U) return false;
    const unsigned char first = decoder->pending[0];
    *event = (InputEvent){.type = INPUT_KEY_NONE};
    if (first == 0x1bU) {
        /* Hold ambiguous escape prefixes until completion or the runtime's bounded flush deadline. */
        if (decoder->length == 1U && !flushing) return false;
        if (decoder->length >= 2U && (decoder->pending[1] == '[' || decoder->pending[1] == 'O')) {
            if (decoder->length < 3U && !flushing) return false;
            if (decoder->length >= 3U) {
                const InputSgrMouseResult mouse = input_parse_sgr_mouse(decoder, event, flushing);
                if (mouse == INPUT_SGR_MOUSE_EMITTED) return true;
                if (mouse == INPUT_SGR_MOUSE_INCOMPLETE) return false;
                if (mouse == INPUT_SGR_MOUSE_INVALID) {
                    if (input_discard_control_candidate(decoder, flushing)) goto restart;
                    return false;
                }
                const unsigned char final = decoder->pending[2];
                InputKeyType type = INPUT_KEY_NONE;
                if (final == 'A') type = INPUT_KEY_UP;
                if (final == 'B') type = INPUT_KEY_DOWN;
                if (final == 'C') type = INPUT_KEY_RIGHT;
                if (final == 'D') type = INPUT_KEY_LEFT;
                if (final == 'H') type = INPUT_KEY_HOME;
                if (final == 'F') type = INPUT_KEY_END;
                if (final == 'Z') type = INPUT_KEY_BACKTAB;
                if (type != INPUT_KEY_NONE) {
                    event->type = type;
                    input_decoder_consume(decoder, 3U);
                    return true;
                }

                if (decoder->pending[1] == '[' &&
                    (final == '1' || final == '3' || final == '4' || final == '5' ||
                     final == '6' || final == '7' || final == '8')) {
                    if (decoder->length < 4U && !flushing) return false;
                    if (decoder->length >= 4U && decoder->pending[3] == '~') {
                        switch (final) {
                            case '1':
                            case '7':
                                event->type = INPUT_KEY_HOME;
                                break;
                            case '3':
                                event->type = INPUT_KEY_DELETE;
                                break;
                            case '4':
                            case '8':
                                event->type = INPUT_KEY_END;
                                break;
                            case '5':
                                event->type = INPUT_KEY_PAGE_UP;
                                break;
                            case '6':
                                event->type = INPUT_KEY_PAGE_DOWN;
                                break;
                        }
                        input_decoder_consume(decoder, 4U);
                        return true;
                    }
                }
                if (input_discard_control_candidate(decoder, flushing)) goto restart;
                return false;
            }
        }
        event->type = INPUT_KEY_ESCAPE;
        input_decoder_consume(decoder, 1U);
        return true;
    }
    if (first == '\r' || first == '\n') event->type = INPUT_KEY_ENTER;
    else if (first == '\t') event->type = INPUT_KEY_TAB;
    else if (first == 0x7fU || first == 0x08U) event->type = INPUT_KEY_BACKSPACE;
    else if (first == 0x03U) event->type = INPUT_KEY_INTERRUPT;
    if (event->type != INPUT_KEY_NONE) {
        input_decoder_consume(decoder, 1U);
        return true;
    }
    if (first < 0x80U) {
        event->type = INPUT_KEY_CHARACTER;
        event->codepoint = first;
        input_decoder_consume(decoder, 1U);
        return true;
    }
    size_t count = 0U;
    uint32_t codepoint = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
        count = 2U;
        codepoint = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
        count = 3U;
        codepoint = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        count = 4U;
        codepoint = first & 0x07U;
    } else {
        count = 1U;
    }
    if (decoder->length < count && !flushing) return false;
    bool valid = count > 1U && decoder->length >= count;
    for (size_t index = 1U; valid && index < count; ++index) {
        const unsigned char byte = decoder->pending[index];
        valid = (byte & 0xc0U) == 0x80U;
        codepoint = (codepoint << 6U) | (uint32_t)(byte & 0x3fU);
    }
    if (!valid || (count == 3U && codepoint < 0x800U) ||
        (count == 4U && codepoint < 0x10000U) ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
        /* One-byte recovery preserves the next byte as a possible valid sequence boundary. */
        event->type = INPUT_KEY_CHARACTER;
        event->codepoint = 0xfffdU;
        input_decoder_consume(decoder, 1U);
        return true;
    }
    event->type = INPUT_KEY_CHARACTER;
    event->codepoint = codepoint;
    input_decoder_consume(decoder, count);
    return true;
}

void input_decoder_init(InputDecoder *decoder) {
    if (decoder != NULL) *decoder = (InputDecoder){0};
}

static size_t drain(InputDecoder *decoder, InputEvent *events, size_t capacity, bool flushing) {
    size_t count = 0U;
    while (count < capacity && emit(decoder, &events[count], flushing)) ++count;
    return count;
}

size_t input_decoder_feed(InputDecoder *decoder, const unsigned char *bytes, size_t length,
                          InputEvent *events, size_t event_capacity) {
    if (decoder == NULL || (length > 0U && bytes == NULL) ||
        (event_capacity > 0U && events == NULL)) return 0U;
    size_t event_count = 0U;
    /* Invalid controls discard through their final byte so payload cannot reappear as text. */
    for (size_t index = 0U; index < length; ++index) {
        if (decoder->discarding_control_sequence) {
            if (input_control_final(bytes[index])) decoder->discarding_control_sequence = false;
            continue;
        }
        if (decoder->length == sizeof(decoder->pending)) {
            if (event_count == event_capacity || !emit(decoder, &events[event_count], true)) break;
            ++event_count;
        }
        decoder->pending[decoder->length++] = bytes[index];
        if (event_count < event_capacity) {
            event_count += drain(decoder, &events[event_count], event_capacity - event_count, false);
        }
    }
    return event_count;
}

size_t input_decoder_flush(InputDecoder *decoder, InputEvent *events, size_t event_capacity) {
    if (decoder == NULL || (event_capacity > 0U && events == NULL)) return 0U;
    decoder->discarding_control_sequence = false;
    return drain(decoder, events, event_capacity, true);
}

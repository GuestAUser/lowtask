#ifndef LOWTASK_INPUT_INPUT_H
#define LOWTASK_INPUT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    INPUT_KEY_NONE = 0,
    INPUT_KEY_CHARACTER,
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_HOME,
    INPUT_KEY_END,
    INPUT_KEY_PAGE_UP,
    INPUT_KEY_PAGE_DOWN,
    INPUT_KEY_DELETE,
    INPUT_KEY_BACKSPACE,
    INPUT_KEY_ENTER,
    INPUT_KEY_ESCAPE,
    INPUT_KEY_TAB,
    INPUT_KEY_BACKTAB,
    INPUT_KEY_INTERRUPT,
    INPUT_KEY_MOUSE
} InputKeyType;

typedef enum {
    INPUT_MOUSE_PRESS = 0,
    INPUT_MOUSE_RELEASE,
    INPUT_MOUSE_MOTION,
    INPUT_MOUSE_WHEEL
} InputMouseAction;

typedef enum {
    INPUT_MOUSE_BUTTON_NONE = 0,
    INPUT_MOUSE_BUTTON_LEFT,
    INPUT_MOUSE_BUTTON_MIDDLE,
    INPUT_MOUSE_BUTTON_RIGHT,
    INPUT_MOUSE_BUTTON_WHEEL_UP,
    INPUT_MOUSE_BUTTON_WHEEL_DOWN,
    INPUT_MOUSE_BUTTON_WHEEL_LEFT,
    INPUT_MOUSE_BUTTON_WHEEL_RIGHT
} InputMouseButton;

typedef enum {
    INPUT_MOUSE_MODIFIER_NONE = 0,
    INPUT_MOUSE_MODIFIER_SHIFT = 1U << 0U,
    INPUT_MOUSE_MODIFIER_ALT = 1U << 1U,
    INPUT_MOUSE_MODIFIER_CONTROL = 1U << 2U
} InputMouseModifier;

typedef struct {
    InputKeyType type;
    uint32_t codepoint;
    InputMouseAction mouse_action;
    InputMouseButton mouse_button;
    uint16_t mouse_column;
    uint16_t mouse_row;
    uint8_t mouse_modifiers;
} InputEvent;

typedef struct {
    unsigned char pending[128];
    size_t length;
    bool discarding_control_sequence;
} InputDecoder;

void input_decoder_init(InputDecoder *decoder);
size_t input_decoder_feed(InputDecoder *decoder, const unsigned char *bytes, size_t length,
                          InputEvent *events, size_t event_capacity);
size_t input_decoder_flush(InputDecoder *decoder, InputEvent *events, size_t event_capacity);

#endif

#define _XOPEN_SOURCE 600

#include "input/input.h"
#include "platform/terminal.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static InputEvent decode_one(InputDecoder *decoder, const char *sequence) {
    InputEvent event = {0};
    const size_t count = input_decoder_feed(decoder, (const unsigned char *)sequence,
                                            strlen(sequence), &event, 1U);
    assert(count == 1U);
    return event;
}

static void assert_mouse(InputEvent event, InputMouseAction action, InputMouseButton button,
                         uint16_t column, uint16_t row, unsigned int modifiers) {
    assert(event.type == INPUT_KEY_MOUSE);
    assert(event.mouse_action == action);
    assert(event.mouse_button == button);
    assert(event.mouse_column == column);
    assert(event.mouse_row == row);
    assert(event.mouse_modifiers == modifiers);
}

static void test_sgr_mouse_events(void) {
    InputDecoder decoder;
    input_decoder_init(&decoder);

    assert(decode_one(&decoder, "\x1b[Z").type == INPUT_KEY_BACKTAB);

    assert_mouse(decode_one(&decoder, "\x1b[<0;12;7M"), INPUT_MOUSE_PRESS,
                 INPUT_MOUSE_BUTTON_LEFT, 11U, 6U, INPUT_MOUSE_MODIFIER_NONE);
    assert_mouse(decode_one(&decoder, "\x1b[<0;12;7m"), INPUT_MOUSE_RELEASE,
                 INPUT_MOUSE_BUTTON_LEFT, 11U, 6U, INPUT_MOUSE_MODIFIER_NONE);
    assert_mouse(decode_one(&decoder, "\x1b[<32;4;9M"), INPUT_MOUSE_MOTION,
                 INPUT_MOUSE_BUTTON_LEFT, 3U, 8U, INPUT_MOUSE_MODIFIER_NONE);
    assert_mouse(decode_one(&decoder, "\x1b[<35;4;9M"), INPUT_MOUSE_MOTION,
                 INPUT_MOUSE_BUTTON_NONE, 3U, 8U, INPUT_MOUSE_MODIFIER_NONE);
    assert_mouse(decode_one(&decoder, "\x1b[<64;2;3M"), INPUT_MOUSE_WHEEL,
                 INPUT_MOUSE_BUTTON_WHEEL_UP, 1U, 2U, INPUT_MOUSE_MODIFIER_NONE);
    assert_mouse(decode_one(&decoder, "\x1b[<65;2;3M"), INPUT_MOUSE_WHEEL,
                 INPUT_MOUSE_BUTTON_WHEEL_DOWN, 1U, 2U, INPUT_MOUSE_MODIFIER_NONE);
    assert_mouse(decode_one(&decoder, "\x1b[<28;5;6M"), INPUT_MOUSE_PRESS,
                 INPUT_MOUSE_BUTTON_LEFT, 4U, 5U,
                 INPUT_MOUSE_MODIFIER_SHIFT | INPUT_MOUSE_MODIFIER_ALT |
                     INPUT_MOUSE_MODIFIER_CONTROL);
}

static void test_split_and_bounded_sgr_mouse(void) {
    static const char sequence[] = "\x1b[<2;123;456M";
    InputDecoder decoder;
    InputEvent event = {0};
    input_decoder_init(&decoder);

    for (size_t index = 0U; index + 1U < sizeof(sequence) - 1U; ++index) {
        assert(input_decoder_feed(&decoder, (const unsigned char *)&sequence[index], 1U,
                                  &event, 1U) == 0U);
    }
    assert(input_decoder_feed(&decoder,
                              (const unsigned char *)&sequence[sizeof(sequence) - 2U], 1U,
                              &event, 1U) == 1U);
    assert_mouse(event, INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_RIGHT, 122U, 455U,
                 INPUT_MOUSE_MODIFIER_NONE);

    unsigned char oversized[96];
    oversized[0] = 0x1bU;
    oversized[1] = '[';
    oversized[2] = '<';
    memset(oversized + 3U, '9', sizeof(oversized) - 4U);
    oversized[sizeof(oversized) - 1U] = 'M';
    InputEvent discarded[sizeof(oversized)];
    const size_t count = input_decoder_feed(&decoder, oversized, sizeof(oversized), discarded,
                                            sizeof(discarded) / sizeof(discarded[0]));
    assert(count == 0U);
    assert(decoder.length == 0U);

    event = decode_one(&decoder, "\x1b[<0;1;1M");
    assert_mouse(event, INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 0U, 0U,
                 INPUT_MOUSE_MODIFIER_NONE);

    static const unsigned char malformed[] = "\x1b[<0;0;1M";
    const size_t malformed_count = input_decoder_feed(
        &decoder, malformed, sizeof(malformed) - 1U, discarded,
        sizeof(discarded) / sizeof(discarded[0]));
    assert(malformed_count == 0U);
    assert(decoder.length == 0U);
}

static void test_mouse_before_buffered_keys(void) {
    unsigned char buffered[64];
    static const char sequence[] = "\x1b[<0;2;3M";
    memcpy(buffered, sequence, sizeof(sequence) - 1U);
    memset(buffered + sizeof(sequence) - 1U, 'a', sizeof(buffered) - sizeof(sequence) + 1U);

    InputDecoder decoder;
    InputEvent events[64];
    input_decoder_init(&decoder);
    assert(input_decoder_feed(&decoder, buffered, sizeof(buffered), NULL, 0U) == 0U);
    const size_t count = input_decoder_flush(&decoder, events, 64U);
    assert(count == 1U + sizeof(buffered) - (sizeof(sequence) - 1U));
    assert_mouse(events[0], INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 2U,
                 INPUT_MOUSE_MODIFIER_NONE);
    for (size_t index = 1U; index < count; ++index) {
        assert(events[index].type == INPUT_KEY_CHARACTER);
        assert(events[index].codepoint == 'a');
    }
}

static size_t read_available(int descriptor, char *buffer, size_t capacity) {
    size_t length = 0U;
    while (length < capacity) {
        const ssize_t count = read(descriptor, buffer + length, capacity - length);
        if (count > 0) {
            length += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        assert(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        break;
    }
    return length;
}

static void test_terminal_mouse_lifecycle(void) {
    const int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(master >= 0);
    assert(grantpt(master) == 0);
    assert(unlockpt(master) == 0);
    const char *const slave_name = ptsname(master);
    assert(slave_name != NULL);
    const int slave = open(slave_name, O_RDWR | O_NOCTTY);
    assert(slave >= 0);

    struct termios original;
    assert(tcgetattr(slave, &original) == 0);
    const int original_flags = fcntl(slave, F_GETFL);
    assert(original_flags >= 0);
    Terminal terminal;
    assert(terminal_open(&terminal, slave, slave));
    assert((fcntl(slave, F_GETFL) & O_NONBLOCK) != 0);

    char output[1024] = {0};
    size_t length = read_available(master, output, sizeof(output) - 1U);
    output[length] = '\0';
    assert(strstr(output, "\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h") != NULL);

    terminal_close(&terminal);
    const size_t close_length = read_available(master, output + length, sizeof(output) - 1U - length);
    length += close_length;
    output[length] = '\0';
    assert(strstr(output, "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l") != NULL);

    struct termios restored;
    assert(tcgetattr(slave, &restored) == 0);
    assert(restored.c_iflag == original.c_iflag);
    assert(restored.c_oflag == original.c_oflag);
    assert(restored.c_cflag == original.c_cflag);
    assert(restored.c_lflag == original.c_lflag);
    assert(memcmp(restored.c_cc, original.c_cc, sizeof(original.c_cc)) == 0);
    assert(fcntl(slave, F_GETFL) == original_flags);

    const size_t before_second_close = length;
    terminal_close(&terminal);
    length += read_available(master, output + length, sizeof(output) - 1U - length);
    assert(length == before_second_close);

    const int read_only_output = open(slave_name, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    assert(read_only_output >= 0);
    Terminal failed_terminal;
    assert(!terminal_open(&failed_terminal, slave, read_only_output));
    assert(!failed_terminal.raw_enabled);
    assert(tcgetattr(slave, &restored) == 0);
    assert(restored.c_iflag == original.c_iflag);
    assert(restored.c_oflag == original.c_oflag);
    assert(restored.c_cflag == original.c_cflag);
    assert(restored.c_lflag == original.c_lflag);
    assert(memcmp(restored.c_cc, original.c_cc, sizeof(original.c_cc)) == 0);
    assert(close(read_only_output) == 0);

    assert(close(slave) == 0);
    assert(close(master) == 0);
}

int main(void) {
    test_sgr_mouse_events();
    test_split_and_bounded_sgr_mouse();
    test_mouse_before_buffered_keys();
    test_terminal_mouse_lifecycle();
    puts("test_mouse: PASS");
    return 0;
}

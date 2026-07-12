#include "input/input.h"
#include "tui/animation.h"
#include "tui/color.h"
#include "tui/render.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_input_decoder(void) {
    InputDecoder decoder;
    InputEvent events[8];
    input_decoder_init(&decoder);

    const unsigned char first[] = {'j', ' ', 0x1bU, '['};
    size_t count = input_decoder_feed(&decoder, first, sizeof(first), events, 8U);
    assert(count == 2U);
    assert(events[0].type == INPUT_KEY_CHARACTER && events[0].codepoint == 'j');
    assert(events[1].type == INPUT_KEY_CHARACTER && events[1].codepoint == ' ');

    const unsigned char second[] = {'A', 0xc3U, 0xbcU, 0x7fU};
    count = input_decoder_feed(&decoder, second, sizeof(second), events, 8U);
    assert(count == 3U);
    assert(events[0].type == INPUT_KEY_UP);
    assert(events[1].type == INPUT_KEY_CHARACTER && events[1].codepoint == 0xfcU);
    assert(events[2].type == INPUT_KEY_BACKSPACE);

    const unsigned char escape[] = {0x1bU};
    count = input_decoder_feed(&decoder, escape, sizeof(escape), events, 8U);
    assert(count == 0U);
    count = input_decoder_flush(&decoder, events, 8U);
    assert(count == 1U && events[0].type == INPUT_KEY_ESCAPE);

    static const unsigned char malformed_mouse[] = "\x1b[<0;0;1Md";
    count = input_decoder_feed(&decoder, malformed_mouse, sizeof(malformed_mouse) - 1U,
                               events, 8U);
    assert(count == 1U);
    assert(events[0].type == INPUT_KEY_CHARACTER && events[0].codepoint == 'd');

    unsigned char unterminated[64] = {0x1bU, '[', '<'};
    memset(unterminated + 3U, '9', sizeof(unterminated) - 3U);
    count = input_decoder_feed(&decoder, unterminated, sizeof(unterminated), events, 8U);
    assert(count == 0U && decoder.discarding_control_sequence);
    assert(input_decoder_flush(&decoder, events, 8U) == 0U);
    count = input_decoder_feed(&decoder, (const unsigned char *)"q", 1U, events, 8U);
    assert(count == 1U && events[0].type == INPUT_KEY_CHARACTER && events[0].codepoint == 'q');

    const unsigned char page_prefix[] = {0x1bU, '[', '5'};
    count = input_decoder_feed(&decoder, page_prefix, sizeof(page_prefix), events, 8U);
    assert(count == 0U);
    const unsigned char page_suffix[] = {'~', 0x1bU, '[', '6', '~'};
    count = input_decoder_feed(&decoder, page_suffix, sizeof(page_suffix), events, 8U);
    assert(count == 2U);
    assert(events[0].type == INPUT_KEY_PAGE_UP);
    assert(events[1].type == INPUT_KEY_PAGE_DOWN);

    static const unsigned char malformed_page[] = "\x1b[5xj";
    count = input_decoder_feed(&decoder, malformed_page, sizeof(malformed_page) - 1U,
                               events, 8U);
    assert(count == 1U);
    assert(events[0].type == INPUT_KEY_CHARACTER && events[0].codepoint == 'j');
}

static void test_animation(void) {
    assert(animation_clamp_progress(NAN) == 0.0F);
    assert(animation_clamp_progress(-1.0F) == 0.0F);
    assert(animation_clamp_progress(2.0F) == 1.0F);
    assert(animation_clamp_progress(0.25F) == 0.25F);
    assert(fabsf(ease_out_cubic(0.0F) - 0.0F) < 0.0001F);
    assert(fabsf(ease_out_cubic(0.5F) - 0.875F) < 0.0001F);
    assert(fabsf(ease_out_cubic(1.0F) - 1.0F) < 0.0001F);
    assert(unsetenv("LOWTASK_REDUCE_MOTION") == 0);
    assert(!animation_reduced_motion_enabled());
    assert(fabsf(animation_motion_progress(0.5F) - 0.875F) < 0.0001F);
    assert(setenv("LOWTASK_REDUCE_MOTION", "0", 1) == 0);
    assert(!animation_reduced_motion_enabled());
    assert(setenv("LOWTASK_REDUCE_MOTION", "1", 1) == 0);
    assert(animation_reduced_motion_enabled());
    assert(animation_motion_progress(0.5F) == 1.0F);
    assert(unsetenv("LOWTASK_REDUCE_MOTION") == 0);

    Tween tween;
    tween_init(&tween, 0.0F);
    tween_to(&tween, 10.0F, 1.0F);
    assert(tween_update(&tween, 0.5F));
    assert(fabsf(tween.value - 8.75F) < 0.001F);
    assert(tween_update(&tween, 0.5F));
    assert(!tween.active && fabsf(tween.value - 10.0F) < 0.001F);
    assert(!tween_update(&tween, 10.0F));
    tween_init(&tween, 0.0F);
    tween_to(&tween, 10.0F, 0.18F);
    assert(tween_update(&tween, 1.0F));
    assert(!tween.active && fabsf(tween.value - 10.0F) < 0.001F);
}

static void test_color_modes(void) {
    char output[64];
    assert(color_ansi(output, sizeof(output), 0x336699U, true, true) > 0);
    assert(strstr(output, "38;2;51;102;153") != NULL);
    assert(color_ansi(output, sizeof(output), 0x336699U, false, false) > 0);
    assert(strstr(output, "48;5;") != NULL);
    assert(color_token_rgb(TUI_COLOR_URGENT) == 0xf15d9eU);
    assert(color_token_xterm(TUI_COLOR_URGENT) == 205U);
    assert(color_ansi(output, sizeof(output), color_token_rgb(TUI_COLOR_URGENT), false, true) > 0);
    assert(strstr(output, "38;5;205") != NULL);
}

static void test_diff_renderer(void) {
    Renderer renderer;
    const RendererStyle style = {
        .foreground = 0xf0f0f0U,
        .background = 0x101010U,
        .attributes = RENDER_ATTR_BOLD,
    };
    int descriptors[2];
    assert(pipe(descriptors) == 0);
    assert(renderer_init(&renderer, 8U, 2U, true));

    renderer_begin(&renderer, 0x101010U);
    renderer_put_utf8(&renderer, 0U, 0U, "hello", 5U, style);
    const ssize_t first = renderer_present(&renderer, descriptors[1]);
    assert(first > 0);
    char output[2048];
    const ssize_t read_count = read(descriptors[0], output, sizeof(output) - 1U);
    assert(read_count > 0);
    output[read_count] = '\0';
    assert(strstr(output, "\x1b[1;1H") != NULL);
    assert(strstr(output, "hello") != NULL);

    renderer_begin(&renderer, 0x101010U);
    renderer_put_utf8(&renderer, 0U, 0U, "hello", 5U, style);
    assert(renderer_present(&renderer, descriptors[1]) == 0);

    renderer_put_utf8(&renderer, 4U, 0U, "!", 1U, style);
    const ssize_t changed = renderer_present(&renderer, descriptors[1]);
    assert(changed > 0 && changed < first);

    renderer_begin(&renderer, 0x101010U);
    renderer_put_utf8(&renderer, 0U, 0U, "Ａ", 2U, style);
    assert(renderer_codepoint_width(0xff21U) == 2U);
    assert(renderer.back[0].width == 2U && renderer.back[1].width == 0U);
    assert(renderer_codepoint_width(0x302aU) == 0U);
    assert(renderer_codepoint_width(0x05b8U) == 0U);
    assert(renderer_codepoint_width(0x0651U) == 0U);
    assert(renderer_codepoint_width(0x093cU) == 0U);
    assert(renderer_codepoint_width(0x0e31U) == 0U);
    assert(renderer_codepoint_width(0x061cU) == 0U);
    assert(renderer_codepoint_width(0x200eU) == 0U);
    assert(renderer_codepoint_width(0x202aU) == 0U);
    assert(renderer_codepoint_width(0x2066U) == 0U);
    /* Soft hyphen occupies one cell in the target glibc/xterm width policy. */
    assert(renderer_codepoint_width(0x00adU) == 1U);
    renderer_begin(&renderer, 0x101010U);
    renderer_put_utf8(&renderer, 0U, 0U, "一〪a", 3U, style);
    assert(strcmp(renderer.back[0].glyph, "一〪") == 0);
    assert(renderer.back[0].width == 2U && renderer.back[1].width == 0U);
    assert(strcmp(renderer.back[2].glyph, "a") == 0);

    renderer_free(&renderer);
    assert(close(descriptors[0]) == 0);
    assert(close(descriptors[1]) == 0);
}

static void test_nonblocking_renderer_backpressure(void) {
    int descriptors[2];
    assert(pipe(descriptors) == 0);
    const int write_flags = fcntl(descriptors[1], F_GETFL);
    const int read_flags = fcntl(descriptors[0], F_GETFL);
    assert(write_flags >= 0 && read_flags >= 0);
    assert(fcntl(descriptors[1], F_SETFL, write_flags | O_NONBLOCK) == 0);
    assert(fcntl(descriptors[0], F_SETFL, read_flags | O_NONBLOCK) == 0);
    char fill[4096];
    memset(fill, 'x', sizeof(fill));
    while (write(descriptors[1], fill, sizeof(fill)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);

    Renderer renderer;
    assert(renderer_init(&renderer, 80U, 24U, true));
    renderer_begin(&renderer, 0x101010U);
    renderer_put_utf8(&renderer, 0U, 0U, "backpressure", 12U,
                      (RendererStyle){.foreground = 0xffffffU, .background = 0x101010U});
    assert(renderer_present(&renderer, descriptors[1]) == 0);
    assert(renderer_has_pending_output(&renderer));
    while (read(descriptors[0], fill, sizeof(fill)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    assert(renderer_present(&renderer, descriptors[1]) > 0);
    assert(!renderer_has_pending_output(&renderer));
    renderer_free(&renderer);
    assert(close(descriptors[0]) == 0);
    assert(close(descriptors[1]) == 0);
}

int main(void) {
    test_input_decoder();
    test_animation();
    test_color_modes();
    test_diff_renderer();
    test_nonblocking_renderer_backpressure();
    puts("test_ui: PASS");
    return 0;
}

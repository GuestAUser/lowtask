#include "core/text_input.h"

#include "core/text.h"

#include <string.h>

static size_t codepoint_at(const AppTextInput *input, size_t offset, uint32_t *codepoint) {
    size_t count = 1U;
    if (offset >= input->length ||
        !text_decode_utf8((const unsigned char *)input->value + offset, codepoint, &count) ||
        count > input->length - offset) {
        *codepoint = 0xfffdU;
        return offset < input->length ? 1U : 0U;
    }
    return count;
}

static size_t next_cluster(const AppTextInput *input, size_t offset) {
    if (offset >= input->length) return input->length;
    uint32_t ignored = 0U;
    const size_t first_count = codepoint_at(input, offset, &ignored);
    size_t next = offset + first_count;
    while (next < input->length) {
        uint32_t codepoint = 0U;
        const size_t count = codepoint_at(input, next, &codepoint);
        if (count == 0U || text_codepoint_width(codepoint) != 0U) break;
        next += count;
    }
    return next;
}

static size_t previous_cluster(const AppTextInput *input, size_t offset) {
    size_t current = 0U;
    size_t previous = 0U;
    while (current < offset) {
        previous = current;
        const size_t next = next_cluster(input, current);
        if (next <= current || next >= offset) return previous;
        current = next;
    }
    return previous;
}

static size_t cells_between(const AppTextInput *input, size_t start, size_t end) {
    size_t cells = 0U;
    size_t offset = start;
    while (offset < end) {
        uint32_t codepoint = 0U;
        const size_t count = codepoint_at(input, offset, &codepoint);
        if (count == 0U) break;
        cells += text_codepoint_width(codepoint);
        offset += count;
    }
    return cells;
}

void app_text_input_clear(AppTextInput *input) {
    if (input != NULL) *input = (AppTextInput){0};
}

bool app_text_input_set(AppTextInput *input, const char *value) {
    if (input == NULL || !text_utf8_is_valid(value == NULL ? "" : value,
                                              LOWTASK_TEXT_MAX, true)) return false;
    const char *source = value == NULL ? "" : value;
    const size_t length = strlen(source);
    memcpy(input->value, source, length + 1U);
    input->length = length;
    input->cursor = length;
    return true;
}

bool app_text_input_insert(AppTextInput *input, uint32_t codepoint) {
    if (input == NULL || input->cursor > input->length) return false;
    char encoded[4];
    const size_t count = text_encode_utf8(codepoint, encoded);
    if (count == 0U || count > LOWTASK_TEXT_MAX - input->length) return false;
    memmove(input->value + input->cursor + count, input->value + input->cursor,
            input->length - input->cursor + 1U);
    memcpy(input->value + input->cursor, encoded, count);
    input->length += count;
    input->cursor += count;
    while (input->cursor < input->length) {
        uint32_t following = 0U;
        const size_t following_count = codepoint_at(input, input->cursor, &following);
        if (following_count == 0U || text_codepoint_width(following) != 0U) break;
        input->cursor += following_count;
    }
    return true;
}

void app_text_input_left(AppTextInput *input) {
    if (input != NULL && input->cursor > 0U) input->cursor = previous_cluster(input, input->cursor);
}

void app_text_input_right(AppTextInput *input) {
    if (input != NULL && input->cursor < input->length) {
        input->cursor = next_cluster(input, input->cursor);
    }
}

void app_text_input_home(AppTextInput *input) {
    if (input != NULL) input->cursor = 0U;
}

void app_text_input_end(AppTextInput *input) {
    if (input != NULL) input->cursor = input->length;
}

static void erase(AppTextInput *input, size_t start, size_t end) {
    if (input == NULL || start > end || end > input->length) return;
    memmove(input->value + start, input->value + end, input->length - end + 1U);
    input->length -= end - start;
    input->cursor = start;
}

void app_text_input_backspace(AppTextInput *input) {
    if (input != NULL && input->cursor > 0U) {
        erase(input, previous_cluster(input, input->cursor), input->cursor);
    }
}

void app_text_input_delete(AppTextInput *input) {
    if (input != NULL && input->cursor < input->length) {
        erase(input, input->cursor, next_cluster(input, input->cursor));
    }
}

AppTextViewport app_text_input_viewport(const AppTextInput *input, size_t cells) {
    AppTextViewport viewport = {0};
    if (input == NULL) return viewport;
    viewport.start = input->cursor <= input->length ? input->cursor : input->length;
    viewport.end = viewport.start;
    viewport.draw_caret = cells > 0U;
    size_t remaining = cells > 0U ? cells - 1U : 0U;
    while (viewport.start > 0U) {
        const size_t candidate = previous_cluster(input, viewport.start);
        const size_t width = cells_between(input, candidate, viewport.start);
        if (width > remaining) break;
        viewport.start = candidate;
        remaining -= width;
    }
    viewport.caret_cell = cells_between(input, viewport.start, input->cursor);
    while (viewport.end < input->length) {
        const size_t candidate = next_cluster(input, viewport.end);
        const size_t width = cells_between(input, viewport.end, candidate);
        if (width > remaining) break;
        viewport.end = candidate;
        remaining -= width;
    }
    return viewport;
}

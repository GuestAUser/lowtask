#ifndef LOWTASK_CORE_TEXT_H
#define LOWTASK_CORE_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t text_bounded_length(const char *text, size_t limit);
bool text_decode_utf8(const unsigned char *text, uint32_t *codepoint, size_t *byte_count);
size_t text_encode_utf8(uint32_t codepoint, char output[4]);
bool text_utf8_is_valid(const char *text, size_t maximum_bytes, bool allow_empty);
unsigned text_codepoint_width(uint32_t codepoint);

#endif

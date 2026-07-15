#include "core/text.h"

typedef struct {
    uint32_t first;
    uint32_t last;
} UnicodeInterval;

size_t text_bounded_length(const char *text, size_t limit) {
    size_t length = 0U;
    if (text == NULL) return 0U;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

bool text_decode_utf8(const unsigned char *text, uint32_t *codepoint, size_t *byte_count) {
    if (text == NULL || codepoint == NULL || byte_count == NULL || text[0] == '\0') return false;
    const unsigned char first = text[0];
    size_t count = 1U;
    uint32_t value = first;
    if (first < 0x80U) {
        *codepoint = value;
        *byte_count = count;
        return true;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
        count = 2U;
        value = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
        count = 3U;
        value = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        count = 4U;
        value = first & 0x07U;
    } else {
        *codepoint = 0xfffdU;
        *byte_count = 1U;
        return false;
    }
    for (size_t index = 1U; index < count; ++index) {
        const unsigned char continuation = text[index];
        if (continuation == '\0' || (continuation & 0xc0U) != 0x80U) {
            *codepoint = 0xfffdU;
            *byte_count = 1U;
            return false;
        }
        value = (value << 6U) | (uint32_t)(continuation & 0x3fU);
    }
    const bool overlong = (count == 2U && value < 0x80U) ||
                          (count == 3U && value < 0x800U) ||
                          (count == 4U && value < 0x10000U);
    if (overlong || (value >= 0xd800U && value <= 0xdfffU) || value > 0x10ffffU) {
        *codepoint = 0xfffdU;
        *byte_count = count;
        return false;
    }
    *codepoint = value;
    *byte_count = count;
    return true;
}

size_t text_encode_utf8(uint32_t codepoint, char output[4]) {
    if (output == NULL || codepoint == 0U || codepoint < 0x20U || codepoint == 0x7fU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) return 0U;
    if (codepoint < 0x80U) {
        output[0] = (char)codepoint;
        return 1U;
    }
    if (codepoint <= 0x7ffU) {
        output[0] = (char)(0xc0U | (codepoint >> 6U));
        output[1] = (char)(0x80U | (codepoint & 0x3fU));
        return 2U;
    }
    if (codepoint <= 0xffffU) {
        output[0] = (char)(0xe0U | (codepoint >> 12U));
        output[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[2] = (char)(0x80U | (codepoint & 0x3fU));
        return 3U;
    }
    output[0] = (char)(0xf0U | (codepoint >> 18U));
    output[1] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
    output[2] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
    output[3] = (char)(0x80U | (codepoint & 0x3fU));
    return 4U;
}

bool text_utf8_is_valid(const char *text, size_t maximum_bytes, bool allow_empty) {
    if (text == NULL) return allow_empty;
    const size_t length = text_bounded_length(text, maximum_bytes + 1U);
    if (length > maximum_bytes || (!allow_empty && length == 0U)) return false;
    size_t offset = 0U;
    while (offset < length) {
        uint32_t codepoint = 0U;
        size_t count = 0U;
        if (!text_decode_utf8((const unsigned char *)text + offset, &codepoint, &count) ||
            count > length - offset || codepoint == 0U || codepoint < 0x20U ||
            codepoint == 0x7fU) return false;
        offset += count;
    }
    return true;
}

static bool interval_contains(const UnicodeInterval *intervals, size_t count,
                              uint32_t codepoint) {
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if (codepoint < intervals[middle].first) high = middle;
        else if (codepoint > intervals[middle].last) low = middle + 1U;
        else return true;
    }
    return false;
}

static bool zero_width(uint32_t codepoint) {
    static const UnicodeInterval combining[] = {
#include "core/zero_width.inc"
    };
    static const UnicodeInterval format[] = {
        {0x0600U, 0x0605U}, {0x061cU, 0x061cU}, {0x06ddU, 0x06ddU},
        {0x070fU, 0x070fU}, {0x0890U, 0x0891U}, {0x08e2U, 0x08e2U},
        {0x180eU, 0x180eU}, {0x200bU, 0x200fU}, {0x202aU, 0x202eU},
        {0x2060U, 0x2064U}, {0x2066U, 0x206fU}, {0xfeffU, 0xfeffU},
        {0xfff9U, 0xfffbU}, {0x110bdU, 0x110bdU}, {0x110cdU, 0x110cdU},
        {0x13430U, 0x1343fU}, {0x1bca0U, 0x1bca3U}, {0x1d173U, 0x1d17aU},
        {0xe0001U, 0xe0001U}, {0xe0020U, 0xe007fU},
    };
    return interval_contains(combining, sizeof(combining) / sizeof(combining[0]), codepoint) ||
           interval_contains(format, sizeof(format) / sizeof(format[0]), codepoint) ||
           (codepoint >= 0x1160U && codepoint <= 0x11ffU);
}

unsigned text_codepoint_width(uint32_t codepoint) {
    if (zero_width(codepoint)) return 0U;
    if ((codepoint >= 0x1100U && codepoint <= 0x115fU) ||
        (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) ||
        (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
        (codepoint >= 0xff01U && codepoint <= 0xff60U) ||
        (codepoint >= 0xffe0U && codepoint <= 0xffe6U) ||
        (codepoint >= 0x1f300U && codepoint <= 0x1faffU) ||
        (codepoint >= 0x20000U && codepoint <= 0x3fffdU)) return 2U;
    return 1U;
}

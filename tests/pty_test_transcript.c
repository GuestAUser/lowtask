#include "tests/pty_test_api.h"

#include <stdlib.h>
#include <string.h>

bool transcript_append(Transcript *transcript, const char *bytes, size_t length) {
    if (length > TRANSCRIPT_LIMIT - transcript->length) return false;
    const size_t needed = transcript->length + length + 1U;
    if (needed > transcript->capacity) {
        size_t capacity = transcript->capacity == 0U ? 16384U : transcript->capacity;
        while (capacity < needed) capacity *= 2U;
        char *grown = realloc(transcript->bytes, capacity);
        if (grown == NULL) return false;
        transcript->bytes = grown;
        transcript->capacity = capacity;
    }
    memcpy(transcript->bytes + transcript->length, bytes, length);
    transcript->length += length;
    transcript->bytes[transcript->length] = '\0';
    return true;
}

uint32_t transcript_hash(const Transcript *transcript) {
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < transcript->length; ++index) {
        hash ^= (unsigned char)transcript->bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

size_t transcript_csi_count(const Transcript *transcript) {
    size_t count = 0U;
    for (size_t index = 0U; index + 1U < transcript->length; ++index) {
        if ((unsigned char)transcript->bytes[index] == 0x1bU && transcript->bytes[index + 1U] == '[') ++count;
    }
    return count;
}

bool transcript_contains_since(const Session *session, size_t offset, const char *needle) {
    return session->transcript.bytes != NULL && offset <= session->transcript.length &&
           strstr(session->transcript.bytes + offset, needle) != NULL;
}

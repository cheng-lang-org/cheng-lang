#ifndef CHENG_TWOWAY_SEARCH_H
#define CHENG_TWOWAY_SEARCH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline size_t cheng_v3_twoway_max_size(size_t left, size_t right) {
    return left > right ? left : right;
}

static inline int cheng_v3_twoway_suffix_greater(size_t left, size_t right) {
    if (left == (size_t)-1) {
        return 0;
    }
    if (right == (size_t)-1) {
        return 1;
    }
    return left > right ? 1 : 0;
}

static inline size_t cheng_v3_twoway_maximal_suffix(const unsigned char* needle,
                                                    size_t needle_len,
                                                    size_t* period_out,
                                                    int reversed) {
    size_t max_suffix = (size_t)-1;
    size_t cursor = 0;
    size_t offset = 1;
    size_t period = 1;
    while (cursor + offset < needle_len) {
        unsigned char left = needle[cursor + offset];
        unsigned char right = needle[max_suffix + offset];
        if ((!reversed && left < right) || (reversed && left > right)) {
            cursor += offset;
            offset = 1;
            period = cursor - max_suffix;
            continue;
        }
        if (left == right) {
            if (offset != period) {
                offset += 1;
            } else {
                cursor += period;
                offset = 1;
            }
            continue;
        }
        max_suffix = cursor;
        cursor = max_suffix + 1;
        offset = 1;
        period = 1;
    }
    *period_out = period;
    return max_suffix;
}

static inline const unsigned char* cheng_v3_twoway_find_bytes(const unsigned char* haystack,
                                                              size_t haystack_len,
                                                              const unsigned char* needle,
                                                              size_t needle_len) {
    size_t forward_period = 0;
    size_t reverse_period = 0;
    size_t period = 0;
    size_t forward_suffix = 0;
    size_t reverse_suffix = 0;
    size_t split = 0;
    if (needle_len == 0) {
        return haystack;
    }
    if (haystack == NULL || needle == NULL || needle_len > haystack_len) {
        return NULL;
    }
    if (needle_len == 1) {
        return (const unsigned char*)memchr(haystack, needle[0], haystack_len);
    }

    forward_suffix = cheng_v3_twoway_maximal_suffix(needle, needle_len, &forward_period, 0);
    reverse_suffix = cheng_v3_twoway_maximal_suffix(needle, needle_len, &reverse_period, 1);
    if (cheng_v3_twoway_suffix_greater(forward_suffix, reverse_suffix)) {
        split = forward_suffix;
        period = forward_period;
    } else {
        split = reverse_suffix;
        period = reverse_period;
    }

    if (memcmp(needle, needle + period, split + 1) == 0) {
        size_t memory = 0;
        size_t pos = 0;
        while (pos + needle_len <= haystack_len) {
            size_t index = split + 1;
            if (index < memory) {
                index = memory;
            }
            while (index < needle_len && needle[index] == haystack[pos + index]) {
                index += 1;
            }
            if (index >= needle_len) {
                index = split + 1;
                while (index > memory && needle[index - 1] == haystack[pos + index - 1]) {
                    index -= 1;
                }
                if (index <= memory) {
                    return haystack + pos;
                }
                pos += period;
                memory = needle_len - period;
            } else {
                pos += index - split;
                memory = 0;
            }
        }
        return NULL;
    }

    period = cheng_v3_twoway_max_size(split + 1, needle_len - split - 1) + 1;
    {
        size_t pos = 0;
        while (pos + needle_len <= haystack_len) {
            size_t index = split + 1;
            while (index < needle_len && needle[index] == haystack[pos + index]) {
                index += 1;
            }
            if (index >= needle_len) {
                index = split + 1;
                while (index > 0 && needle[index - 1] == haystack[pos + index - 1]) {
                    index -= 1;
                }
                if (index == 0) {
                    return haystack + pos;
                }
                pos += period;
            } else {
                pos += index - split;
            }
        }
    }
    return NULL;
}

static inline int cheng_v3_twoway_contains_bytes(const unsigned char* haystack,
                                                 size_t haystack_len,
                                                 const unsigned char* needle,
                                                 size_t needle_len) {
    return cheng_v3_twoway_find_bytes(haystack, haystack_len, needle, needle_len) != NULL ? 1 : 0;
}

#endif

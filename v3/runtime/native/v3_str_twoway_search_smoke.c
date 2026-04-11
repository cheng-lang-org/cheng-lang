#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "v3_str_twoway_search.h"

static ptrdiff_t smoke_naive_find(const unsigned char* haystack,
                                  size_t haystack_len,
                                  const unsigned char* needle,
                                  size_t needle_len) {
    size_t index = 0;
    if (needle_len == 0) {
        return 0;
    }
    if (needle_len > haystack_len) {
        return -1;
    }
    for (index = 0; index + needle_len <= haystack_len; ++index) {
        if (memcmp(haystack + index, needle, needle_len) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static uint64_t smoke_pow_u64(uint64_t base, size_t exponent) {
    uint64_t value = 1;
    size_t i = 0;
    for (i = 0; i < exponent; ++i) {
        value *= base;
    }
    return value;
}

static void smoke_fill(unsigned char* out, size_t len, uint64_t code) {
    static const unsigned char ALPHABET[] = {'a', 'b', 0};
    size_t i = 0;
    for (i = 0; i < len; ++i) {
        out[i] = ALPHABET[code % 3u];
        code /= 3u;
    }
}

static int smoke_case(const unsigned char* haystack,
                      size_t haystack_len,
                      const unsigned char* needle,
                      size_t needle_len) {
    const unsigned char* found = cheng_v3_twoway_find_bytes(haystack, haystack_len, needle, needle_len);
    ptrdiff_t expected = smoke_naive_find(haystack, haystack_len, needle, needle_len);
    ptrdiff_t got = found != NULL ? (ptrdiff_t)(found - haystack) : -1;
    if (expected == got) {
        return 0;
    }
    fprintf(stderr,
            "twoway smoke mismatch hay_len=%zu needle_len=%zu expected=%td got=%td\n",
            haystack_len,
            needle_len,
            expected,
            got);
    return 1;
}

int main(void) {
    unsigned char haystack[8];
    unsigned char needle[5];
    size_t hay_len = 0;
    size_t needle_len = 0;
    if (smoke_case((const unsigned char*)"aaaaab", 6, (const unsigned char*)"aaab", 4) != 0 ||
        smoke_case((const unsigned char*)"abcxabcdabxabcdabcdabcy", 23, (const unsigned char*)"abcdabcy", 8) != 0 ||
        smoke_case((const unsigned char*)"ababababca", 10, (const unsigned char*)"abababca", 8) != 0) {
        return 1;
    }
    for (hay_len = 0; hay_len <= 7; ++hay_len) {
        uint64_t hay_total = smoke_pow_u64(3u, hay_len);
        for (needle_len = 0; needle_len <= 4; ++needle_len) {
            uint64_t needle_total = smoke_pow_u64(3u, needle_len);
            uint64_t hay_code = 0;
            for (hay_code = 0; hay_code < hay_total; ++hay_code) {
                uint64_t needle_code = 0;
                smoke_fill(haystack, hay_len, hay_code);
                for (needle_code = 0; needle_code < needle_total; ++needle_code) {
                    smoke_fill(needle, needle_len, needle_code);
                    if (smoke_case(haystack, hay_len, needle, needle_len) != 0) {
                        return 1;
                    }
                }
            }
        }
    }
    puts("v3 twoway search smoke ok");
    return 0;
}

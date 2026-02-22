#include "string_utils.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
size_t strlen(const char *str) {
    size_t len = 0;
    if (str) {
        while (*str++) {
            len++;
        }
    }
    return len;
}
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
char *strcpy(char *dest, const char *src) {
    char *orig_dest = dest;
    if (dest && src) {
        while ((*dest++ = *src++)) {
            /* Copy each character including null terminator */
        }
    }
    return orig_dest;
}
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) {
        return 0;
    }
    
    while (n > 0) {
        if (*s1 != *s2) {
            return (*(unsigned char *)s1 - *(unsigned char *)s2);
        }
        if (*s1 == '\0') {
            return 0;
        }
        s1++;
        s2++;
        n--;
    }
    return 0;
}
#pragma GCC diagnostic pop

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    if (d && s) {
        while (n--) {
            *d++ = *s++;
        }
    }
    return dest;
}

#pragma GCC push_options
#pragma GCC optimize("O0")
void *memset(void *dest, int c, size_t n) {
    unsigned char *d = (unsigned char *)dest;

    if (d) {
        while (n--) {
            *d++ = (unsigned char)c;
        }
    }
    return dest;
}
#pragma GCC pop_options

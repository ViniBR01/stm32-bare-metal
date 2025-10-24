#include "string_utils.h"

size_t strlen(const char *str) {
    size_t len = 0;
    if (str) {
        while (*str++) {
            len++;
        }
    }
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *orig_dest = dest;
    if (dest && src) {
        while ((*dest++ = *src++)) {
            /* Copy each character including null terminator */
        }
    }
    return orig_dest;
}

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

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Calculate the length of a string
 * @param str The string to measure
 * @return The length of the string (not including null terminator)
 */
size_t strlen(const char *str);

/**
 * @brief Copy a string from source to destination
 * @param dest Destination buffer
 * @param src Source string
 * @return Pointer to destination buffer
 */
char *strcpy(char *dest, const char *src);

/**
 * @brief Compare two strings up to n characters
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Copy memory from source to destination
 * @param dest Destination buffer
 * @param src Source buffer
 * @param n Number of bytes to copy
 * @return Pointer to destination buffer
 */
void *memcpy(void *dest, const void *src, size_t n);

/**
 * @brief Fill memory with a constant byte
 * @param dest Destination buffer
 * @param c    Byte value to set (converted to unsigned char)
 * @param n    Number of bytes to fill
 * @return Pointer to destination buffer
 */
void *memset(void *dest, int c, size_t n);

#endif /* STRING_UTILS_H */

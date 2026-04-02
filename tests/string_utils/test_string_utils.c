/*
 * Unit tests for utils/src/string_utils.c
 *
 * Strategy: the project defines its own strlen, strcpy, strncmp, memcpy and
 * memset — symbols that also exist in the host libc.  To avoid duplicate-
 * symbol linker errors on macOS we rename each function via a preprocessor
 * macro before pulling in the implementation, giving us su_strlen, su_strcpy,
 * etc.  After the include the macros are undefined so that Unity and the rest
 * of the test file use the standard libc versions.
 */

/* ---- rename project symbols before including the implementation ---- */
#define strlen   su_strlen
#define strcpy   su_strcpy
#define strncmp  su_strncmp
#define memcpy   su_memcpy
#define memset   su_memset

#include "string_utils.h"                   /* renamed declarations */
#include "../../utils/src/string_utils.c"   /* renamed definitions  */

#undef strlen
#undef strcpy
#undef strncmp
#undef memcpy
#undef memset
/* ------------------------------------------------------------------- */

#include <string.h>
#include <stddef.h>
#include "unity.h"

/* ===== Unity boilerplate ===== */
void setUp(void)    {}
void tearDown(void) {}

/* ===================================================================
 * strlen tests
 * =================================================================== */

void test_strlen_normal_string(void) {
    TEST_ASSERT_EQUAL_size_t(5, su_strlen("hello"));
}

void test_strlen_empty_string(void) {
    TEST_ASSERT_EQUAL_size_t(0, su_strlen(""));
}

void test_strlen_single_char(void) {
    TEST_ASSERT_EQUAL_size_t(1, su_strlen("x"));
}

void test_strlen_null_returns_zero(void) {
    TEST_ASSERT_EQUAL_size_t(0, su_strlen(NULL));
}

/* ===================================================================
 * strcpy tests
 * =================================================================== */

void test_strcpy_copies_string(void) {
    char buf[16] = {0};
    su_strcpy(buf, "hello");
    TEST_ASSERT_EQUAL_STRING("hello", buf);
}

void test_strcpy_returns_dest(void) {
    char buf[16];
    char *ret = su_strcpy(buf, "hi");
    TEST_ASSERT_EQUAL_PTR(buf, ret);
}

void test_strcpy_null_terminates(void) {
    char buf[16];
    memset(buf, 0xFF, sizeof(buf));
    su_strcpy(buf, "abc");
    TEST_ASSERT_EQUAL_CHAR('\0', buf[3]);
}

void test_strcpy_empty_src(void) {
    char buf[4] = {'x', 'x', 'x', 'x'};
    su_strcpy(buf, "");
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
}

/* ===================================================================
 * strncmp tests
 * =================================================================== */

void test_strncmp_equal_strings(void) {
    TEST_ASSERT_EQUAL_INT(0, su_strncmp("abc", "abc", 3));
}

void test_strncmp_equal_up_to_n(void) {
    /* Only compare first 3 chars — both start with "abc" */
    TEST_ASSERT_EQUAL_INT(0, su_strncmp("abcX", "abcY", 3));
}

void test_strncmp_first_less(void) {
    /* 'a' < 'b' */
    TEST_ASSERT_LESS_THAN(0, su_strncmp("a", "b", 1));
}

void test_strncmp_first_greater(void) {
    /* 'b' > 'a' */
    TEST_ASSERT_GREATER_THAN(0, su_strncmp("b", "a", 1));
}

void test_strncmp_n_zero_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, su_strncmp("abc", "xyz", 0));
}

void test_strncmp_null_s1_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, su_strncmp(NULL, "abc", 3));
}

void test_strncmp_null_s2_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, su_strncmp("abc", NULL, 3));
}

void test_strncmp_prefix_match(void) {
    /* "led" is a prefix of "led_on"; comparing 3 chars → equal */
    TEST_ASSERT_EQUAL_INT(0, su_strncmp("led_on", "led", 3));
}

/* ===================================================================
 * memcpy tests
 * =================================================================== */

void test_memcpy_copies_bytes(void) {
    const unsigned char src[] = {0x01, 0x02, 0x03, 0x04};
    unsigned char dst[4]      = {0};
    su_memcpy(dst, src, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 4);
}

void test_memcpy_returns_dest(void) {
    char src[4] = "abc";
    char dst[4];
    void *ret = su_memcpy(dst, src, 4);
    TEST_ASSERT_EQUAL_PTR(dst, ret);
}

void test_memcpy_zero_bytes_no_crash(void) {
    char dst[4] = {'x', 'x', 'x', 'x'};
    su_memcpy(dst, "abcd", 0);
    /* dst should be unchanged */
    TEST_ASSERT_EQUAL_CHAR('x', dst[0]);
}

/* ===================================================================
 * memset tests
 * =================================================================== */

void test_memset_fills_buffer(void) {
    char buf[8];
    su_memset(buf, 0xAB, sizeof(buf));
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, (unsigned char)buf[i]);
    }
}

void test_memset_zero_fill(void) {
    char buf[4] = {1, 2, 3, 4};
    su_memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(0, buf[i]);
    }
}

void test_memset_returns_dest(void) {
    char buf[4];
    void *ret = su_memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_PTR(buf, ret);
}

void test_memset_zero_bytes_no_crash(void) {
    char buf[4] = {'x', 'x', 'x', 'x'};
    su_memset(buf, 0, 0);
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
}

/* ===================================================================
 * Test runner
 * =================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* strlen */
    RUN_TEST(test_strlen_normal_string);
    RUN_TEST(test_strlen_empty_string);
    RUN_TEST(test_strlen_single_char);
    RUN_TEST(test_strlen_null_returns_zero);

    /* strcpy */
    RUN_TEST(test_strcpy_copies_string);
    RUN_TEST(test_strcpy_returns_dest);
    RUN_TEST(test_strcpy_null_terminates);
    RUN_TEST(test_strcpy_empty_src);

    /* strncmp */
    RUN_TEST(test_strncmp_equal_strings);
    RUN_TEST(test_strncmp_equal_up_to_n);
    RUN_TEST(test_strncmp_first_less);
    RUN_TEST(test_strncmp_first_greater);
    RUN_TEST(test_strncmp_n_zero_returns_zero);
    RUN_TEST(test_strncmp_null_s1_returns_zero);
    RUN_TEST(test_strncmp_null_s2_returns_zero);
    RUN_TEST(test_strncmp_prefix_match);

    /* memcpy */
    RUN_TEST(test_memcpy_copies_bytes);
    RUN_TEST(test_memcpy_returns_dest);
    RUN_TEST(test_memcpy_zero_bytes_no_crash);

    /* memset */
    RUN_TEST(test_memset_fills_buffer);
    RUN_TEST(test_memset_zero_fill);
    RUN_TEST(test_memset_returns_dest);
    RUN_TEST(test_memset_zero_bytes_no_crash);

    return UNITY_END();
}

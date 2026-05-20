#include "skeleton.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_skeleton_version_returns_1(void)
{
    TEST_ASSERT_EQUAL_INT(1, skeleton_version());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_skeleton_version_returns_1);
    return UNITY_END();
}

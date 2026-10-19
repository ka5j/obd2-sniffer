/*
 * Shared helpers for the Unity unit tests.
 */
#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "unity.h"

/*
 * Run one test function, honouring the command-line name filter
 * (`test_x -n test_case`). Unity's plain RUN_TEST ignores the filter; only
 * its generated runners apply it. CMake registers one CTest test per
 * UNIT_TEST(...) line, each running a single case, so reports list every
 * case individually.
 */
#define UNIT_TEST(fn)                                                                              \
    do {                                                                                           \
        Unity.CurrentTestName = #fn;                                                               \
        if (UnityTestMatches()) {                                                                  \
            RUN_TEST(fn);                                                                          \
        }                                                                                          \
    } while (0)

#endif /* TEST_SUPPORT_H */

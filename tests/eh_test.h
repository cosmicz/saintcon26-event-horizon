/* Minimal test harness: each test is a void function registered in
 * test_main.c; ctest runs each by name so failures are reported per test. */
#ifndef EH_TEST_H
#define EH_TEST_H

#include <stdio.h>

extern int eh_test_failures;

#define EH_CHECK(cond)                                                              \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            eh_test_failures++;                                                     \
        }                                                                           \
    } while (0)

#define EH_CHECK_MSG(cond, ...)                                                     \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: check failed: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                           \
            fputc('\n', stderr);                                                    \
            eh_test_failures++;                                                     \
        }                                                                           \
    } while (0)

#endif

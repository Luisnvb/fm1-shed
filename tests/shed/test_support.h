/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_TEST_SUPPORT_H
#define FM1_SHED_TEST_SUPPORT_H

#include <stdio.h>

extern unsigned fm1_shed_test_checks;
extern unsigned fm1_shed_test_failures;

#define CHECK(cond) do { \
    fm1_shed_test_checks++; \
    if (!(cond)) { \
        fm1_shed_test_failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#endif

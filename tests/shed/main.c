/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "tests.h"
#include <stdio.h>

unsigned fm1_shed_test_checks;
unsigned fm1_shed_test_failures;

int main(void)
{
    test_theory();
    test_exercises();
    test_progress();
    printf("shed tests: %u checks, %u failures\n", fm1_shed_test_checks, fm1_shed_test_failures);
    return fm1_shed_test_failures ? 1 : 0;
}

/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "test_support.h"
#include "progress.h"

void test_progress(void)
{
    struct fm1_shed_session s;

    fm1_shed_session_reset(&s);
    CHECK_EQ(s.asked, 0U);
    CHECK_EQ(fm1_shed_session_percent(&s), 0U);
    CHECK(!fm1_shed_session_done(&s));

    fm1_shed_session_record(&s, 1);
    fm1_shed_session_record(&s, 1);
    fm1_shed_session_record(&s, 0);
    fm1_shed_session_record(&s, 1);
    CHECK_EQ(s.asked, 4U);
    CHECK_EQ(s.correct, 3U);
    CHECK_EQ(s.streak, 1U);       /* the miss reset it before the last hit */
    CHECK_EQ(s.best_streak, 2U);  /* the first two hits */
    CHECK_EQ(fm1_shed_session_percent(&s), 75U);

    fm1_shed_session_reset(&s);
    {
        unsigned i;
        for (i = 0; i < FM1_SHED_SESSION_LENGTH - 1; i++)
            fm1_shed_session_record(&s, 1);
        CHECK(!fm1_shed_session_done(&s));
        fm1_shed_session_record(&s, 0);
        CHECK(fm1_shed_session_done(&s));
        CHECK_EQ(fm1_shed_session_percent(&s), 95U); /* 19/20, rounds up */
    }

    /* NULL is handled rather than dereferenced. */
    fm1_shed_session_reset(NULL);
    fm1_shed_session_record(NULL, 1);
    CHECK(!fm1_shed_session_done(NULL));
    CHECK_EQ(fm1_shed_session_percent(NULL), 0U);
}

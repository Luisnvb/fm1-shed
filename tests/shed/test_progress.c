/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "test_support.h"
#include "progress.h"

static void test_session(void)
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

static void test_adapt_level(void)
{
    struct fm1_shed_session s;
    unsigned level, i;

    fm1_shed_session_reset(&s);
    level = 0;
    for (i = 0; i < FM1_SHED_ADAPT_UP_STREAK - 1; i++) {
        fm1_shed_session_record(&s, 1);
        level = fm1_shed_session_adapt_level(&s, level, 2);
    }
    CHECK_EQ(level, 0U); /* not yet a full streak */
    fm1_shed_session_record(&s, 1);
    level = fm1_shed_session_adapt_level(&s, level, 2);
    CHECK_EQ(level, 1U); /* FM1_SHED_ADAPT_UP_STREAK straight hits */
    for (i = 0; i < FM1_SHED_ADAPT_UP_STREAK; i++) {
        fm1_shed_session_record(&s, 1);
        level = fm1_shed_session_adapt_level(&s, level, 2);
    }
    CHECK_EQ(level, 2U); /* climbs again, but clamps at max_level */
    for (i = 0; i < FM1_SHED_ADAPT_UP_STREAK; i++) {
        fm1_shed_session_record(&s, 1);
        level = fm1_shed_session_adapt_level(&s, level, 2);
    }
    CHECK_EQ(level, 2U); /* stays clamped */

    fm1_shed_session_reset(&s);
    level = 2;
    for (i = 0; i < FM1_SHED_ADAPT_DOWN_STREAK; i++) {
        fm1_shed_session_record(&s, 0);
        level = fm1_shed_session_adapt_level(&s, level, 2);
    }
    CHECK_EQ(level, 1U); /* FM1_SHED_ADAPT_DOWN_STREAK straight misses */
    for (i = 0; i < FM1_SHED_ADAPT_DOWN_STREAK; i++) {
        fm1_shed_session_record(&s, 0);
        level = fm1_shed_session_adapt_level(&s, level, 2);
    }
    CHECK_EQ(level, 0U);
    for (i = 0; i < FM1_SHED_ADAPT_DOWN_STREAK; i++) {
        fm1_shed_session_record(&s, 0);
        level = fm1_shed_session_adapt_level(&s, level, 2);
    }
    CHECK_EQ(level, 0U); /* clamps at zero */

    CHECK_EQ(fm1_shed_session_adapt_level(NULL, 1, 2), 1U);
}

static void test_scores(void)
{
    struct fm1_shed_scores scores;
    struct fm1_shed_rng rng;
    unsigned i;

    fm1_shed_scores_reset(&scores);
    CHECK_EQ(scores.value[FM1_SHED_EX_DEGREE][0], FM1_SHED_SCORE_MAX / 2U);

    for (i = 0; i < FM1_SHED_SCORE_MAX + 4U; i++)
        fm1_shed_scores_record(&scores, FM1_SHED_EX_DEGREE, 3, 1);
    CHECK_EQ(scores.value[FM1_SHED_EX_DEGREE][3], (uint8_t)FM1_SHED_SCORE_MAX); /* caps */

    fm1_shed_scores_reset(&scores);
    fm1_shed_scores_record(&scores, FM1_SHED_EX_DEGREE, 5, 0);
    CHECK_EQ(scores.value[FM1_SHED_EX_DEGREE][5], FM1_SHED_SCORE_MAX / 2U - 2U);
    for (i = 0; i < 10; i++)
        fm1_shed_scores_record(&scores, FM1_SHED_EX_DEGREE, 5, 0);
    CHECK_EQ(scores.value[FM1_SHED_EX_DEGREE][5], 0U); /* floors at zero */

    /* Out-of-range arguments are a no-op, not a crash. */
    fm1_shed_scores_record(&scores, FM1_SHED_EX_DEGREE, FM1_SHED_SCORE_POOL_MAX, 1);
    fm1_shed_scores_record(NULL, FM1_SHED_EX_DEGREE, 0, 1);
    fm1_shed_scores_record(&scores, (enum fm1_shed_exercise_type)99, 0, 1);

    /* pick(): an item driven far below the others gets picked often, but
     * not literally every single time (the 40% uniform fallback). */
    fm1_shed_scores_reset(&scores);
    for (i = 0; i < 6; i++)
        fm1_shed_scores_record(&scores, FM1_SHED_EX_CHORD_QUALITY, 2, 0);
    fm1_shed_rng_seed(&rng, 909);
    {
        unsigned picked_weak = 0, other = 0;
        for (i = 0; i < 400; i++) {
            unsigned index = fm1_shed_scores_pick(&scores, FM1_SHED_EX_CHORD_QUALITY, 6, &rng);
            CHECK(index < 6);
            if (index == 2)
                picked_weak++;
            else
                other++;
        }
        CHECK(picked_weak > other);  /* clearly biased towards the weak item */
        CHECK(other > 0);            /* but not exclusively -- still reviews the rest */
    }

    fm1_shed_rng_seed(&rng, 1);
    CHECK_EQ(fm1_shed_scores_pick(NULL, FM1_SHED_EX_DEGREE, 7, &rng), 0U);
    CHECK_EQ(fm1_shed_scores_pick(&scores, FM1_SHED_EX_DEGREE, 0, &rng), 0U);
}

void test_progress(void)
{
    test_session();
    test_adapt_level();
    test_scores();
}

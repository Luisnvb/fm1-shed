/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "progress.h"
#include <stddef.h>

void fm1_shed_session_reset(struct fm1_shed_session *s)
{
    if (!s)
        return;
    s->asked = s->correct = s->streak = s->best_streak = s->miss_streak = 0;
}

void fm1_shed_session_record(struct fm1_shed_session *s, int correct)
{
    if (!s)
        return;
    s->asked++;
    if (correct) {
        s->correct++;
        s->streak++;
        s->miss_streak = 0;
        if (s->streak > s->best_streak)
            s->best_streak = s->streak;
    } else {
        s->streak = 0;
        s->miss_streak++;
    }
}

int fm1_shed_session_done(const struct fm1_shed_session *s)
{
    return s && s->asked >= FM1_SHED_SESSION_LENGTH;
}

unsigned fm1_shed_session_percent(const struct fm1_shed_session *s)
{
    if (!s || !s->asked)
        return 0;
    return (s->correct * 100U + s->asked / 2U) / s->asked;
}

unsigned fm1_shed_session_adapt_level(const struct fm1_shed_session *s, unsigned level, unsigned max_level)
{
    if (!s)
        return level;
    if (s->streak && s->streak % FM1_SHED_ADAPT_UP_STREAK == 0 && level < max_level)
        return level + 1;
    if (s->miss_streak && s->miss_streak % FM1_SHED_ADAPT_DOWN_STREAK == 0 && level > 0)
        return level - 1;
    return level;
}

void fm1_shed_scores_reset(struct fm1_shed_scores *scores)
{
    unsigned type, index;
    if (!scores)
        return;
    for (type = 0; type < FM1_SHED_EX_TYPE_COUNT; type++)
        for (index = 0; index < FM1_SHED_SCORE_POOL_MAX; index++)
            scores->value[type][index] = FM1_SHED_SCORE_MAX / 2U;
}

void fm1_shed_scores_record(struct fm1_shed_scores *scores, enum fm1_shed_exercise_type type,
    unsigned pool_index, int correct)
{
    uint8_t *v;
    if (!scores || (unsigned)type >= FM1_SHED_EX_TYPE_COUNT || pool_index >= FM1_SHED_SCORE_POOL_MAX)
        return;
    v = &scores->value[type][pool_index];
    if (correct)
        *v = (uint8_t)(*v < FM1_SHED_SCORE_MAX ? *v + 1U : FM1_SHED_SCORE_MAX);
    else
        *v = (uint8_t)(*v >= 2U ? *v - 2U : 0U);
}

unsigned fm1_shed_scores_pick(const struct fm1_shed_scores *scores, enum fm1_shed_exercise_type type,
    unsigned pool_size, struct fm1_shed_rng *rng)
{
    unsigned i, min_value, min_count, pick;
    if (!scores || !rng || !pool_size || (unsigned)type >= FM1_SHED_EX_TYPE_COUNT)
        return 0;
    if (pool_size > FM1_SHED_SCORE_POOL_MAX)
        pool_size = FM1_SHED_SCORE_POOL_MAX;
    if (fm1_shed_rng_below(rng, 100U) >= 60U) /* 40% of the time: plain uniform, for variety */
        return fm1_shed_rng_below(rng, pool_size);
    min_value = FM1_SHED_SCORE_MAX + 1U;
    for (i = 0; i < pool_size; i++)
        if (scores->value[type][i] < min_value)
            min_value = scores->value[type][i];
    min_count = 0;
    for (i = 0; i < pool_size; i++)
        if (scores->value[type][i] == min_value)
            min_count++;
    pick = fm1_shed_rng_below(rng, min_count);
    for (i = 0; i < pool_size; i++) {
        if (scores->value[type][i] != min_value)
            continue;
        if (pick == 0)
            return i;
        pick--;
    }
    return 0; /* unreachable: min_count counted at least one matching i above */
}

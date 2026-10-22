/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "progress.h"
#include <stddef.h>

void fm1_shed_session_reset(struct fm1_shed_session *s)
{
    if (!s)
        return;
    s->asked = s->correct = s->streak = s->best_streak = 0;
}

void fm1_shed_session_record(struct fm1_shed_session *s, int correct)
{
    if (!s)
        return;
    s->asked++;
    if (correct) {
        s->correct++;
        s->streak++;
        if (s->streak > s->best_streak)
            s->best_streak = s->streak;
    } else {
        s->streak = 0;
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

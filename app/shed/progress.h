/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_PROGRESS_H
#define FM1_SHED_PROGRESS_H

/* Session statistics, RAM only (SHED_SPEC.md section 3 "Sin escrituras en
 * flash en el MVP"). Spaced repetition and adaptive difficulty (section 5
 * "Pedagogía") are Fase 3; this is just the Fase 2 session counter and
 * running score a 20-question block needs for its summary. Pure C11, no
 * FM-1 SDK dependency. */

#ifdef __cplusplus
extern "C" {
#endif

#define FM1_SHED_SESSION_LENGTH 20U

struct fm1_shed_session {
    unsigned asked, correct, streak, best_streak;
};

void fm1_shed_session_reset(struct fm1_shed_session *s);

/* Records one answered question. */
void fm1_shed_session_record(struct fm1_shed_session *s, int correct);

/* 1 once `asked` has reached FM1_SHED_SESSION_LENGTH. */
int fm1_shed_session_done(const struct fm1_shed_session *s);

/* correct*100/asked, rounded to the nearest percent; 0 if nothing was asked yet. */
unsigned fm1_shed_session_percent(const struct fm1_shed_session *s);

#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_PROGRESS_H
#define FM1_SHED_PROGRESS_H

/* Session statistics, adaptive difficulty and spaced repetition, RAM only
 * (SHED_SPEC.md section 3 "Sin escrituras en flash en el MVP" and section 5
 * "Pedagogía"). Pure C11, no FM-1 SDK dependency; depends only on
 * exercises.h, for the exercise-type enum spaced repetition is indexed by
 * and the RNG spaced repetition's picker draws from. */

#include "exercises.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FM1_SHED_SESSION_LENGTH 20U

struct fm1_shed_session {
    unsigned asked, correct, streak, best_streak, miss_streak;
};

void fm1_shed_session_reset(struct fm1_shed_session *s);

/* Records one answered question. */
void fm1_shed_session_record(struct fm1_shed_session *s, int correct);

/* 1 once `asked` has reached FM1_SHED_SESSION_LENGTH. */
int fm1_shed_session_done(const struct fm1_shed_session *s);

/* correct*100/asked, rounded to the nearest percent; 0 if nothing was asked yet. */
unsigned fm1_shed_session_percent(const struct fm1_shed_session *s);

/* Adaptive difficulty (section 5 "la dificultad sube tras N aciertos
 * seguidos y baja tras varios fallos"): given the session's current
 * streak/miss_streak (as they stand right after fm1_shed_session_record()),
 * suggests the next difficulty level. Climbs by one every
 * FM1_SHED_ADAPT_UP_STREAK further correct answers in a row (clamped to
 * max_level), drops by one every FM1_SHED_ADAPT_DOWN_STREAK further misses
 * in a row (clamped to 0); otherwise returns `level` unchanged. */
#define FM1_SHED_ADAPT_UP_STREAK 5U
#define FM1_SHED_ADAPT_DOWN_STREAK 3U
unsigned fm1_shed_session_adapt_level(const struct fm1_shed_session *s, unsigned level, unsigned max_level);

/* Spaced repetition (section 5 "cada elemento tiene una puntuación; los
 * fallos aparecen más a menudo"): one score per (exercise type, pool
 * index), for the four exercises with a bounded, named pool of answers
 * (grado, calidad de acorde, tensión, escala -- see exercises.h). Scores
 * reset with the session; this is deliberately not persisted (RAM-only
 * MVP, SHED_SPEC.md section 3). */
#define FM1_SHED_SCORE_POOL_MAX 16U /* the largest pool is #7's 16 scales */
#define FM1_SHED_SCORE_MAX 8U
struct fm1_shed_scores {
    uint8_t value[FM1_SHED_EX_TYPE_COUNT][FM1_SHED_SCORE_POOL_MAX];
};
void fm1_shed_scores_reset(struct fm1_shed_scores *scores);
/* correct raises the (type, pool_index) score by one (capped); a miss
 * drops it by two (floored at zero), so misses resurface faster than
 * streaks of hits bury them. pool_index beyond FM1_SHED_SCORE_POOL_MAX is a
 * no-op (every real pool fits). */
void fm1_shed_scores_record(struct fm1_shed_scores *scores, enum fm1_shed_exercise_type type,
    unsigned pool_index, int correct);
/* Picks a pool index in [0, pool_size): most of the time (60%) the lowest
 * (most overdue) score, picked at random among ties; the rest of the time
 * a plain uniform draw, so the session doesn't narrow to a single weak
 * item and everything still gets reviewed. */
unsigned fm1_shed_scores_pick(const struct fm1_shed_scores *scores, enum fm1_shed_exercise_type type,
    unsigned pool_size, struct fm1_shed_rng *rng);

#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_EXERCISES_H
#define FM1_SHED_EXERCISES_H

/* Question generator and answer evaluator for Shed (SHED_SPEC.md section 5).
 * Pure C11, no FM-1 SDK dependencies, same as theory.h. Scoring, spaced
 * repetition and adaptive difficulty live in progress.c (later phase); this
 * module only turns (exercise type, key, difficulty level) into a question
 * and checks a played note or a chosen chord type against it.
 *
 * Fase 1 implements exercise #1 (grado funcional) and #4 (calidad de
 * acorde) only, the pair Fase 2 wires into the simulator. Exercises
 * #2, #3, #5..#9 are added in Fase 3. */

#include "theory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Small, deterministic PRNG (xorshift32) so question generation is
 * reproducible in tests. Not for anything security-sensitive. */
struct fm1_shed_rng {
    uint32_t state;
};
void fm1_shed_rng_seed(struct fm1_shed_rng *rng, uint32_t seed);
uint32_t fm1_shed_rng_next(struct fm1_shed_rng *rng);
/* Uniform-ish value in [0, bound). bound must be > 0. The small modulo bias
 * is irrelevant at the pool sizes used here (<= 13). */
unsigned fm1_shed_rng_below(struct fm1_shed_rng *rng, unsigned bound);

/* Key rotation, SHED_SPEC.md principle 4 ("Las 12 tonalidades"). */
enum fm1_shed_key_mode {
    FM1_SHED_KEY_FIXED = 0,
    FM1_SHED_KEY_FOURTHS,  /* ascend by a perfect fourth (circle of fourths) */
    FM1_SHED_KEY_RANDOM
};
/* Returns the next tonic (pitch class 0..11) given the previous one. */
int fm1_shed_next_key(int previous_key, enum fm1_shed_key_mode mode, struct fm1_shed_rng *rng);

enum fm1_shed_exercise_type {
    FM1_SHED_EX_DEGREE = 0,      /* #1 grado funcional */
    FM1_SHED_EX_CHORD_QUALITY,   /* #4 calidad de acorde */
    FM1_SHED_EX_TYPE_COUNT
};

/* A generated question. Fields unused by `type` are set to a sentinel
 * (-1 for pitch/degree fields, FM1_SHED_CHORD_COUNT for the chord field)
 * rather than left undefined. */
struct fm1_shed_question {
    enum fm1_shed_exercise_type type;
    int key;                          /* tonic pitch class 0..11 */
    int degree;                       /* FM1_SHED_EX_DEGREE: semitones above the tonic, 0..11 */
    enum fm1_shed_chord_type chord;   /* FM1_SHED_EX_CHORD_QUALITY: chord quality to identify */
    int chord_root;                   /* FM1_SHED_EX_CHORD_QUALITY: pitch class the chord is voiced on */
};

/* Difficulty bands for #4 (calidad de acorde): level 0 is the plainest
 * tetrads, each higher level adds richer qualities, until every chord type
 * in theory.h is in the pool. */
unsigned fm1_shed_chord_pool_size(unsigned level);
enum fm1_shed_chord_type fm1_shed_chord_pool_at(unsigned level, unsigned index);

/* Generates a question of `type` in `key` (any int, normalized to 0..11) at
 * `level`. Returns 1 on success, 0 if `type` isn't implemented yet. */
int fm1_shed_question_generate(struct fm1_shed_question *q,
    enum fm1_shed_exercise_type type, int key, unsigned level, struct fm1_shed_rng *rng);

/* Evaluates a played note (any MIDI note number or pitch class; only pitch
 * class matters) against a #1 question. Returns 1 correct, 0 incorrect,
 * -1 if `q` is not a FM1_SHED_EX_DEGREE question. */
int fm1_shed_question_check_note(const struct fm1_shed_question *q, int played_note);

/* Evaluates a chosen chord type against a #4 question. Returns 1 correct,
 * 0 incorrect, -1 if `q` is not a FM1_SHED_EX_CHORD_QUALITY question. */
int fm1_shed_question_check_chord(const struct fm1_shed_question *q, enum fm1_shed_chord_type answer);

#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_EXERCISES_H
#define FM1_SHED_EXERCISES_H

/* Question generator and answer evaluator for Shed (SHED_SPEC.md section 5).
 * Pure C11, no FM-1 SDK dependencies, same as theory.h. Scoring, spaced
 * repetition and adaptive difficulty live in progress.c; this module turns
 * (exercise type, key, difficulty level[, pool index]) into a question and
 * checks an answer (a played note, a chosen chord/tension/scale, or a
 * played-back note sequence) against it.
 *
 * All nine exercises from section 5 are implemented. Four have a bounded,
 * meaningful "pool" of distinct answers (degree, chord quality, tension,
 * scale/mode) and so can drive progress.c's spaced repetition by picking
 * *which* pool index to ask next; the other five (intervals, arpeggio,
 * bass movement, melodic dictation) generate from a wide/continuous
 * space where "repeat the weak item" doesn't apply the same way, and always
 * draw uniformly from `rng`. */

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
 * is irrelevant at the pool sizes used here (<= 16). */
unsigned fm1_shed_rng_below(struct fm1_shed_rng *rng, unsigned bound);

/* Key rotation, SHED_SPEC.md principle 4 ("All 12 keys"). */
enum fm1_shed_key_mode {
    FM1_SHED_KEY_FIXED = 0,
    FM1_SHED_KEY_FOURTHS,  /* ascend by a perfect fourth (circle of fourths) */
    FM1_SHED_KEY_RANDOM
};
/* Returns the next tonic (pitch class 0..11) given the previous one. */
int fm1_shed_next_key(int previous_key, enum fm1_shed_key_mode mode, struct fm1_shed_rng *rng);

enum fm1_shed_exercise_type {
    FM1_SHED_EX_DEGREE = 0,          /* #1 functional degree */
    FM1_SHED_EX_CHORD_QUALITY,       /* #4 chord quality */
    FM1_SHED_EX_INTERVAL_MELODIC,    /* #2 melodic interval */
    FM1_SHED_EX_INTERVAL_HARMONIC,   /* #3 harmonic interval */
    FM1_SHED_EX_TENSION,             /* #5 tension over a dominant */
    FM1_SHED_EX_ARPEGGIO,            /* #6 arpeggio */
    FM1_SHED_EX_SCALE,                /* #7 scale/mode */
    FM1_SHED_EX_BASS_MOVEMENT,       /* #8 bass movement */
    FM1_SHED_EX_MELODIC_DICTATION,   /* #9 melodic dictation */
    FM1_SHED_EX_TYPE_COUNT
};

/* Tensions offered over a dominant 7th (#5), as semitones above the root
 * (left un-reduced like theory.h's chord intervals: 13 = ♭9, 20 = ♭13...). */
enum fm1_shed_tension {
    FM1_SHED_TENSION_9 = 0,
    FM1_SHED_TENSION_FLAT9,
    FM1_SHED_TENSION_SHARP9,
    FM1_SHED_TENSION_SHARP11,
    FM1_SHED_TENSION_13,
    FM1_SHED_TENSION_FLAT13,
    FM1_SHED_TENSION_COUNT
};
int fm1_shed_tension_semitones(enum fm1_shed_tension tension);

/* The reference note #2/#3 build their interval from: middle C's octave,
 * on the key's tonic (60 + pitch class). Shared with player.c so the
 * stimulus and the evaluation always agree on what "the reference note"
 * sounded like. */
int fm1_shed_interval_reference_note(int key);

#define FM1_SHED_SEQUENCE_MAX 8U /* #9's phrase can be up to 8 notes */

/* A generated question. Fields unused by `type` are left at a sentinel
 * (-1 for pitch/degree/interval fields, *_COUNT for enum fields, 0 for
 * sequence_count) rather than left undefined. */
struct fm1_shed_question {
    enum fm1_shed_exercise_type type;
    int key;                          /* tonic pitch class 0..11 */
    int degree;                       /* EX_DEGREE: semitones above the tonic, 0..11 */
    enum fm1_shed_chord_type chord;   /* EX_CHORD_QUALITY, EX_ARPEGGIO: the chord */
    int chord_root;                   /* EX_CHORD_QUALITY, EX_TENSION, EX_ARPEGGIO: pitch class root */
    int interval;                     /* EX_INTERVAL_MELODIC/HARMONIC: signed semitones from the
                                        * reference note (60 + key's pitch class); HARMONIC is
                                        * always positive ("play the upper note") */
    enum fm1_shed_tension tension;    /* EX_TENSION */
    enum fm1_shed_scale_type scale;   /* EX_SCALE */
    /* EX_ARPEGGIO/BASS_MOVEMENT/MELODIC_DICTATION: the answer sequence, as
     * pitch classes in order (evaluated by pitch class, like #1). For
     * EX_BASS_MOVEMENT, sequence_chord[i] is the chord quality voiced over
     * sequence[i] (its root), so the stimulus can play full chords while
     * the answer is just the roots, per the exercise's own description. */
    uint8_t sequence[FM1_SHED_SEQUENCE_MAX];
    uint8_t sequence_chord[FM1_SHED_SEQUENCE_MAX];
    uint8_t sequence_count;
};

/* Difficulty bands: level 0 is the smallest/plainest pool, each higher
 * level adds more until the whole type is covered. Shared by the plain
 * generator below and by progress.c's spaced-repetition picker, which
 * needs the pool size to choose *which* index to ask next. */
unsigned fm1_shed_degree_pool_size(unsigned level);         /* #1: chromatic degree, 0..11 */
int fm1_shed_degree_pool_at(unsigned level, unsigned index);
unsigned fm1_shed_chord_pool_size(unsigned level);           /* #4 */
enum fm1_shed_chord_type fm1_shed_chord_pool_at(unsigned level, unsigned index);
unsigned fm1_shed_tension_pool_size(unsigned level);         /* #5 */
enum fm1_shed_tension fm1_shed_tension_pool_at(unsigned level, unsigned index);
unsigned fm1_shed_scale_pool_size(unsigned level);           /* #7 */
enum fm1_shed_scale_type fm1_shed_scale_pool_at(unsigned level, unsigned index);

/* Generates a question of `type` in `key` (any int, normalized to 0..11) at
 * `level`, drawing uniformly from `rng` throughout. Returns 1 on success, 0
 * if `type` is out of range. */
int fm1_shed_question_generate(struct fm1_shed_question *q,
    enum fm1_shed_exercise_type type, int key, unsigned level, struct fm1_shed_rng *rng);

/* Same, but for a pooled type (DEGREE, CHORD_QUALITY, TENSION, SCALE) asks
 * specifically for `pool_index` (clamped into range) instead of drawing it
 * from `rng` -- `rng` is still used for any other randomness the question
 * needs (e.g. a chord's root). For a non-pooled type this is identical to
 * fm1_shed_question_generate() and `pool_index` is ignored. This is what
 * lets progress.c's spaced repetition pick which item to ask. */
int fm1_shed_question_generate_at(struct fm1_shed_question *q, enum fm1_shed_exercise_type type,
    int key, unsigned level, unsigned pool_index, struct fm1_shed_rng *rng);

/* Evaluates a played note (any MIDI note number or pitch class; only pitch
 * class matters) against a #1 question. Returns 1 correct, 0 incorrect,
 * -1 if `q` is not a FM1_SHED_EX_DEGREE question. */
int fm1_shed_question_check_note(const struct fm1_shed_question *q, int played_note);

/* Evaluates a chosen chord type against a #4 question. Returns 1 correct,
 * 0 incorrect, -1 if `q` is not a FM1_SHED_EX_CHORD_QUALITY question. */
int fm1_shed_question_check_chord(const struct fm1_shed_question *q, enum fm1_shed_chord_type answer);

/* Evaluates a played note against a #2/#3 question. Unlike check_note, this
 * compares the *exact* note (60 + key's pitch class + interval), not just
 * its pitch class: telling a 9th from a 2nd needs the octave. Returns 1
 * correct, 0 incorrect, -1 if `q` is not an interval question. */
int fm1_shed_question_check_interval_note(const struct fm1_shed_question *q, int played_note);

/* Evaluates a chosen tension against a #5 question. Returns 1 correct, 0
 * incorrect, -1 if `q` is not a FM1_SHED_EX_TENSION question. */
int fm1_shed_question_check_tension(const struct fm1_shed_question *q, enum fm1_shed_tension answer);

/* Evaluates a chosen scale against a #7 question. Returns 1 correct, 0
 * incorrect, -1 if `q` is not a FM1_SHED_EX_SCALE question. */
int fm1_shed_question_check_scale(const struct fm1_shed_question *q, enum fm1_shed_scale_type answer);

/* Sequence answers (#6, #8, #9): compares `played_note`'s pitch class
 * against q->sequence[*progress]; on a match advances *progress and
 * returns 1, otherwise leaves *progress unchanged and returns 0. -1 if `q`
 * isn't a sequence question or *progress is already past the end. Use
 * fm1_shed_question_sequence_done() to see whether the whole thing has now
 * been played correctly. */
int fm1_shed_question_check_sequence_note(const struct fm1_shed_question *q, unsigned *progress, int played_note);
int fm1_shed_question_sequence_done(const struct fm1_shed_question *q, unsigned progress);

/* Vamp mode (SHED_SPEC.md section 6): a looping progression the device
 * comps/walks while the user improvises. Pure data plus the lookup; the
 * actual looping playback (player.c) and LED/screen display (ui.c) are not
 * host-testable, but this table and the chord each step implies are. */
struct fm1_shed_vamp_step {
    enum fm1_shed_chord_type chord;
    enum fm1_shed_context context; /* for fm1_shed_chord_scale_lookup(): which scale this step suggests */
    int8_t offset;                 /* semitones from the vamp's key */
    uint16_t duration_ms;
};
struct fm1_shed_vamp {
    const char *name;
    const struct fm1_shed_vamp_step *steps;
    unsigned char count;
};
#define FM1_SHED_VAMP_COUNT 5U
/* NULL if index is out of range. */
const struct fm1_shed_vamp *fm1_shed_vamp(unsigned index);

#ifdef __cplusplus
}
#endif
#endif

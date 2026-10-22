/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "exercises.h"
#include <stddef.h>

/* Diatonic scale degrees of the major scale, as semitone offsets from the
 * tonic. Used both as the level-0 pool for #1 (grado funcional) and as the
 * pool of chord roots for #4 (calidad de acorde), so generated chords land
 * on musically ordinary scale degrees regardless of chord-quality level. */
static const uint8_t diatonic_degrees[7] = {0, 2, 4, 5, 7, 9, 11};

/* Chord qualities ordered simplest -> richest for #4's difficulty bands. */
static const enum fm1_shed_chord_type chord_difficulty_order[FM1_SHED_CHORD_COUNT] = {
    FM1_SHED_CHORD_MAJ7, FM1_SHED_CHORD_MIN7, FM1_SHED_CHORD_DOM7,
    FM1_SHED_CHORD_MIN7_FLAT5, FM1_SHED_CHORD_DIM7, FM1_SHED_CHORD_MIN6,
    FM1_SHED_CHORD_MIN_MAJ7, FM1_SHED_CHORD_DOM7_SUS4, FM1_SHED_CHORD_SIX_NINE,
    FM1_SHED_CHORD_MAJ7_SHARP11, FM1_SHED_CHORD_DOM7_FLAT9,
    FM1_SHED_CHORD_DOM7_SHARP11, FM1_SHED_CHORD_DOM7_ALT,
};
/* How many of the ordered qualities above are unlocked per level; the last
 * band always covers every chord type. */
static const unsigned char chord_level_pool[] = {6, 9, FM1_SHED_CHORD_COUNT};

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void fm1_shed_rng_seed(struct fm1_shed_rng *rng, uint32_t seed)
{
    rng->state = seed ? seed : 1U; /* xorshift32 can't recover from a zero state */
}

uint32_t fm1_shed_rng_next(struct fm1_shed_rng *rng)
{
    return xorshift32(&rng->state);
}

unsigned fm1_shed_rng_below(struct fm1_shed_rng *rng, unsigned bound)
{
    return bound ? fm1_shed_rng_next(rng) % bound : 0;
}

int fm1_shed_next_key(int previous_key, enum fm1_shed_key_mode mode, struct fm1_shed_rng *rng)
{
    switch (mode) {
    case FM1_SHED_KEY_FOURTHS:
        return fm1_shed_pitch_class(previous_key + 5);
    case FM1_SHED_KEY_RANDOM:
        return (int)fm1_shed_rng_below(rng, 12);
    case FM1_SHED_KEY_FIXED:
    default:
        return fm1_shed_pitch_class(previous_key);
    }
}

unsigned fm1_shed_chord_pool_size(unsigned level)
{
    unsigned bands = sizeof(chord_level_pool) / sizeof(chord_level_pool[0]);
    if (level >= bands)
        level = bands - 1;
    return chord_level_pool[level];
}

enum fm1_shed_chord_type fm1_shed_chord_pool_at(unsigned level, unsigned index)
{
    unsigned size = fm1_shed_chord_pool_size(level);
    if (index >= size)
        index = size ? size - 1 : 0;
    return chord_difficulty_order[index];
}

int fm1_shed_question_generate(struct fm1_shed_question *q,
    enum fm1_shed_exercise_type type, int key, unsigned level, struct fm1_shed_rng *rng)
{
    if (!q || !rng)
        return 0;
    key = fm1_shed_pitch_class(key);
    switch (type) {
    case FM1_SHED_EX_DEGREE:
        q->type = type;
        q->key = key;
        q->degree = (int)(level == 0 ? diatonic_degrees[fm1_shed_rng_below(rng, 7)]
                                      : fm1_shed_rng_below(rng, 12));
        q->chord = FM1_SHED_CHORD_COUNT;
        q->chord_root = -1;
        return 1;
    case FM1_SHED_EX_CHORD_QUALITY:
        q->type = type;
        q->key = key;
        q->degree = -1;
        q->chord = fm1_shed_chord_pool_at(level, fm1_shed_rng_below(rng, fm1_shed_chord_pool_size(level)));
        q->chord_root = fm1_shed_pitch_class(key + diatonic_degrees[fm1_shed_rng_below(rng, 7)]);
        return 1;
    default:
        return 0;
    }
}

int fm1_shed_question_check_note(const struct fm1_shed_question *q, int played_note)
{
    if (!q || q->type != FM1_SHED_EX_DEGREE)
        return -1;
    return fm1_shed_pitch_class(played_note) == fm1_shed_pitch_class(q->key + q->degree);
}

int fm1_shed_question_check_chord(const struct fm1_shed_question *q, enum fm1_shed_chord_type answer)
{
    if (!q || q->type != FM1_SHED_EX_CHORD_QUALITY)
        return -1;
    return answer == q->chord;
}

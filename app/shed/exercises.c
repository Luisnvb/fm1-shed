/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "exercises.h"
#include <stddef.h>

/* Diatonic scale degrees of the major scale, as semitone offsets from the
 * tonic. Used as the level-0 pool for #1 (grado funcional), the pool of
 * chord/tension roots for #4/#5, and the diatonic note pool for #9's
 * phrases. */
static const uint8_t diatonic_degrees[7] = {0, 2, 4, 5, 7, 9, 11};

/* #1 grado funcional: diatonic degrees first, then the five chromatic ones;
 * level 0 only unlocks the diatonic prefix, level >= 1 the whole octave. */
static const uint8_t degree_order[12] = {0, 2, 4, 5, 7, 9, 11, 1, 3, 6, 8, 10};
static const unsigned char degree_level_pool[] = {7, 12};

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

/* #5 tensión sobre dominante: natural 9/13 first (consonant, common),
 * altered ones last. Semitones above the root, un-reduced like theory.h. */
static const int8_t tension_semitones_table[FM1_SHED_TENSION_COUNT] = {
    [FM1_SHED_TENSION_9] = 14, [FM1_SHED_TENSION_FLAT9] = 13, [FM1_SHED_TENSION_SHARP9] = 15,
    [FM1_SHED_TENSION_SHARP11] = 18, [FM1_SHED_TENSION_13] = 21, [FM1_SHED_TENSION_FLAT13] = 20,
};
static const enum fm1_shed_tension tension_difficulty_order[FM1_SHED_TENSION_COUNT] = {
    FM1_SHED_TENSION_9, FM1_SHED_TENSION_13, FM1_SHED_TENSION_FLAT9,
    FM1_SHED_TENSION_FLAT13, FM1_SHED_TENSION_SHARP9, FM1_SHED_TENSION_SHARP11,
};
static const unsigned char tension_level_pool[] = {2, 4, FM1_SHED_TENSION_COUNT};

/* #7 escala/modo: modes of the major scale, then of melodic minor, then the
 * symmetric/specialty scales -- SHED_SPEC.md's own ordering. */
static const enum fm1_shed_scale_type scale_difficulty_order[FM1_SHED_SCALE_COUNT] = {
    FM1_SHED_SCALE_IONIAN, FM1_SHED_SCALE_DORIAN, FM1_SHED_SCALE_PHRYGIAN, FM1_SHED_SCALE_LYDIAN,
    FM1_SHED_SCALE_MIXOLYDIAN, FM1_SHED_SCALE_AEOLIAN, FM1_SHED_SCALE_LOCRIAN,
    FM1_SHED_SCALE_MELODIC_MINOR, FM1_SHED_SCALE_LYDIAN_DOMINANT, FM1_SHED_SCALE_LOCRIAN_NAT2,
    FM1_SHED_SCALE_ALTERED, FM1_SHED_SCALE_PHRYGIAN_DOMINANT,
    FM1_SHED_SCALE_DIM_WHOLE_HALF, FM1_SHED_SCALE_DIM_HALF_WHOLE,
    FM1_SHED_SCALE_WHOLE_TONE, FM1_SHED_SCALE_BEBOP_DOMINANT,
};
static const unsigned char scale_level_pool[] = {7, 12, FM1_SHED_SCALE_COUNT};

/* #8 movimiento del bajo: short named progressions, root offsets in
 * semitones from the key. SHED_SPEC.md: "II-V-I -> turnarounds -> blues,
 * rhythm changes". The last two still fit the exercise's own "2-4 acordes"
 * stimulus length by taking a short, representative fragment of the form
 * rather than the whole 12/32-bar tune. */
struct progression_step { unsigned char chord; signed char offset; };
struct progression { const struct progression_step *steps; unsigned char count; };

static const struct progression_step prog_ii_v_i[] = {
    {FM1_SHED_CHORD_MIN7, 2}, {FM1_SHED_CHORD_DOM7, 7}, {FM1_SHED_CHORD_MAJ7, 0},
};
static const struct progression_step prog_turnaround_vi[] = { /* I - vi - ii - V */
    {FM1_SHED_CHORD_MAJ7, 0}, {FM1_SHED_CHORD_MIN7, 9}, {FM1_SHED_CHORD_MIN7, 2}, {FM1_SHED_CHORD_DOM7, 7},
};
static const struct progression_step prog_turnaround_iii[] = { /* iii - VI7 - ii - V */
    {FM1_SHED_CHORD_MIN7, 4}, {FM1_SHED_CHORD_DOM7, 9}, {FM1_SHED_CHORD_MIN7, 2}, {FM1_SHED_CHORD_DOM7, 7},
};
static const struct progression_step prog_blues_turn[] = { /* I7 - IV7 - I7 - V7 */
    {FM1_SHED_CHORD_DOM7, 0}, {FM1_SHED_CHORD_DOM7, 5}, {FM1_SHED_CHORD_DOM7, 0}, {FM1_SHED_CHORD_DOM7, 7},
};
static const struct progression_step prog_rhythm_changes[] = { /* I - VI7 - ii - V */
    {FM1_SHED_CHORD_MAJ7, 0}, {FM1_SHED_CHORD_DOM7, 9}, {FM1_SHED_CHORD_MIN7, 2}, {FM1_SHED_CHORD_DOM7, 7},
};
static const struct progression progressions_level0[] = { {prog_ii_v_i, 3} };
static const struct progression progressions_level1[] = { {prog_turnaround_vi, 4}, {prog_turnaround_iii, 4} };
static const struct progression progressions_level2[] = { {prog_blues_turn, 4}, {prog_rhythm_changes, 4} };
static const struct progression *const progression_level_pool[] = {
    progressions_level0, progressions_level1, progressions_level2,
};
static const unsigned char progression_level_count[] = {
    sizeof progressions_level0 / sizeof progressions_level0[0],
    sizeof progressions_level1 / sizeof progressions_level1[0],
    sizeof progressions_level2 / sizeof progressions_level2[0],
};

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

int fm1_shed_tension_semitones(enum fm1_shed_tension tension)
{
    return (unsigned)tension < FM1_SHED_TENSION_COUNT ? tension_semitones_table[tension] : 0;
}

int fm1_shed_interval_reference_note(int key)
{
    return 60 + fm1_shed_pitch_class(key);
}

/* Generic "N unlocked, clamp the index" pattern shared by every pool. */
static unsigned pool_size(const unsigned char *bands, unsigned band_count, unsigned level)
{
    if (level >= band_count)
        level = band_count - 1;
    return bands[level];
}
static unsigned pool_clamp(unsigned size, unsigned index)
{
    if (index >= size)
        index = size ? size - 1 : 0;
    return index;
}

unsigned fm1_shed_degree_pool_size(unsigned level)
{
    return pool_size(degree_level_pool, sizeof degree_level_pool / sizeof degree_level_pool[0], level);
}
int fm1_shed_degree_pool_at(unsigned level, unsigned index)
{
    return degree_order[pool_clamp(fm1_shed_degree_pool_size(level), index)];
}

unsigned fm1_shed_chord_pool_size(unsigned level)
{
    return pool_size(chord_level_pool, sizeof chord_level_pool / sizeof chord_level_pool[0], level);
}
enum fm1_shed_chord_type fm1_shed_chord_pool_at(unsigned level, unsigned index)
{
    return chord_difficulty_order[pool_clamp(fm1_shed_chord_pool_size(level), index)];
}

unsigned fm1_shed_tension_pool_size(unsigned level)
{
    return pool_size(tension_level_pool, sizeof tension_level_pool / sizeof tension_level_pool[0], level);
}
enum fm1_shed_tension fm1_shed_tension_pool_at(unsigned level, unsigned index)
{
    return tension_difficulty_order[pool_clamp(fm1_shed_tension_pool_size(level), index)];
}

unsigned fm1_shed_scale_pool_size(unsigned level)
{
    return pool_size(scale_level_pool, sizeof scale_level_pool / sizeof scale_level_pool[0], level);
}
enum fm1_shed_scale_type fm1_shed_scale_pool_at(unsigned level, unsigned index)
{
    return scale_difficulty_order[pool_clamp(fm1_shed_scale_pool_size(level), index)];
}

static void clear_question(struct fm1_shed_question *q, enum fm1_shed_exercise_type type, int key)
{
    unsigned i;
    q->type = type;
    q->key = key;
    q->degree = -1;
    q->chord = FM1_SHED_CHORD_COUNT;
    q->chord_root = -1;
    q->interval = 0;
    q->tension = FM1_SHED_TENSION_COUNT;
    q->scale = FM1_SHED_SCALE_COUNT;
    q->sequence_count = 0;
    for (i = 0; i < FM1_SHED_SEQUENCE_MAX; i++) {
        q->sequence[i] = 0;
        q->sequence_chord[i] = 0;
    }
}

static int generate_interval(unsigned level, int harmonic, struct fm1_shed_rng *rng)
{
    unsigned span = level >= 2 ? 24U : 12U;
    unsigned magnitude = 1U + fm1_shed_rng_below(rng, span);
    int sign = 1;
    if (!harmonic && level >= 1 && fm1_shed_rng_below(rng, 2))
        sign = -1;
    return sign * (int)magnitude;
}

/* #6 draws its chord uniformly, like intervals/bass movement/dictation:
 * spaced repetition only applies to the four pooled exercises (see
 * exercises.h). */
static void generate_arpeggio(struct fm1_shed_question *q, unsigned level, struct fm1_shed_rng *rng)
{
    const struct fm1_shed_chord *c;
    unsigned start, i;
    q->chord = fm1_shed_chord_pool_at(level, fm1_shed_rng_below(rng, fm1_shed_chord_pool_size(level)));
    q->chord_root = q->key;
    c = fm1_shed_chord(q->chord);
    if (!c)
        return;
    start = level == 0 ? 0 : fm1_shed_rng_below(rng, c->count);
    q->sequence_count = c->count;
    for (i = 0; i < c->count; i++)
        q->sequence[i] = (uint8_t)fm1_shed_pitch_class(q->chord_root + c->intervals[(start + i) % c->count]);
}

static void generate_bass_movement(struct fm1_shed_question *q, unsigned level, struct fm1_shed_rng *rng)
{
    unsigned band = level >= 2 ? 2U : level;
    const struct progression *options = progression_level_pool[band];
    unsigned count = progression_level_count[band];
    const struct progression *p = &options[fm1_shed_rng_below(rng, count)];
    unsigned i;
    q->sequence_count = p->count;
    for (i = 0; i < p->count; i++) {
        q->sequence[i] = (uint8_t)fm1_shed_pitch_class(q->key + p->steps[i].offset);
        q->sequence_chord[i] = (uint8_t)p->steps[i].chord;
    }
}

static void generate_phrase(struct fm1_shed_question *q, unsigned level, struct fm1_shed_rng *rng)
{
    static const unsigned char min_len[] = {3, 4, 5}, max_len[] = {4, 6, 8}, chromatic_pct[] = {0, 20, 45};
    unsigned band = level >= 2 ? 2U : level;
    unsigned len = min_len[band] + fm1_shed_rng_below(rng, max_len[band] - min_len[band] + 1U);
    unsigned i;
    if (len > FM1_SHED_SEQUENCE_MAX)
        len = FM1_SHED_SEQUENCE_MAX;
    q->sequence_count = (uint8_t)len;
    for (i = 0; i < len; i++) {
        int degree = chromatic_pct[band] && fm1_shed_rng_below(rng, 100) < chromatic_pct[band] ?
            (int)fm1_shed_rng_below(rng, 12) : diatonic_degrees[fm1_shed_rng_below(rng, 7)];
        q->sequence[i] = (uint8_t)fm1_shed_pitch_class(q->key + degree);
    }
}

static int generate(struct fm1_shed_question *q, enum fm1_shed_exercise_type type, int key, unsigned level,
    unsigned pool_index, struct fm1_shed_rng *rng, int indexed)
{
    if (!q || !rng)
        return 0;
    key = fm1_shed_pitch_class(key);
    clear_question(q, type, key);
    switch (type) {
    case FM1_SHED_EX_DEGREE: {
        unsigned count = fm1_shed_degree_pool_size(level);
        q->degree = fm1_shed_degree_pool_at(level, indexed ? pool_clamp(count, pool_index) : fm1_shed_rng_below(rng, count));
        return 1;
    }
    case FM1_SHED_EX_CHORD_QUALITY: {
        unsigned count = fm1_shed_chord_pool_size(level);
        q->chord = fm1_shed_chord_pool_at(level, indexed ? pool_clamp(count, pool_index) : fm1_shed_rng_below(rng, count));
        q->chord_root = fm1_shed_pitch_class(key + diatonic_degrees[fm1_shed_rng_below(rng, 7)]);
        return 1;
    }
    case FM1_SHED_EX_INTERVAL_MELODIC:
        q->interval = generate_interval(level, 0, rng);
        return 1;
    case FM1_SHED_EX_INTERVAL_HARMONIC:
        q->interval = generate_interval(level, 1, rng);
        return 1;
    case FM1_SHED_EX_TENSION: {
        unsigned count = fm1_shed_tension_pool_size(level);
        q->chord = FM1_SHED_CHORD_DOM7;
        q->chord_root = key;
        q->tension = fm1_shed_tension_pool_at(level, indexed ? pool_clamp(count, pool_index) : fm1_shed_rng_below(rng, count));
        return 1;
    }
    case FM1_SHED_EX_ARPEGGIO:
        generate_arpeggio(q, level, rng);
        return 1;
    case FM1_SHED_EX_SCALE: {
        unsigned count = fm1_shed_scale_pool_size(level);
        q->scale = fm1_shed_scale_pool_at(level, indexed ? pool_clamp(count, pool_index) : fm1_shed_rng_below(rng, count));
        return 1;
    }
    case FM1_SHED_EX_BASS_MOVEMENT:
        generate_bass_movement(q, level, rng);
        return 1;
    case FM1_SHED_EX_MELODIC_DICTATION:
        generate_phrase(q, level, rng);
        return 1;
    default:
        return 0;
    }
}

int fm1_shed_question_generate(struct fm1_shed_question *q,
    enum fm1_shed_exercise_type type, int key, unsigned level, struct fm1_shed_rng *rng)
{
    return generate(q, type, key, level, 0, rng, 0);
}

int fm1_shed_question_generate_at(struct fm1_shed_question *q, enum fm1_shed_exercise_type type,
    int key, unsigned level, unsigned pool_index, struct fm1_shed_rng *rng)
{
    return generate(q, type, key, level, pool_index, rng, 1);
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

int fm1_shed_question_check_interval_note(const struct fm1_shed_question *q, int played_note)
{
    if (!q || (q->type != FM1_SHED_EX_INTERVAL_MELODIC && q->type != FM1_SHED_EX_INTERVAL_HARMONIC))
        return -1;
    return played_note == fm1_shed_interval_reference_note(q->key) + q->interval;
}

int fm1_shed_question_check_tension(const struct fm1_shed_question *q, enum fm1_shed_tension answer)
{
    if (!q || q->type != FM1_SHED_EX_TENSION)
        return -1;
    return answer == q->tension;
}

int fm1_shed_question_check_scale(const struct fm1_shed_question *q, enum fm1_shed_scale_type answer)
{
    if (!q || q->type != FM1_SHED_EX_SCALE)
        return -1;
    return answer == q->scale;
}

int fm1_shed_question_check_sequence_note(const struct fm1_shed_question *q, unsigned *progress, int played_note)
{
    if (!q || !progress)
        return -1;
    if (q->type != FM1_SHED_EX_ARPEGGIO && q->type != FM1_SHED_EX_BASS_MOVEMENT &&
        q->type != FM1_SHED_EX_MELODIC_DICTATION)
        return -1;
    if (*progress >= q->sequence_count)
        return -1;
    if (fm1_shed_pitch_class(played_note) != q->sequence[*progress])
        return 0;
    (*progress)++;
    return 1;
}

int fm1_shed_question_sequence_done(const struct fm1_shed_question *q, unsigned progress)
{
    return q != NULL && progress >= q->sequence_count;
}

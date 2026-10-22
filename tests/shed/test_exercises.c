/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "test_support.h"
#include "exercises.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static int in_set(int value, const uint8_t *set, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; i++)
        if (set[i] == value)
            return 1;
    return 0;
}

static void test_rng(void)
{
    struct fm1_shed_rng a, b;
    unsigned i;

    fm1_shed_rng_seed(&a, 12345);
    fm1_shed_rng_seed(&b, 12345);
    for (i = 0; i < 64; i++)
        CHECK_EQ(fm1_shed_rng_next(&a), fm1_shed_rng_next(&b));

    /* A zero seed must not freeze xorshift32 at zero forever. */
    fm1_shed_rng_seed(&a, 0);
    CHECK(fm1_shed_rng_next(&a) != 0);

    fm1_shed_rng_seed(&a, 777);
    CHECK_EQ(fm1_shed_rng_below(&a, 1), 0U);
    for (i = 0; i < 256; i++)
        CHECK(fm1_shed_rng_below(&a, 13) < 13);
}

static void test_next_key(void)
{
    int key, i;
    uint8_t seen[12] = {0};

    CHECK_EQ(fm1_shed_next_key(5, FM1_SHED_KEY_FIXED, NULL), 5);
    CHECK_EQ(fm1_shed_next_key(-1, FM1_SHED_KEY_FIXED, NULL), 11);

    /* Circle of fourths: 12 steps of +5 semitones visit every pitch class
     * exactly once and return to the start. */
    key = 0;
    for (i = 0; i < 12; i++) {
        CHECK(!seen[key]);
        seen[key] = 1;
        key = fm1_shed_next_key(key, FM1_SHED_KEY_FOURTHS, NULL);
    }
    CHECK_EQ(key, 0);
    for (i = 0; i < 12; i++)
        CHECK(seen[i]);

    {
        struct fm1_shed_rng rng;
        fm1_shed_rng_seed(&rng, 99);
        for (i = 0; i < 128; i++) {
            key = fm1_shed_next_key(key, FM1_SHED_KEY_RANDOM, &rng);
            CHECK(key >= 0 && key < 12);
        }
    }
}

static void test_chord_pool(void)
{
    unsigned i;
    CHECK_EQ(fm1_shed_chord_pool_size(0), 6U);
    CHECK_EQ(fm1_shed_chord_pool_size(1), 9U);
    CHECK_EQ(fm1_shed_chord_pool_size(2), (unsigned)FM1_SHED_CHORD_COUNT);
    CHECK_EQ(fm1_shed_chord_pool_size(99), (unsigned)FM1_SHED_CHORD_COUNT); /* clamps */
    CHECK(fm1_shed_chord_pool_size(0) < fm1_shed_chord_pool_size(1));
    CHECK(fm1_shed_chord_pool_size(1) < fm1_shed_chord_pool_size(2));

    /* The richest qualities (altered dominant, #11s, flat 9) must not be
     * reachable at level 0. */
    for (i = 0; i < fm1_shed_chord_pool_size(0); i++) {
        enum fm1_shed_chord_type t = fm1_shed_chord_pool_at(0, i);
        CHECK(t != FM1_SHED_CHORD_DOM7_ALT);
        CHECK(t != FM1_SHED_CHORD_DOM7_FLAT9);
        CHECK(t != FM1_SHED_CHORD_DOM7_SHARP11);
        CHECK(t != FM1_SHED_CHORD_MAJ7_SHARP11);
    }
    /* Every chord type is reachable at the top level. */
    for (i = 0; i < FM1_SHED_CHORD_COUNT; i++) {
        unsigned j, found = 0;
        for (j = 0; j < fm1_shed_chord_pool_size(2); j++)
            if (fm1_shed_chord_pool_at(2, j) == (enum fm1_shed_chord_type)i)
                found = 1;
        CHECK(found);
    }
}

static void test_question_degree(void)
{
    static const uint8_t diatonic[7] = {0, 2, 4, 5, 7, 9, 11};
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    uint8_t seen_chromatic[12] = {0};
    int i;

    fm1_shed_rng_seed(&rng, 42);
    for (i = 0; i < 200; i++) {
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_DEGREE, 3, 0, &rng));
        CHECK_EQ(q.type, FM1_SHED_EX_DEGREE);
        CHECK_EQ(q.key, 3);
        CHECK(in_set(q.degree, diatonic, 7));
        CHECK_EQ(q.chord, FM1_SHED_CHORD_COUNT);
        CHECK_EQ(q.chord_root, -1);
    }

    fm1_shed_rng_seed(&rng, 7);
    for (i = 0; i < 500; i++) {
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_DEGREE, 0, 1, &rng));
        CHECK(q.degree >= 0 && q.degree < 12);
        seen_chromatic[q.degree] = 1;
    }
    /* Level >= 1 must eventually reach every chromatic degree, including
     * non-diatonic ones such as ♭2 (1) or ♭6 (8). */
    for (i = 0; i < 12; i++)
        CHECK(seen_chromatic[i]);
}

static void test_question_chord_quality(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;

    fm1_shed_rng_seed(&rng, 1234);
    for (i = 0; i < 200; i++) {
        unsigned level = (unsigned)(i % 3);
        unsigned j, pool_match = 0;
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_CHORD_QUALITY, -5, level, &rng));
        CHECK_EQ(q.type, FM1_SHED_EX_CHORD_QUALITY);
        CHECK_EQ(q.key, fm1_shed_pitch_class(-5));
        CHECK_EQ(q.degree, -1);
        CHECK(q.chord_root >= 0 && q.chord_root < 12);
        for (j = 0; j < fm1_shed_chord_pool_size(level); j++)
            if (fm1_shed_chord_pool_at(level, j) == q.chord)
                pool_match = 1;
        CHECK(pool_match);
    }
}

static void test_generate_rejects_unimplemented_type(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    fm1_shed_rng_seed(&rng, 1);
    CHECK_EQ(fm1_shed_question_generate(&q, FM1_SHED_EX_TYPE_COUNT, 0, 0, &rng), 0);
    CHECK_EQ(fm1_shed_question_generate(NULL, FM1_SHED_EX_DEGREE, 0, 0, &rng), 0);
    CHECK_EQ(fm1_shed_question_generate(&q, FM1_SHED_EX_DEGREE, 0, 0, NULL), 0);
}

static void test_check_note(void)
{
    struct fm1_shed_question q;
    q.type = FM1_SHED_EX_DEGREE;
    q.key = 0;
    q.degree = 4; /* major third above C: E */
    q.chord = FM1_SHED_CHORD_COUNT;
    q.chord_root = -1;

    CHECK_EQ(fm1_shed_question_check_note(&q, 4), 1);
    CHECK_EQ(fm1_shed_question_check_note(&q, 4 + 12), 1);   /* octave above */
    CHECK_EQ(fm1_shed_question_check_note(&q, 4 - 24), 1);   /* two octaves below */
    CHECK_EQ(fm1_shed_question_check_note(&q, 5), 0);

    {
        struct fm1_shed_question wrong_type = q;
        wrong_type.type = FM1_SHED_EX_CHORD_QUALITY;
        CHECK_EQ(fm1_shed_question_check_note(&wrong_type, 4), -1);
    }
    CHECK_EQ(fm1_shed_question_check_note(NULL, 4), -1);
}

static void test_check_chord(void)
{
    struct fm1_shed_question q;
    q.type = FM1_SHED_EX_CHORD_QUALITY;
    q.key = 2;
    q.degree = -1;
    q.chord = FM1_SHED_CHORD_MIN7;
    q.chord_root = 2;

    CHECK_EQ(fm1_shed_question_check_chord(&q, FM1_SHED_CHORD_MIN7), 1);
    CHECK_EQ(fm1_shed_question_check_chord(&q, FM1_SHED_CHORD_DOM7), 0);

    {
        struct fm1_shed_question wrong_type = q;
        wrong_type.type = FM1_SHED_EX_DEGREE;
        CHECK_EQ(fm1_shed_question_check_chord(&wrong_type, FM1_SHED_CHORD_MIN7), -1);
    }
    CHECK_EQ(fm1_shed_question_check_chord(NULL, FM1_SHED_CHORD_MIN7), -1);
}

static void test_degree_pool(void)
{
    static const uint8_t diatonic[7] = {0, 2, 4, 5, 7, 9, 11};
    unsigned i;
    CHECK_EQ(fm1_shed_degree_pool_size(0), 7U);
    CHECK_EQ(fm1_shed_degree_pool_size(1), 12U);
    CHECK_EQ(fm1_shed_degree_pool_size(99), 12U); /* clamps */
    for (i = 0; i < 7; i++)
        CHECK(in_set(fm1_shed_degree_pool_at(0, i), diatonic, 7));
    {
        uint8_t seen[12] = {0};
        for (i = 0; i < 12; i++)
            seen[fm1_shed_degree_pool_at(1, i)] = 1;
        for (i = 0; i < 12; i++)
            CHECK(seen[i]); /* every chromatic degree reachable at level 1 */
    }
}

static void test_tension(void)
{
    unsigned i;
    CHECK_EQ(fm1_shed_tension_semitones(FM1_SHED_TENSION_9), 14);
    CHECK_EQ(fm1_shed_tension_semitones(FM1_SHED_TENSION_FLAT9), 13);
    CHECK_EQ(fm1_shed_tension_semitones(FM1_SHED_TENSION_SHARP9), 15);
    CHECK_EQ(fm1_shed_tension_semitones(FM1_SHED_TENSION_SHARP11), 18);
    CHECK_EQ(fm1_shed_tension_semitones(FM1_SHED_TENSION_13), 21);
    CHECK_EQ(fm1_shed_tension_semitones(FM1_SHED_TENSION_FLAT13), 20);
    CHECK_EQ(fm1_shed_tension_semitones((enum fm1_shed_tension)99), 0);

    CHECK_EQ(fm1_shed_tension_pool_size(0), 2U);
    CHECK_EQ(fm1_shed_tension_pool_size(1), 4U);
    CHECK_EQ(fm1_shed_tension_pool_size(2), (unsigned)FM1_SHED_TENSION_COUNT);
    for (i = 0; i < fm1_shed_tension_pool_size(0); i++) {
        enum fm1_shed_tension t = fm1_shed_tension_pool_at(0, i);
        CHECK(t == FM1_SHED_TENSION_9 || t == FM1_SHED_TENSION_13); /* the plain ones first */
    }
    /* Every tension is reachable at the top level. */
    for (i = 0; i < FM1_SHED_TENSION_COUNT; i++) {
        unsigned j, found = 0;
        for (j = 0; j < fm1_shed_tension_pool_size(2); j++)
            if (fm1_shed_tension_pool_at(2, j) == (enum fm1_shed_tension)i)
                found = 1;
        CHECK(found);
    }
}

static void test_scale_pool(void)
{
    unsigned i;
    CHECK_EQ(fm1_shed_scale_pool_size(0), 7U);  /* modes of the major scale */
    CHECK_EQ(fm1_shed_scale_pool_size(1), 12U); /* + melodic minor family */
    CHECK_EQ(fm1_shed_scale_pool_size(2), (unsigned)FM1_SHED_SCALE_COUNT);
    for (i = 0; i < FM1_SHED_SCALE_COUNT; i++) {
        unsigned j, found = 0;
        for (j = 0; j < fm1_shed_scale_pool_size(2); j++)
            if (fm1_shed_scale_pool_at(2, j) == (enum fm1_shed_scale_type)i)
                found = 1;
        CHECK(found);
    }
}

static void test_question_interval(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;

    fm1_shed_rng_seed(&rng, 11);
    for (i = 0; i < 100; i++) { /* level 0: ascending, within the octave */
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_INTERVAL_MELODIC, 0, 0, &rng));
        CHECK(q.interval >= 1 && q.interval <= 12);
    }
    {
        int saw_negative = 0;
        for (i = 0; i < 200; i++) { /* level 1: both directions, still within the octave */
            CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_INTERVAL_MELODIC, 0, 1, &rng));
            CHECK(q.interval != 0 && q.interval >= -12 && q.interval <= 12);
            if (q.interval < 0)
                saw_negative = 1;
        }
        CHECK(saw_negative);
    }
    {
        int saw_compound = 0;
        for (i = 0; i < 300; i++) { /* level 2: compound intervals (9th/11th/13th territory) */
            CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_INTERVAL_MELODIC, 0, 2, &rng));
            CHECK(q.interval >= -24 && q.interval <= 24 && q.interval != 0);
            if (q.interval > 12 || q.interval < -12)
                saw_compound = 1;
        }
        CHECK(saw_compound);
    }
    /* Harmonic intervals are always "upward" (the second note is the upper one). */
    for (i = 0; i < 200; i++) {
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_INTERVAL_HARMONIC, 3, 2, &rng));
        CHECK(q.interval >= 1 && q.interval <= 24);
    }
}

static void test_check_interval_note(void)
{
    struct fm1_shed_question q;
    q.type = FM1_SHED_EX_INTERVAL_MELODIC;
    q.key = 2; /* D */
    q.interval = 14; /* a 9th, not a 2nd */

    CHECK_EQ(fm1_shed_interval_reference_note(2), 62); /* 60 + D(2) */
    CHECK_EQ(fm1_shed_question_check_interval_note(&q, 62 + 14), 1);
    /* The exact octave matters here, unlike check_note: a 2nd one octave
     * off from the 9th that was actually asked for must NOT count. */
    CHECK_EQ(fm1_shed_question_check_interval_note(&q, 62 + 2), 0);
    CHECK_EQ(fm1_shed_question_check_interval_note(&q, 62 + 14 + 12), 0);

    {
        struct fm1_shed_question wrong_type = q;
        wrong_type.type = FM1_SHED_EX_DEGREE;
        CHECK_EQ(fm1_shed_question_check_interval_note(&wrong_type, 76), -1);
    }
    CHECK_EQ(fm1_shed_question_check_interval_note(NULL, 76), -1);
}

static void test_question_tension(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;
    fm1_shed_rng_seed(&rng, 55);
    for (i = 0; i < 100; i++) {
        unsigned level = (unsigned)(i % 3), j, found = 0;
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_TENSION, 5, level, &rng));
        CHECK_EQ(q.type, FM1_SHED_EX_TENSION);
        CHECK_EQ(q.chord, FM1_SHED_CHORD_DOM7);
        CHECK_EQ(q.chord_root, 5);
        for (j = 0; j < fm1_shed_tension_pool_size(level); j++)
            if (fm1_shed_tension_pool_at(level, j) == q.tension)
                found = 1;
        CHECK(found);
    }
    CHECK_EQ(fm1_shed_question_check_tension(&q, q.tension), 1);
    CHECK_EQ(fm1_shed_question_check_tension(&q, FM1_SHED_TENSION_COUNT), 0);
    CHECK_EQ(fm1_shed_question_check_tension(NULL, q.tension), -1);
}

static void test_question_scale(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;
    fm1_shed_rng_seed(&rng, 56);
    for (i = 0; i < 100; i++) {
        unsigned level = (unsigned)(i % 3), j, found = 0;
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_SCALE, 1, level, &rng));
        CHECK_EQ(q.type, FM1_SHED_EX_SCALE);
        for (j = 0; j < fm1_shed_scale_pool_size(level); j++)
            if (fm1_shed_scale_pool_at(level, j) == q.scale)
                found = 1;
        CHECK(found);
    }
    CHECK_EQ(fm1_shed_question_check_scale(&q, q.scale), 1);
    CHECK_EQ(fm1_shed_question_check_scale(NULL, q.scale), -1);
}

static void test_question_arpeggio(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;

    fm1_shed_rng_seed(&rng, 21);
    for (i = 0; i < 100; i++) {
        const struct fm1_shed_chord *c;
        unsigned j;
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_ARPEGGIO, 4, 0, &rng));
        c = fm1_shed_chord(q.chord);
        CHECK(c != NULL);
        CHECK_EQ(q.sequence_count, c->count);
        /* Level 0: root position, ascending from the root. */
        for (j = 0; j < c->count; j++)
            CHECK_EQ(q.sequence[j], (uint8_t)fm1_shed_pitch_class(q.chord_root + c->intervals[j]));
    }
    {
        int saw_inversion = 0;
        for (i = 0; i < 100; i++) {
            CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_ARPEGGIO, 4, 1, &rng));
            if (q.sequence[0] != (uint8_t)fm1_shed_pitch_class(q.chord_root))
                saw_inversion = 1;
        }
        CHECK(saw_inversion); /* level >= 1 sometimes starts from the 3rd/5th/7th */
    }
}

static void test_question_bass_movement(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;
    fm1_shed_rng_seed(&rng, 33);
    for (i = 0; i < 100; i++) {
        unsigned level = (unsigned)(i % 3), j;
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_BASS_MOVEMENT, 7, level, &rng));
        CHECK(q.sequence_count == 3 || q.sequence_count == 4);
        if (level == 0)
            CHECK_EQ(q.sequence_count, 3U); /* the level-0 pool is only ii-V-I */
        for (j = 0; j < q.sequence_count; j++) {
            CHECK(q.sequence[j] < 12);
            CHECK(fm1_shed_chord(q.sequence_chord[j]) != NULL);
        }
    }
}

static void test_question_melodic_dictation(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    int i;

    fm1_shed_rng_seed(&rng, 44);
    for (i = 0; i < 100; i++) {
        unsigned j;
        CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_MELODIC_DICTATION, 0, 0, &rng));
        CHECK(q.sequence_count >= 3 && q.sequence_count <= 4);
        for (j = 0; j < q.sequence_count; j++)
            CHECK(in_set(q.sequence[j], (const uint8_t[]){0, 2, 4, 5, 7, 9, 11}, 7)); /* diatonic only */
    }
    {
        int saw_chromatic = 0;
        for (i = 0; i < 300; i++) {
            unsigned j;
            static const uint8_t diatonic[7] = {0, 2, 4, 5, 7, 9, 11};
            CHECK(fm1_shed_question_generate(&q, FM1_SHED_EX_MELODIC_DICTATION, 0, 2, &rng));
            CHECK(q.sequence_count >= 5 && q.sequence_count <= 8);
            for (j = 0; j < q.sequence_count; j++)
                if (!in_set(q.sequence[j], diatonic, 7))
                    saw_chromatic = 1;
        }
        CHECK(saw_chromatic); /* level 2 allows chromatic passing tones */
    }
}

static void test_sequence_answer(void)
{
    struct fm1_shed_question q;
    unsigned progress;

    q.type = FM1_SHED_EX_ARPEGGIO;
    q.sequence_count = 3;
    q.sequence[0] = 0; q.sequence[1] = 4; q.sequence[2] = 7;

    progress = 0;
    CHECK_EQ(fm1_shed_question_check_sequence_note(&q, &progress, 0), 1);
    CHECK_EQ(progress, 1U);
    CHECK(!fm1_shed_question_sequence_done(&q, progress));
    CHECK_EQ(fm1_shed_question_check_sequence_note(&q, &progress, 60), 0); /* wrong note: progress unchanged */
    CHECK_EQ(progress, 1U);
    CHECK_EQ(fm1_shed_question_check_sequence_note(&q, &progress, 4 + 24), 1); /* octave-independent */
    CHECK_EQ(progress, 2U);
    CHECK_EQ(fm1_shed_question_check_sequence_note(&q, &progress, 7), 1);
    CHECK_EQ(progress, 3U);
    CHECK(fm1_shed_question_sequence_done(&q, progress));
    CHECK_EQ(fm1_shed_question_check_sequence_note(&q, &progress, 0), -1); /* already complete */

    {
        struct fm1_shed_question wrong_type = q;
        unsigned p2 = 0;
        wrong_type.type = FM1_SHED_EX_DEGREE;
        CHECK_EQ(fm1_shed_question_check_sequence_note(&wrong_type, &p2, 0), -1);
    }
    CHECK_EQ(fm1_shed_question_check_sequence_note(&q, NULL, 0), -1);
    CHECK(!fm1_shed_question_sequence_done(NULL, 0));
}

static void test_generate_at_pooled(void)
{
    struct fm1_shed_rng rng;
    struct fm1_shed_question q;
    unsigned i;

    fm1_shed_rng_seed(&rng, 5);
    for (i = 0; i < fm1_shed_chord_pool_size(1); i++) {
        CHECK(fm1_shed_question_generate_at(&q, FM1_SHED_EX_CHORD_QUALITY, 0, 1, i, &rng));
        CHECK_EQ(q.chord, fm1_shed_chord_pool_at(1, i));
    }
    /* An out-of-range pool_index clamps rather than reading out of bounds. */
    CHECK(fm1_shed_question_generate_at(&q, FM1_SHED_EX_DEGREE, 0, 0, 999, &rng));
    CHECK_EQ(q.degree, fm1_shed_degree_pool_at(0, fm1_shed_degree_pool_size(0) - 1));
    /* Non-pooled types ignore pool_index and still produce a valid question. */
    CHECK(fm1_shed_question_generate_at(&q, FM1_SHED_EX_MELODIC_DICTATION, 0, 0, 3, &rng));
    CHECK(q.sequence_count >= 3);
}

static void test_vamp(void)
{
    unsigned v;
    CHECK(fm1_shed_vamp(FM1_SHED_VAMP_COUNT) == NULL);
    CHECK(fm1_shed_vamp(999) == NULL);
    for (v = 0; v < FM1_SHED_VAMP_COUNT; v++) {
        const struct fm1_shed_vamp *vamp = fm1_shed_vamp(v);
        unsigned i;
        CHECK(vamp != NULL);
        if (!vamp)
            continue;
        CHECK(vamp->name != NULL && vamp->name[0] != 0);
        CHECK(vamp->count >= 1 && vamp->count <= 12); /* the blues vamp is the longest, 12 bars */
        for (i = 0; i < vamp->count; i++) {
            const struct fm1_shed_vamp_step *s = &vamp->steps[i];
            /* Every step's (chord, context) must resolve to a real scale --
             * this is the check that would have caught a typo pairing a
             * chord with a context theory.c has no row for. */
            const struct fm1_shed_chord_scale *row = fm1_shed_chord_scale_lookup(s->chord, s->context);
            CHECK(row != NULL);
            if (row)
                CHECK(fm1_shed_scale(row->scale) != NULL);
            CHECK(fm1_shed_chord(s->chord) != NULL);
            CHECK(s->offset > -12 && s->offset < 12);
            CHECK(s->duration_ms >= 500 && s->duration_ms <= 3000);
        }
    }
}

void test_exercises(void)
{
    test_rng();
    test_next_key();
    test_chord_pool();
    test_degree_pool();
    test_tension();
    test_scale_pool();
    test_question_degree();
    test_question_chord_quality();
    test_question_interval();
    test_check_interval_note();
    test_question_tension();
    test_question_scale();
    test_question_arpeggio();
    test_question_bass_movement();
    test_question_melodic_dictation();
    test_sequence_answer();
    test_generate_at_pooled();
    test_vamp();
    test_generate_rejects_unimplemented_type();
    test_check_note();
    test_check_chord();
}

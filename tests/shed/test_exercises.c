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

void test_exercises(void)
{
    test_rng();
    test_next_key();
    test_chord_pool();
    test_question_degree();
    test_question_chord_quality();
    test_generate_rejects_unimplemented_type();
    test_check_note();
    test_check_chord();
}

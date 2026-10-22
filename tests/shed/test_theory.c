/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "test_support.h"
#include "theory.h"
#include <string.h>

/* Expected data transcribed independently from SHED_SPEC.md section 4, so
 * this test actually cross-checks theory.c against the spec rather than
 * against itself. */

struct expected_chord {
    enum fm1_shed_chord_type type;
    const char *symbol;
    uint8_t intervals[FM1_SHED_CHORD_MAX_NOTES];
    uint8_t count;
};

static const struct expected_chord expected_chords[] = {
    { FM1_SHED_CHORD_MAJ7,         "maj7",    {0, 4, 7, 11},          4 },
    { FM1_SHED_CHORD_SIX_NINE,     "6/9",     {0, 4, 7, 9, 14},       5 },
    { FM1_SHED_CHORD_MIN7,         "m7",      {0, 3, 7, 10},          4 },
    { FM1_SHED_CHORD_MIN6,         "m6",      {0, 3, 7, 9},           4 },
    { FM1_SHED_CHORD_MIN_MAJ7,     "m(maj7)", {0, 3, 7, 11},          4 },
    { FM1_SHED_CHORD_DOM7,         "7",       {0, 4, 7, 10},          4 },
    { FM1_SHED_CHORD_DOM7_SUS4,    "7sus4",   {0, 5, 7, 10},          4 },
    { FM1_SHED_CHORD_MIN7_FLAT5,   "m7♭5",    {0, 3, 6, 10},          4 },
    { FM1_SHED_CHORD_DIM7,         "dim7",    {0, 3, 6, 9},           4 },
    { FM1_SHED_CHORD_MAJ7_SHARP11, "maj7♯11", {0, 4, 7, 11, 18},      5 },
    { FM1_SHED_CHORD_DOM7_FLAT9,   "7♭9",     {0, 4, 7, 10, 13},      5 },
    { FM1_SHED_CHORD_DOM7_SHARP11, "7♯11",    {0, 4, 7, 10, 18},      5 },
    { FM1_SHED_CHORD_DOM7_ALT,     "7alt",    {0, 4, 10, 13, 15, 20}, 6 },
};

struct expected_scale {
    enum fm1_shed_scale_type type;
    uint8_t intervals[FM1_SHED_SCALE_MAX_NOTES];
    uint8_t count;
};

static const struct expected_scale expected_scales[] = {
    { FM1_SHED_SCALE_IONIAN,            {0, 2, 4, 5, 7, 9, 11},     7 },
    { FM1_SHED_SCALE_DORIAN,            {0, 2, 3, 5, 7, 9, 10},     7 },
    { FM1_SHED_SCALE_PHRYGIAN,          {0, 1, 3, 5, 7, 8, 10},     7 },
    { FM1_SHED_SCALE_LYDIAN,            {0, 2, 4, 6, 7, 9, 11},     7 },
    { FM1_SHED_SCALE_MIXOLYDIAN,        {0, 2, 4, 5, 7, 9, 10},     7 },
    { FM1_SHED_SCALE_AEOLIAN,           {0, 2, 3, 5, 7, 8, 10},     7 },
    { FM1_SHED_SCALE_LOCRIAN,           {0, 1, 3, 5, 6, 8, 10},     7 },
    { FM1_SHED_SCALE_MELODIC_MINOR,     {0, 2, 3, 5, 7, 9, 11},     7 },
    { FM1_SHED_SCALE_LYDIAN_DOMINANT,   {0, 2, 4, 6, 7, 9, 10},     7 },
    { FM1_SHED_SCALE_LOCRIAN_NAT2,      {0, 2, 3, 5, 6, 8, 10},     7 },
    { FM1_SHED_SCALE_ALTERED,           {0, 1, 3, 4, 6, 8, 10},     7 },
    { FM1_SHED_SCALE_DIM_WHOLE_HALF,    {0, 2, 3, 5, 6, 8, 9, 11},  8 },
    { FM1_SHED_SCALE_DIM_HALF_WHOLE,    {0, 1, 3, 4, 6, 7, 9, 10},  8 },
    { FM1_SHED_SCALE_WHOLE_TONE,        {0, 2, 4, 6, 8, 10},        6 },
    { FM1_SHED_SCALE_BEBOP_DOMINANT,    {0, 2, 4, 5, 7, 9, 10, 11}, 8 },
    { FM1_SHED_SCALE_PHRYGIAN_DOMINANT, {0, 1, 4, 5, 7, 8, 10},     7 },
};

struct expected_row {
    enum fm1_shed_chord_type chord;
    enum fm1_shed_context context;
    enum fm1_shed_scale_type scale;
    enum fm1_shed_scale_type alt_scale; /* FM1_SHED_SCALE_COUNT if none */
    uint8_t avoid[2];                   /* 0xff = unused */
};

static const struct expected_row expected_rows[] = {
    { FM1_SHED_CHORD_MAJ7,         FM1_SHED_CONTEXT_I,       FM1_SHED_SCALE_IONIAN,
      FM1_SHED_SCALE_LYDIAN,           {5, 0xff} },
    { FM1_SHED_CHORD_MAJ7,         FM1_SHED_CONTEXT_IV,      FM1_SHED_SCALE_LYDIAN,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
    { FM1_SHED_CHORD_MIN7,         FM1_SHED_CONTEXT_II,      FM1_SHED_SCALE_DORIAN,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
    { FM1_SHED_CHORD_MIN7,         FM1_SHED_CONTEXT_III,     FM1_SHED_SCALE_PHRYGIAN,
      FM1_SHED_SCALE_COUNT,            {1, 8} },
    { FM1_SHED_CHORD_MIN7,         FM1_SHED_CONTEXT_VI,      FM1_SHED_SCALE_AEOLIAN,
      FM1_SHED_SCALE_COUNT,            {8, 0xff} },
    { FM1_SHED_CHORD_DOM7,         FM1_SHED_CONTEXT_V_MAJOR, FM1_SHED_SCALE_MIXOLYDIAN,
      FM1_SHED_SCALE_BEBOP_DOMINANT,   {5, 0xff} },
    { FM1_SHED_CHORD_DOM7_SHARP11, FM1_SHED_CONTEXT_NONE,    FM1_SHED_SCALE_LYDIAN_DOMINANT,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
    { FM1_SHED_CHORD_DOM7_ALT,     FM1_SHED_CONTEXT_NONE,    FM1_SHED_SCALE_ALTERED,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
    { FM1_SHED_CHORD_DOM7_FLAT9,   FM1_SHED_CONTEXT_V_MINOR, FM1_SHED_SCALE_PHRYGIAN_DOMINANT,
      FM1_SHED_SCALE_DIM_HALF_WHOLE,   {0xff, 0xff} },
    { FM1_SHED_CHORD_MIN7_FLAT5,   FM1_SHED_CONTEXT_NONE,    FM1_SHED_SCALE_LOCRIAN,
      FM1_SHED_SCALE_LOCRIAN_NAT2,     {1, 0xff} },
    { FM1_SHED_CHORD_DIM7,         FM1_SHED_CONTEXT_NONE,    FM1_SHED_SCALE_DIM_WHOLE_HALF,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
    { FM1_SHED_CHORD_MIN_MAJ7,     FM1_SHED_CONTEXT_NONE,    FM1_SHED_SCALE_MELODIC_MINOR,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
    { FM1_SHED_CHORD_MIN6,         FM1_SHED_CONTEXT_NONE,    FM1_SHED_SCALE_MELODIC_MINOR,
      FM1_SHED_SCALE_COUNT,            {0xff, 0xff} },
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static void test_pitch_class(void)
{
    CHECK_EQ(fm1_shed_pitch_class(0), 0);
    CHECK_EQ(fm1_shed_pitch_class(11), 11);
    CHECK_EQ(fm1_shed_pitch_class(12), 0);
    CHECK_EQ(fm1_shed_pitch_class(25), 1);
    CHECK_EQ(fm1_shed_pitch_class(-1), 11);
    CHECK_EQ(fm1_shed_pitch_class(-12), 0);
    CHECK_EQ(fm1_shed_pitch_class(-13), 11);
}

static void test_chords(void)
{
    unsigned i, j;
    CHECK_EQ(COUNT_OF(expected_chords), FM1_SHED_CHORD_COUNT);
    for (i = 0; i < COUNT_OF(expected_chords); i++) {
        const struct expected_chord *e = &expected_chords[i];
        const struct fm1_shed_chord *c = fm1_shed_chord(e->type);
        CHECK(c != NULL);
        if (!c)
            continue;
        CHECK(c->symbol != NULL);
        CHECK(strcmp(c->symbol, e->symbol) == 0);
        CHECK_EQ(c->count, e->count);
        for (j = 0; j < e->count; j++)
            CHECK_EQ(c->intervals[j], e->intervals[j]);
    }
    CHECK(fm1_shed_chord((enum fm1_shed_chord_type)FM1_SHED_CHORD_COUNT) == NULL);
    CHECK(fm1_shed_chord((enum fm1_shed_chord_type)999) == NULL);
}

static void test_scales(void)
{
    unsigned i, j;
    CHECK_EQ(COUNT_OF(expected_scales), FM1_SHED_SCALE_COUNT);
    for (i = 0; i < COUNT_OF(expected_scales); i++) {
        const struct expected_scale *e = &expected_scales[i];
        const struct fm1_shed_scale *s = fm1_shed_scale(e->type);
        CHECK(s != NULL);
        if (!s)
            continue;
        CHECK(s->name != NULL);
        CHECK_EQ(s->count, e->count);
        for (j = 0; j < e->count; j++)
            CHECK_EQ(s->intervals[j], e->intervals[j]);
    }
    CHECK(fm1_shed_scale((enum fm1_shed_scale_type)FM1_SHED_SCALE_COUNT) == NULL);
}

static void test_chord_scale_table(void)
{
    unsigned i;
    CHECK_EQ(COUNT_OF(expected_rows), FM1_SHED_CHORD_SCALE_ROWS);
    for (i = 0; i < COUNT_OF(expected_rows); i++) {
        const struct expected_row *e = &expected_rows[i];
        const struct fm1_shed_chord_scale *row = fm1_shed_chord_scale_lookup(e->chord, e->context);
        CHECK(row != NULL);
        if (!row)
            continue;
        CHECK_EQ(row->scale, e->scale);
        CHECK_EQ(row->alt_scale, e->alt_scale);
        CHECK_EQ(row->avoid[0], e->avoid[0]);
        CHECK_EQ(row->avoid[1], e->avoid[1]);
    }

    /* A context this chord doesn't have a specific row for falls back to
     * the chord's FM1_SHED_CONTEXT_NONE row (m7♭5 has no "context II" row,
     * only its generic Locrian row). */
    {
        const struct fm1_shed_chord_scale *row =
            fm1_shed_chord_scale_lookup(FM1_SHED_CHORD_MIN7_FLAT5, FM1_SHED_CONTEXT_II);
        CHECK(row != NULL);
        if (row)
            CHECK_EQ(row->scale, FM1_SHED_SCALE_LOCRIAN);
    }
    /* m7 has no generic row, only I/II/III/VI-specific ones: an unlisted
     * context (e.g. V) must fail rather than silently pick one. */
    CHECK(fm1_shed_chord_scale_lookup(FM1_SHED_CHORD_MIN7, FM1_SHED_CONTEXT_V_MAJOR) == NULL);
}

static void test_pitch_class_builders(void)
{
    uint8_t out[FM1_SHED_SCALE_MAX_NOTES];
    unsigned n;

    n = fm1_shed_chord_pitch_classes(FM1_SHED_CHORD_MAJ7, 0, out);
    CHECK_EQ(n, 4U);
    CHECK_EQ(out[0], 0); CHECK_EQ(out[1], 4); CHECK_EQ(out[2], 7); CHECK_EQ(out[3], 11);

    /* Rooted on B♭ (10): intervals wrap past the octave into the next one. */
    n = fm1_shed_chord_pitch_classes(FM1_SHED_CHORD_MAJ7, 10, out);
    CHECK_EQ(n, 4U);
    CHECK_EQ(out[0], 10); CHECK_EQ(out[1], 2); CHECK_EQ(out[2], 5); CHECK_EQ(out[3], 9);

    /* 6/9's 9th (interval 14) reduces to pitch class 2 when rooted on C. */
    n = fm1_shed_chord_pitch_classes(FM1_SHED_CHORD_SIX_NINE, 0, out);
    CHECK_EQ(n, 5U);
    CHECK_EQ(out[4], 2);

    n = fm1_shed_scale_pitch_classes(FM1_SHED_SCALE_DORIAN, 2, out); /* D Dorian */
    CHECK_EQ(n, 7U);
    CHECK_EQ(out[0], 2); CHECK_EQ(out[1], 4); CHECK_EQ(out[2], 5);
    CHECK_EQ(out[3], 7); CHECK_EQ(out[4], 9); CHECK_EQ(out[5], 11); CHECK_EQ(out[6], 0);

    CHECK_EQ(fm1_shed_chord_pitch_classes((enum fm1_shed_chord_type)999, 0, out), 0U);
    CHECK_EQ(fm1_shed_scale_pitch_classes((enum fm1_shed_scale_type)999, 0, out), 0U);
}

void test_theory(void)
{
    test_pitch_class();
    test_chords();
    test_scales();
    test_chord_scale_table();
    test_pitch_class_builders();
}

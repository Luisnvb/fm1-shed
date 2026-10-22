/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "theory.h"
#include <stddef.h>

/* Chord table, SHED_SPEC.md section 4 "Tipos de acorde". Indexed by
 * fm1_shed_chord_type so a bad enum value can never desync from its row. */
static const struct fm1_shed_chord chords[FM1_SHED_CHORD_COUNT] = {
    [FM1_SHED_CHORD_MAJ7]         = { "maj7",     {0, 4, 7, 11},         4 },
    [FM1_SHED_CHORD_SIX_NINE]     = { "6/9",      {0, 4, 7, 9, 14},      5 },
    [FM1_SHED_CHORD_MIN7]         = { "m7",       {0, 3, 7, 10},         4 },
    [FM1_SHED_CHORD_MIN6]         = { "m6",       {0, 3, 7, 9},          4 },
    [FM1_SHED_CHORD_MIN_MAJ7]     = { "m(maj7)",  {0, 3, 7, 11},         4 },
    [FM1_SHED_CHORD_DOM7]         = { "7",        {0, 4, 7, 10},         4 },
    [FM1_SHED_CHORD_DOM7_SUS4]    = { "7sus4",    {0, 5, 7, 10},         4 },
    [FM1_SHED_CHORD_MIN7_FLAT5]   = { "m7♭5",     {0, 3, 6, 10},         4 },
    [FM1_SHED_CHORD_DIM7]         = { "dim7",     {0, 3, 6, 9},          4 },
    [FM1_SHED_CHORD_MAJ7_SHARP11] = { "maj7♯11",  {0, 4, 7, 11, 18},     5 },
    [FM1_SHED_CHORD_DOM7_FLAT9]   = { "7♭9",      {0, 4, 7, 10, 13},     5 },
    [FM1_SHED_CHORD_DOM7_SHARP11] = { "7♯11",     {0, 4, 7, 10, 18},     5 },
    /* Fifth omitted in favor of the altered tensions, as the spec notes. */
    [FM1_SHED_CHORD_DOM7_ALT]     = { "7alt",     {0, 4, 10, 13, 15, 20}, 6 },
};

/* Scale table, SHED_SPEC.md section 4 "Escalas (intervalos)". */
static const struct fm1_shed_scale scales[FM1_SHED_SCALE_COUNT] = {
    [FM1_SHED_SCALE_IONIAN]            = { "Jónica",                    {0, 2, 4, 5, 7, 9, 11},     7 },
    [FM1_SHED_SCALE_DORIAN]            = { "Dórica",                    {0, 2, 3, 5, 7, 9, 10},     7 },
    [FM1_SHED_SCALE_PHRYGIAN]          = { "Frigia",                    {0, 1, 3, 5, 7, 8, 10},     7 },
    [FM1_SHED_SCALE_LYDIAN]            = { "Lidia",                     {0, 2, 4, 6, 7, 9, 11},     7 },
    [FM1_SHED_SCALE_MIXOLYDIAN]        = { "Mixolidia",                 {0, 2, 4, 5, 7, 9, 10},     7 },
    [FM1_SHED_SCALE_AEOLIAN]           = { "Eólica",                    {0, 2, 3, 5, 7, 8, 10},     7 },
    [FM1_SHED_SCALE_LOCRIAN]           = { "Locria",                    {0, 1, 3, 5, 6, 8, 10},     7 },
    [FM1_SHED_SCALE_MELODIC_MINOR]     = { "Menor melódica",            {0, 2, 3, 5, 7, 9, 11},     7 },
    [FM1_SHED_SCALE_LYDIAN_DOMINANT]   = { "Lidia dominante",           {0, 2, 4, 6, 7, 9, 10},     7 },
    [FM1_SHED_SCALE_LOCRIAN_NAT2]      = { "Locria ♮2",                 {0, 2, 3, 5, 6, 8, 10},     7 },
    [FM1_SHED_SCALE_ALTERED]           = { "Alterada",                  {0, 1, 3, 4, 6, 8, 10},     7 },
    [FM1_SHED_SCALE_DIM_WHOLE_HALF]    = { "Disminuida tono-semitono",  {0, 2, 3, 5, 6, 8, 9, 11},  8 },
    [FM1_SHED_SCALE_DIM_HALF_WHOLE]    = { "Disminuida semitono-tono",  {0, 1, 3, 4, 6, 7, 9, 10},  8 },
    [FM1_SHED_SCALE_WHOLE_TONE]        = { "Tonos enteros",             {0, 2, 4, 6, 8, 10},        6 },
    [FM1_SHED_SCALE_BEBOP_DOMINANT]    = { "Bebop dominante",           {0, 2, 4, 5, 7, 9, 10, 11}, 8 },
    [FM1_SHED_SCALE_PHRYGIAN_DOMINANT] = { "Frigia dominante",          {0, 1, 4, 5, 7, 8, 10},     7 },
};

/* Chord -> scale table, SHED_SPEC.md section 4 "Acorde -> escala".
 * 0xff marks an unused `avoid` slot. */
static const struct fm1_shed_chord_scale chord_scale[FM1_SHED_CHORD_SCALE_ROWS] = {
    /* maj7 (I) */
    { FM1_SHED_CHORD_MAJ7, FM1_SHED_CONTEXT_I, FM1_SHED_SCALE_IONIAN,
      FM1_SHED_SCALE_LYDIAN, {5, 0xff} },
    /* maj7 (IV) */
    { FM1_SHED_CHORD_MAJ7, FM1_SHED_CONTEXT_IV, FM1_SHED_SCALE_LYDIAN,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
    /* m7 (II) */
    { FM1_SHED_CHORD_MIN7, FM1_SHED_CONTEXT_II, FM1_SHED_SCALE_DORIAN,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
    /* m7 (III): avoid ♭2, ♭6 */
    { FM1_SHED_CHORD_MIN7, FM1_SHED_CONTEXT_III, FM1_SHED_SCALE_PHRYGIAN,
      FM1_SHED_SCALE_COUNT, {1, 8} },
    /* m7 (VI): avoid ♭6 */
    { FM1_SHED_CHORD_MIN7, FM1_SHED_CONTEXT_VI, FM1_SHED_SCALE_AEOLIAN,
      FM1_SHED_SCALE_COUNT, {8, 0xff} },
    /* 7 (V -> mayor): avoid 4 */
    { FM1_SHED_CHORD_DOM7, FM1_SHED_CONTEXT_V_MAJOR, FM1_SHED_SCALE_MIXOLYDIAN,
      FM1_SHED_SCALE_BEBOP_DOMINANT, {5, 0xff} },
    /* 7♯11 */
    { FM1_SHED_CHORD_DOM7_SHARP11, FM1_SHED_CONTEXT_NONE, FM1_SHED_SCALE_LYDIAN_DOMINANT,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
    /* 7alt */
    { FM1_SHED_CHORD_DOM7_ALT, FM1_SHED_CONTEXT_NONE, FM1_SHED_SCALE_ALTERED,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
    /* 7♭9 (V -> menor); alt = disminuida semitono-tono */
    { FM1_SHED_CHORD_DOM7_FLAT9, FM1_SHED_CONTEXT_V_MINOR, FM1_SHED_SCALE_PHRYGIAN_DOMINANT,
      FM1_SHED_SCALE_DIM_HALF_WHOLE, {0xff, 0xff} },
    /* m7♭5: avoid ♭2 (en locria) */
    { FM1_SHED_CHORD_MIN7_FLAT5, FM1_SHED_CONTEXT_NONE, FM1_SHED_SCALE_LOCRIAN,
      FM1_SHED_SCALE_LOCRIAN_NAT2, {1, 0xff} },
    /* dim7 */
    { FM1_SHED_CHORD_DIM7, FM1_SHED_CONTEXT_NONE, FM1_SHED_SCALE_DIM_WHOLE_HALF,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
    /* m(maj7) */
    { FM1_SHED_CHORD_MIN_MAJ7, FM1_SHED_CONTEXT_NONE, FM1_SHED_SCALE_MELODIC_MINOR,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
    /* m6 */
    { FM1_SHED_CHORD_MIN6, FM1_SHED_CONTEXT_NONE, FM1_SHED_SCALE_MELODIC_MINOR,
      FM1_SHED_SCALE_COUNT, {0xff, 0xff} },
};

int fm1_shed_pitch_class(int note)
{
    int pc = note % 12;
    return pc < 0 ? pc + 12 : pc;
}

const struct fm1_shed_chord *fm1_shed_chord(enum fm1_shed_chord_type type)
{
    return (unsigned)type < FM1_SHED_CHORD_COUNT ? &chords[type] : NULL;
}

const struct fm1_shed_scale *fm1_shed_scale(enum fm1_shed_scale_type type)
{
    return (unsigned)type < FM1_SHED_SCALE_COUNT ? &scales[type] : NULL;
}

const struct fm1_shed_chord_scale *fm1_shed_chord_scale_lookup(
    enum fm1_shed_chord_type type, enum fm1_shed_context context)
{
    const struct fm1_shed_chord_scale *fallback = NULL;
    unsigned i;
    for (i = 0; i < FM1_SHED_CHORD_SCALE_ROWS; i++) {
        if (chord_scale[i].chord != type)
            continue;
        if (chord_scale[i].context == context)
            return &chord_scale[i];
        if (chord_scale[i].context == FM1_SHED_CONTEXT_NONE)
            fallback = &chord_scale[i];
    }
    return fallback;
}

unsigned fm1_shed_chord_pitch_classes(enum fm1_shed_chord_type type, int root,
    uint8_t out[FM1_SHED_CHORD_MAX_NOTES])
{
    const struct fm1_shed_chord *c = fm1_shed_chord(type);
    unsigned i;
    if (!c)
        return 0;
    for (i = 0; i < c->count; i++)
        out[i] = (uint8_t)fm1_shed_pitch_class(root + c->intervals[i]);
    return c->count;
}

unsigned fm1_shed_scale_pitch_classes(enum fm1_shed_scale_type type, int root,
    uint8_t out[FM1_SHED_SCALE_MAX_NOTES])
{
    const struct fm1_shed_scale *s = fm1_shed_scale(type);
    unsigned i;
    if (!s)
        return 0;
    for (i = 0; i < s->count; i++)
        out[i] = (uint8_t)fm1_shed_pitch_class(root + s->intervals[i]);
    return s->count;
}

/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_THEORY_H
#define FM1_SHED_THEORY_H

/* Jazz theory core for Shed (see SHED_SPEC.md section 4). Pure C11, no
 * FM-1 SDK dependencies: this file and theory.c must build and run on the
 * host as well as in the FM-1 app. Pitch classes are 0..11 with C = 0.
 * Chord and scale intervals are semitones from the root and are left
 * un-reduced above the octave (e.g. 14 for a natural 9th) so a caller can
 * tell a 9th from a 2nd; fm1_shed_pitch_class() reduces them back to 0..11
 * when a caller only cares about pitch class. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM1_SHED_CHORD_MAX_NOTES 6U
#define FM1_SHED_SCALE_MAX_NOTES 8U
#define FM1_SHED_CHORD_SCALE_ROWS 13U

enum fm1_shed_chord_type {
    FM1_SHED_CHORD_MAJ7 = 0,
    FM1_SHED_CHORD_SIX_NINE,
    FM1_SHED_CHORD_MIN7,
    FM1_SHED_CHORD_MIN6,
    FM1_SHED_CHORD_MIN_MAJ7,
    FM1_SHED_CHORD_DOM7,
    FM1_SHED_CHORD_DOM7_SUS4,
    FM1_SHED_CHORD_MIN7_FLAT5,
    FM1_SHED_CHORD_DIM7,
    FM1_SHED_CHORD_MAJ7_SHARP11,
    FM1_SHED_CHORD_DOM7_FLAT9,
    FM1_SHED_CHORD_DOM7_SHARP11,
    FM1_SHED_CHORD_DOM7_ALT,
    FM1_SHED_CHORD_COUNT
};

enum fm1_shed_scale_type {
    FM1_SHED_SCALE_IONIAN = 0,
    FM1_SHED_SCALE_DORIAN,
    FM1_SHED_SCALE_PHRYGIAN,
    FM1_SHED_SCALE_LYDIAN,
    FM1_SHED_SCALE_MIXOLYDIAN,
    FM1_SHED_SCALE_AEOLIAN,
    FM1_SHED_SCALE_LOCRIAN,
    FM1_SHED_SCALE_MELODIC_MINOR,
    FM1_SHED_SCALE_LYDIAN_DOMINANT,
    FM1_SHED_SCALE_LOCRIAN_NAT2,
    FM1_SHED_SCALE_ALTERED,
    FM1_SHED_SCALE_DIM_WHOLE_HALF,
    FM1_SHED_SCALE_DIM_HALF_WHOLE,
    FM1_SHED_SCALE_WHOLE_TONE,
    FM1_SHED_SCALE_BEBOP_DOMINANT,
    FM1_SHED_SCALE_PHRYGIAN_DOMINANT,
    FM1_SHED_SCALE_COUNT
};

/* Harmonic function, used only to disambiguate a chord -> scale mapping
 * that depends on context and not on chord quality alone (maj7 as I vs.
 * IV; m7 as II, III or VI; 7 resolving to a major or a minor tonic).
 * FM1_SHED_CONTEXT_NONE means the chord quality alone picks the row. */
enum fm1_shed_context {
    FM1_SHED_CONTEXT_NONE = 0,
    FM1_SHED_CONTEXT_I,
    FM1_SHED_CONTEXT_II,
    FM1_SHED_CONTEXT_III,
    FM1_SHED_CONTEXT_IV,
    FM1_SHED_CONTEXT_V_MAJOR,  /* dominant resolving to a major tonic */
    FM1_SHED_CONTEXT_V_MINOR,  /* dominant resolving to a minor tonic */
    FM1_SHED_CONTEXT_VI,
    FM1_SHED_CONTEXT_COUNT
};

struct fm1_shed_chord {
    const char *symbol;                        /* UTF-8, e.g. "maj7", "7\xe2\x99\xad9" */
    uint8_t intervals[FM1_SHED_CHORD_MAX_NOTES];
    uint8_t count;
};

struct fm1_shed_scale {
    const char *name;                           /* UTF-8 */
    uint8_t intervals[FM1_SHED_SCALE_MAX_NOTES];
    uint8_t count;
};

/* One row of the chord -> scale table (SHED_SPEC.md section 4). alt_scale
 * is FM1_SHED_SCALE_COUNT when the row has no alternative. avoid holds up
 * to two semitone offsets from the chord root (0xff = unused slot). */
struct fm1_shed_chord_scale {
    enum fm1_shed_chord_type chord;
    enum fm1_shed_context context;
    enum fm1_shed_scale_type scale;
    enum fm1_shed_scale_type alt_scale;
    uint8_t avoid[2];
};

/* Reduces any signed semitone offset to a pitch class in 0..11. */
int fm1_shed_pitch_class(int note);

/* Table accessors. NULL (chord/scale) or 0 (lookup) on an out-of-range type. */
const struct fm1_shed_chord *fm1_shed_chord(enum fm1_shed_chord_type type);
const struct fm1_shed_scale *fm1_shed_scale(enum fm1_shed_scale_type type);

/* Finds the chord -> scale row for (type, context). Falls back to the
 * FM1_SHED_CONTEXT_NONE row for that chord type if a context-specific row
 * doesn't exist. Returns NULL if `type` has no row at all. */
const struct fm1_shed_chord_scale *fm1_shed_chord_scale_lookup(
    enum fm1_shed_chord_type type, enum fm1_shed_context context);

/* Writes the pitch classes (0..11) of `type` rooted at `root` to `out`
 * (capacity FM1_SHED_CHORD_MAX_NOTES) and returns how many were written. */
unsigned fm1_shed_chord_pitch_classes(enum fm1_shed_chord_type type, int root,
    uint8_t out[FM1_SHED_CHORD_MAX_NOTES]);

/* Same for a scale (capacity FM1_SHED_SCALE_MAX_NOTES). */
unsigned fm1_shed_scale_pitch_classes(enum fm1_shed_scale_type type, int root,
    uint8_t out[FM1_SHED_SCALE_MAX_NOTES]);

#ifdef __cplusplus
}
#endif
#endif

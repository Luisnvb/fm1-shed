/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_UI_H
#define FM1_SHED_UI_H

/* Screens, control reading and key LEDs for Shed (SHED_SPEC.md section 3
 * "ui.h/.c" and section 7 "Pantallas"). Depends on the FM-1 SDK; not
 * host-testable. Owns no exercise logic: shed.c drives a small state
 * machine and tells this module what to show. */

#include "exercises.h"
#include "fm1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* fm1.h: "keys; bits 0..26: F3..G5". Public so shed.c can turn
 * fm1_shed_input's keys_pressed/keys_released bitmasks (vamp mode's
 * polyphonic live playing) into actual MIDI notes itself. */
enum { FM1_SHED_KEY_COUNT = 27, FM1_SHED_KEY_BASE_NOTE = 53 };

/* Panel action buttons while a Shed screen is up. Bits set in
 * fm1_shed_input.action were newly pressed this poll. Meaning depends on
 * shed.c's current state: during an exercise (section 5 "Controles
 * comunes durante un ejercicio") vs. vamp mode (section 6) the same
 * physical button does a related but different thing, noted below. */
enum fm1_shed_action {
    FM1_SHED_ACTION_REPEAT_STIMULUS = 1U << 0, /* PLAY: repeat the question's stimulus */
    FM1_SHED_ACTION_REPEAT_CADENCE  = 1U << 1, /* SEQ:  repeat the cadence / vamp: next vamp */
    FM1_SHED_ACTION_REVEAL          = 1U << 2, /* EDIT: reveal the answer / vamp: cycle the LED view */
    FM1_SHED_ACTION_NEXT            = 1U << 3, /* ARP:  next question / vamp: next key */
    FM1_SHED_ACTION_TOGGLE_TYPE     = 1U << 4, /* ENV:  switch exercise type */
    FM1_SHED_ACTION_CONFIRM         = 1U << 5  /* GLO:  confirm the dial's pick / vamp: hear a phrase (oído) */
};

/* One poll's worth of input, edge-detected against this module's own
 * last-seen controls -- never app/screen.c's previous_buttons/previous_keys,
 * which Shed's active frames bypass entirely (see shed.h). */
struct fm1_shed_input {
    int note;          /* pitch class 0..11 of a newly pressed key, -1 if none */
    int note_absolute; /* that key's actual MIDI note (F3=53..G5=79); -1 if none.
                         * #2/#3 need this -- a 9th and a 2nd share a pitch class. */
    unsigned action;    /* FM1_SHED_ACTION_* bits newly pressed this poll */
    int dial_delta;     /* PRESETS dial (encoder[6]) detents this poll: menu navigation,
                          * or the chord/tension/scale picker, depending on shed.c's state */
    /* Vamp mode's live playing is polyphonic (SHED_SPEC.md section 6: the
     * user improvises over the loop), unlike every exercise's one-note-at-
     * a-time `note` above -- bit i is key i (0..26, F3..G5), set when that
     * key was newly pressed/released this poll. */
    uint32_t keys_pressed, keys_released;
};

/* Resets this module's edge-detection state to `controls` (whatever is
 * physically held at the moment shed_enter() runs, typically still HOME+SCL)
 * so the first fm1_shed_ui_read() afterwards doesn't treat already-held
 * buttons as a fresh press. Call once from shed_enter(). */
void fm1_shed_ui_reset(const struct fm1_controls *controls);

/* Fills `input` from `controls`. Call once per poll while Shed is active. */
void fm1_shed_ui_read(const struct fm1_controls *controls, struct fm1_shed_input *input);

enum fm1_shed_screen {
    FM1_SHED_SCREEN_MENU,
    FM1_SHED_SCREEN_QUESTION,
    FM1_SHED_SCREEN_FEEDBACK,
    FM1_SHED_SCREEN_SUMMARY,
    FM1_SHED_SCREEN_VAMP /* section 6: free play over a loop. The "oído" variant
                           * reuses QUESTION/FEEDBACK with exercise forced to
                           * TENSION or SCALE -- it's the same question either way. */
};

/* Vamp mode's LED guidance (section 6: "encendido fijo = notas del acorde;
 * parpadeo = tensiones disponibles", plus the "Vistas" list). */
enum fm1_shed_vamp_view {
    FM1_SHED_VAMP_VIEW_ARPEGGIO = 0, /* chord tones solid, the rest of the suggested scale blinking */
    FM1_SHED_VAMP_VIEW_SCALE,        /* the whole suggested scale, solid */
    FM1_SHED_VAMP_VIEW_GUIDE,        /* only the 3rd and 7th, solid */
    FM1_SHED_VAMP_VIEW_NONE,         /* nothing lit -- just listen */
    FM1_SHED_VAMP_VIEW_COUNT
};

/* What a screen shows. shed.c fills in only the fields its current screen
 * uses. ui.c reads `question` only for FM1_SHED_SCREEN_FEEDBACK (never
 * during FM1_SHED_SCREEN_QUESTION -- the point is to hear it, not see it);
 * `chord_pick`/`tension_pick`/`scale_pick` are shown live during a QUESTION
 * of the matching type, to preview the dial's current highlight.
 * `sequence_progress`/`sequence_total` show "n/m" while answering a
 * sequence question (#6/#8/#9). `menu_index` is MENU's current highlight. */
struct fm1_shed_ui_state {
    enum fm1_shed_screen screen;
    int key;          /* current tonic, pitch class 0..11; -1 to hide (MENU) */
    enum fm1_shed_exercise_type exercise; /* MENU/QUESTION/FEEDBACK: which one is selected/active */
    unsigned menu_index;                   /* MENU only */
    unsigned level;                        /* QUESTION/FEEDBACK: current difficulty, 0-based */
    const struct fm1_shed_question *question; /* FEEDBACK only */
    enum fm1_shed_chord_type chord_pick;      /* live dial highlight: CHORD_QUALITY, ARPEGGIO isn't dial-driven */
    enum fm1_shed_tension tension_pick;       /* live dial highlight: TENSION */
    enum fm1_shed_scale_type scale_pick;      /* live dial highlight: SCALE */
    unsigned sequence_progress, sequence_total; /* QUESTION, sequence types only */
    unsigned asked, session_length;           /* QUESTION/FEEDBACK/SUMMARY; session_length 0 hides "n/m" (vamp's oído) */
    unsigned streak, best_streak;
    int correct;      /* FEEDBACK only: 1 right, 0 wrong */
    unsigned percent; /* SUMMARY only */

    /* VAMP only. */
    unsigned vamp_index;                 /* which of fm1_shed_vamp(0..FM1_SHED_VAMP_COUNT-1) */
    enum fm1_shed_chord_type vamp_chord, vamp_next_chord;
    int vamp_root, vamp_next_root;       /* pitch class 0..11 */
    enum fm1_shed_scale_type vamp_scale; /* suggested over vamp_chord */
    enum fm1_shed_vamp_view vamp_view;
};

/* Draws the screen for `state`. Throttles its own redraws against `now`
 * (screen.c's own ~33ms cadence; see app/screen.c groove_screen_control),
 * so callers may call this every poll. */
void fm1_shed_ui_draw(const struct fm1_shed_ui_state *state, uint32_t now);

/* Lights the note-key LEDs (14..40) matching `pitch_class` (0..11) across
 * the whole F3..G5 range, all others off; -1 turns every key LED off. Used
 * to show the correct answer to a degree/interval question on reveal. */
void fm1_shed_ui_show_answer_leds(int pitch_class);

/* Turns off every LED Shed may have lit (buttons and keys, 0..40). Call
 * once from shed_exit(), after fm1_shed_player_release(). */
void fm1_shed_ui_clear_leds(void);

/* Sets the note-key LEDs for vamp mode's current chord/scale/view (section
 * 6). Blinking tensions use `now` for their phase, so call every poll
 * while in FM1_SHED_SCREEN_VAMP, not just when something changes. */
void fm1_shed_ui_show_vamp_leds(enum fm1_shed_vamp_view view, enum fm1_shed_chord_type chord,
    int root, enum fm1_shed_scale_type scale, uint32_t now);

#ifdef __cplusplus
}
#endif
#endif

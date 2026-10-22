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

/* Panel action buttons while a Shed screen is up (SHED_SPEC.md section 5
 * "Controles comunes durante un ejercicio"). Bits set in
 * fm1_shed_input.action were newly pressed this poll. */
enum fm1_shed_action {
    FM1_SHED_ACTION_REPEAT_STIMULUS = 1U << 0, /* PLAY: repeat the question's stimulus */
    FM1_SHED_ACTION_REPEAT_CADENCE  = 1U << 1, /* SEQ:  repeat the cadence */
    FM1_SHED_ACTION_REVEAL          = 1U << 2, /* EDIT: reveal the answer */
    FM1_SHED_ACTION_NEXT            = 1U << 3, /* ARP:  next question / new session */
    FM1_SHED_ACTION_TOGGLE_TYPE     = 1U << 4, /* ENV:  switch exercise type */
    FM1_SHED_ACTION_CONFIRM         = 1U << 5  /* GLO:  confirm the dial's pick */
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
    FM1_SHED_SCREEN_SUMMARY
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
    unsigned asked, session_length;           /* QUESTION/FEEDBACK/SUMMARY */
    unsigned streak, best_streak;
    int correct;      /* FEEDBACK only: 1 right, 0 wrong */
    unsigned percent; /* SUMMARY only */
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

#ifdef __cplusplus
}
#endif
#endif

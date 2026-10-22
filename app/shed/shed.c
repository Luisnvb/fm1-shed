/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "shed.h"
#include "ui.h"
#include "player.h"
#include "progress.h"
#include <stddef.h>

/* Duplicated from app/screen.c's private button enum; see ui.c's identical
 * comment for why there is no shared header for this. Only the two bits
 * the entry/exit gesture needs. */
enum { BTN_SCL = 1U << 3, BTN_HOME = 1U << 8 };

enum shed_state { SHED_STATE_MENU, SHED_STATE_QUESTION, SHED_STATE_FEEDBACK, SHED_STATE_SUMMARY };

static unsigned active, exit_pending;
static unsigned toggle_previous_buttons;

static enum shed_state state;
static enum fm1_shed_exercise_type exercise_type;
static unsigned level; /* fixed at 0 in Fase 2: adaptive difficulty is Fase 3 */
static int current_key;
static struct fm1_shed_rng rng;
static struct fm1_shed_question question;
static unsigned chord_pick_index;
static int last_correct;
static struct fm1_shed_session session;

static enum fm1_shed_chord_type current_pick(void)
{
    return fm1_shed_chord_pool_at(level, chord_pick_index);
}

static void start_question(uint32_t now)
{
    uint32_t t;
    fm1_shed_player_silence();
    fm1_shed_ui_show_answer_leds(-1);
    current_key = fm1_shed_next_key(current_key, FM1_SHED_KEY_FOURTHS, &rng);
    fm1_shed_question_generate(&question, exercise_type, current_key, level, &rng);
    chord_pick_index = 0;
    t = fm1_shed_player_cadence(current_key, now + 150U);
    t += 150U;
    if (exercise_type == FM1_SHED_EX_DEGREE)
        fm1_shed_player_note(fm1_shed_pitch_class(current_key + question.degree), t, 750U);
    else
        fm1_shed_player_chord(question.chord, question.chord_root, FM1_SHED_VOICING_A, t, 1300U);
    state = SHED_STATE_QUESTION;
}

static void new_session(uint32_t now)
{
    fm1_shed_session_reset(&session);
    start_question(now);
}

/* Records the answer, shows the correct one, and on a miss schedules it to
 * play a moment later (SHED_SPEC.md section 5 "reproducción de la
 * respuesta correcta tras un fallo"). */
static void answer(uint32_t now, int correct)
{
    fm1_shed_session_record(&session, correct);
    last_correct = correct;
    if (exercise_type == FM1_SHED_EX_DEGREE)
        fm1_shed_ui_show_answer_leds(fm1_shed_pitch_class(current_key + question.degree));
    if (!correct) {
        fm1_shed_player_silence();
        if (exercise_type == FM1_SHED_EX_DEGREE)
            fm1_shed_player_note(fm1_shed_pitch_class(current_key + question.degree), now + 300U, 700U);
        else
            fm1_shed_player_chord(question.chord, question.chord_root, FM1_SHED_VOICING_A, now + 300U, 1200U);
    }
    state = SHED_STATE_FEEDBACK;
}

static void handle_input(uint32_t now, const struct fm1_shed_input *in)
{
    if (in->action & FM1_SHED_ACTION_TOGGLE_TYPE) {
        exercise_type = exercise_type == FM1_SHED_EX_DEGREE ? FM1_SHED_EX_CHORD_QUALITY : FM1_SHED_EX_DEGREE;
        if (state == SHED_STATE_QUESTION)
            start_question(now);
        return;
    }
    switch (state) {
    case SHED_STATE_MENU:
        if (in->action & FM1_SHED_ACTION_REPEAT_STIMULUS) /* PLAY doubles as "start" on the menu */
            new_session(now);
        break;
    case SHED_STATE_QUESTION:
        if (exercise_type == FM1_SHED_EX_DEGREE && in->note >= 0) {
            answer(now, fm1_shed_question_check_note(&question, in->note));
        } else if (exercise_type == FM1_SHED_EX_CHORD_QUALITY) {
            if (in->chord_delta) {
                unsigned pool = fm1_shed_chord_pool_size(level);
                int idx = (int)chord_pick_index + in->chord_delta;
                idx %= (int)pool;
                if (idx < 0)
                    idx += (int)pool;
                chord_pick_index = (unsigned)idx;
            }
            if (in->action & FM1_SHED_ACTION_CONFIRM)
                answer(now, fm1_shed_question_check_chord(&question, current_pick()));
        }
        if (in->action & FM1_SHED_ACTION_REPEAT_CADENCE) {
            fm1_shed_player_silence();
            fm1_shed_player_cadence(current_key, now + 80U);
        } else if (in->action & FM1_SHED_ACTION_REPEAT_STIMULUS) {
            fm1_shed_player_silence();
            if (exercise_type == FM1_SHED_EX_DEGREE)
                fm1_shed_player_note(fm1_shed_pitch_class(current_key + question.degree), now + 80U, 750U);
            else
                fm1_shed_player_chord(question.chord, question.chord_root, FM1_SHED_VOICING_A, now + 80U, 1300U);
        } else if (in->action & FM1_SHED_ACTION_REVEAL) {
            answer(now, 0);
        } else if (in->action & FM1_SHED_ACTION_NEXT) {
            start_question(now); /* skip, unscored */
        }
        break;
    case SHED_STATE_FEEDBACK:
        if (in->action & FM1_SHED_ACTION_NEXT) {
            if (fm1_shed_session_done(&session)) {
                state = SHED_STATE_SUMMARY;
                fm1_shed_ui_show_answer_leds(-1);
            } else {
                start_question(now);
            }
        }
        break;
    case SHED_STATE_SUMMARY:
        if (in->action & FM1_SHED_ACTION_NEXT)
            new_session(now);
        break;
    }
}

static void build_ui_state(struct fm1_shed_ui_state *out)
{
    out->screen = state == SHED_STATE_MENU ? FM1_SHED_SCREEN_MENU :
                  state == SHED_STATE_QUESTION ? FM1_SHED_SCREEN_QUESTION :
                  state == SHED_STATE_FEEDBACK ? FM1_SHED_SCREEN_FEEDBACK : FM1_SHED_SCREEN_SUMMARY;
    out->key = state == SHED_STATE_MENU ? -1 : current_key;
    out->exercise = exercise_type;
    out->question = state == SHED_STATE_FEEDBACK ? &question : NULL;
    out->chord_pick = current_pick();
    out->asked = session.asked;
    out->session_length = FM1_SHED_SESSION_LENGTH;
    out->streak = session.streak;
    out->best_streak = session.best_streak;
    out->correct = last_correct;
    out->percent = fm1_shed_session_percent(&session);
}

void shed_enter(uint32_t now)
{
    fm1_shed_player_claim();
    fm1_shed_ui_clear_leds(); /* don't inherit whatever the normal UI had lit */
    fm1_shed_rng_seed(&rng, now ? now : 1U);
    current_key = 0;
    exercise_type = FM1_SHED_EX_DEGREE;
    level = 0;
    chord_pick_index = 0;
    last_correct = 0;
    state = SHED_STATE_MENU;
    fm1_shed_session_reset(&session);
}

void shed_exit(void)
{
    fm1_shed_player_release();
    fm1_shed_ui_clear_leds();
}

int shed_tick(const struct fm1_controls *controls, uint32_t now)
{
    unsigned pressed, toggled;
    if (!controls)
        return active;
    pressed = controls->buttons & ~toggle_previous_buttons;
    toggled = ((controls->buttons & (BTN_HOME | BTN_SCL)) == (BTN_HOME | BTN_SCL)) &&
              (pressed & (BTN_HOME | BTN_SCL));
    toggle_previous_buttons = controls->buttons;

    if (!active) {
        if (toggled) {
            shed_enter(now);
            fm1_shed_ui_reset(controls);
            active = 1;
            exit_pending = 0;
        }
        return (int)active;
    }
    if (!exit_pending && toggled) {
        shed_exit();
        exit_pending = 1;
    }
    if (exit_pending) {
        if (!controls->buttons && !controls->keys) {
            active = 0;
            exit_pending = 0;
            return 0;
        }
        return 1;
    }

    fm1_shed_player_tick(now);
    {
        struct fm1_shed_input input;
        struct fm1_shed_ui_state ui_state;
        fm1_shed_ui_read(controls, &input);
        handle_input(now, &input);
        build_ui_state(&ui_state);
        fm1_shed_ui_draw(&ui_state, now);
    }
    return 1;
}

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

/* Difficulty bands, shared by every pooled exercise's table in exercises.c
 * (degree/chord/tension/scale) and by the non-pooled ones' own band logic. */
enum { SHED_MAX_LEVEL = 2 };

enum shed_state { SHED_STATE_MENU, SHED_STATE_QUESTION, SHED_STATE_FEEDBACK, SHED_STATE_SUMMARY };

static unsigned active, exit_pending;
static unsigned toggle_previous_buttons;

static enum shed_state state;
static enum fm1_shed_exercise_type exercise_type;
static unsigned menu_index;
static unsigned level;
static int current_key;
static struct fm1_shed_rng rng;
static struct fm1_shed_question question;
static struct fm1_shed_scores scores;
static unsigned dial_index;         /* CHORD_QUALITY/TENSION/SCALE: current dial highlight */
static unsigned current_pool_index; /* the pooled index the active question was asked at */
static unsigned sequence_progress;  /* ARPEGGIO/BASS_MOVEMENT/MELODIC_DICTATION */
static int last_correct;
static struct fm1_shed_session session;

/* The four exercises with a bounded, named pool of answers: the ones
 * progress.c's spaced repetition applies to (see exercises.h). */
static int is_pooled(enum fm1_shed_exercise_type type)
{
    return type == FM1_SHED_EX_DEGREE || type == FM1_SHED_EX_CHORD_QUALITY ||
           type == FM1_SHED_EX_TENSION || type == FM1_SHED_EX_SCALE;
}

static unsigned pooled_size(enum fm1_shed_exercise_type type, unsigned lvl)
{
    switch (type) {
    case FM1_SHED_EX_DEGREE: return fm1_shed_degree_pool_size(lvl);
    case FM1_SHED_EX_CHORD_QUALITY: return fm1_shed_chord_pool_size(lvl);
    case FM1_SHED_EX_TENSION: return fm1_shed_tension_pool_size(lvl);
    case FM1_SHED_EX_SCALE: return fm1_shed_scale_pool_size(lvl);
    default: return 0;
    }
}

static int is_sequence_exercise(enum fm1_shed_exercise_type type)
{
    return type == FM1_SHED_EX_ARPEGGIO || type == FM1_SHED_EX_BASS_MOVEMENT ||
           type == FM1_SHED_EX_MELODIC_DICTATION;
}

/* Schedules the current question's stimulus (everything after the cadence)
 * starting at `t`. Shared by start_question(), "repeat stimulus" and
 * replaying the right answer after a miss, so there is exactly one place
 * that knows how each of the nine exercises sounds. */
static uint32_t schedule_stimulus(uint32_t t)
{
    switch (exercise_type) {
    case FM1_SHED_EX_DEGREE:
        return fm1_shed_player_note(fm1_shed_pitch_class(current_key + question.degree), t, 750U);
    case FM1_SHED_EX_INTERVAL_MELODIC: {
        int ref = fm1_shed_interval_reference_note(current_key);
        uint32_t t2 = fm1_shed_player_note_register(0, ref, t, 600U);
        return fm1_shed_player_note_register(0, ref + question.interval, t2 + 100U, 600U);
    }
    case FM1_SHED_EX_INTERVAL_HARMONIC: {
        int ref = fm1_shed_interval_reference_note(current_key);
        uint32_t end1 = fm1_shed_player_note_register(0, ref, t, 900U);
        uint32_t end2 = fm1_shed_player_note_register(0, ref + question.interval, t, 900U);
        return end1 > end2 ? end1 : end2;
    }
    case FM1_SHED_EX_CHORD_QUALITY:
        return fm1_shed_player_chord(question.chord, question.chord_root, FM1_SHED_VOICING_A, t, 1300U);
    case FM1_SHED_EX_TENSION: {
        uint32_t end1 = fm1_shed_player_chord(question.chord, question.chord_root, FM1_SHED_VOICING_A, t, 1300U);
        /* The tension sits a register above the comping voicing, so it
         * reads as "the note on top" rather than blending into the chord. */
        uint32_t end2 = fm1_shed_player_note_register(
            question.chord_root + fm1_shed_tension_semitones(question.tension), 60, t, 1300U);
        return end1 > end2 ? end1 : end2;
    }
    case FM1_SHED_EX_ARPEGGIO:
        return fm1_shed_player_sequence(question.sequence, question.sequence_count,
            FM1_SHED_PIANO_REGISTER, 1 /* ascending */, t, 500U);
    case FM1_SHED_EX_SCALE: {
        uint8_t notes[FM1_SHED_SCALE_MAX_NOTES];
        unsigned n = fm1_shed_scale_pitch_classes(question.scale, current_key, notes);
        uint32_t pedal_end = fm1_shed_player_note_register(current_key, FM1_SHED_BASS_REGISTER, t,
            (uint32_t)n * 220U + 200U);
        uint32_t seq_end = fm1_shed_player_sequence(notes, n, 60, 1 /* ascending */, t, 220U);
        return pedal_end > seq_end ? pedal_end : seq_end;
    }
    case FM1_SHED_EX_BASS_MOVEMENT: {
        uint32_t t2 = t;
        unsigned i;
        for (i = 0; i < question.sequence_count; i++)
            t2 = fm1_shed_player_chord((enum fm1_shed_chord_type)question.sequence_chord[i], question.sequence[i],
                (i & 1U) ? FM1_SHED_VOICING_B : FM1_SHED_VOICING_A, t2, 800U);
        return t2;
    }
    case FM1_SHED_EX_MELODIC_DICTATION:
        return fm1_shed_player_sequence(question.sequence, question.sequence_count,
            FM1_SHED_PIANO_REGISTER, 0 /* fixed register */, t, 420U);
    default:
        return t;
    }
}

static void start_question(uint32_t now)
{
    uint32_t t;
    fm1_shed_player_silence();
    fm1_shed_ui_show_answer_leds(-1);
    current_key = fm1_shed_next_key(current_key, FM1_SHED_KEY_FOURTHS, &rng);
    if (is_pooled(exercise_type)) {
        current_pool_index = fm1_shed_scores_pick(&scores, exercise_type, pooled_size(exercise_type, level), &rng);
        fm1_shed_question_generate_at(&question, exercise_type, current_key, level, current_pool_index, &rng);
    } else {
        fm1_shed_question_generate(&question, exercise_type, current_key, level, &rng);
    }
    dial_index = 0;
    sequence_progress = 0;
    t = fm1_shed_player_cadence(current_key, now + 150U);
    schedule_stimulus(t + 150U);
    state = SHED_STATE_QUESTION;
}

static void new_session(uint32_t now)
{
    fm1_shed_session_reset(&session);
    start_question(now);
}

/* Records the answer (session stats, spaced-repetition score, adaptive
 * difficulty), shows the correct one on the keys when it's key-based, and
 * on a miss replays the stimulus a moment later (SHED_SPEC.md section 5
 * "reproducción de la respuesta correcta tras un fallo"). */
static void answer(uint32_t now, int correct)
{
    fm1_shed_session_record(&session, correct);
    if (is_pooled(exercise_type))
        fm1_shed_scores_record(&scores, exercise_type, current_pool_index, correct);
    level = fm1_shed_session_adapt_level(&session, level, SHED_MAX_LEVEL);
    last_correct = correct;
    if (exercise_type == FM1_SHED_EX_DEGREE)
        fm1_shed_ui_show_answer_leds(fm1_shed_pitch_class(current_key + question.degree));
    else if (exercise_type == FM1_SHED_EX_INTERVAL_MELODIC || exercise_type == FM1_SHED_EX_INTERVAL_HARMONIC)
        fm1_shed_ui_show_answer_leds(
            fm1_shed_pitch_class(fm1_shed_interval_reference_note(current_key) + question.interval));
    if (!correct) {
        fm1_shed_player_silence();
        schedule_stimulus(now + 300U);
    }
    state = SHED_STATE_FEEDBACK;
}

static void move_dial(int delta, unsigned pool_size)
{
    int idx;
    if (!delta || !pool_size)
        return;
    idx = (int)dial_index + delta;
    idx %= (int)pool_size;
    if (idx < 0)
        idx += (int)pool_size;
    dial_index = (unsigned)idx;
}

static void handle_question_input(uint32_t now, const struct fm1_shed_input *in)
{
    switch (exercise_type) {
    case FM1_SHED_EX_DEGREE:
        if (in->note >= 0) {
            answer(now, fm1_shed_question_check_note(&question, in->note));
            return;
        }
        break;
    case FM1_SHED_EX_INTERVAL_MELODIC:
    case FM1_SHED_EX_INTERVAL_HARMONIC:
        if (in->note_absolute >= 0) {
            answer(now, fm1_shed_question_check_interval_note(&question, in->note_absolute));
            return;
        }
        break;
    case FM1_SHED_EX_CHORD_QUALITY:
        move_dial(in->dial_delta, fm1_shed_chord_pool_size(level));
        if (in->action & FM1_SHED_ACTION_CONFIRM) {
            answer(now, fm1_shed_question_check_chord(&question, fm1_shed_chord_pool_at(level, dial_index)));
            return;
        }
        break;
    case FM1_SHED_EX_TENSION:
        move_dial(in->dial_delta, fm1_shed_tension_pool_size(level));
        if (in->action & FM1_SHED_ACTION_CONFIRM) {
            answer(now, fm1_shed_question_check_tension(&question, fm1_shed_tension_pool_at(level, dial_index)));
            return;
        }
        break;
    case FM1_SHED_EX_SCALE:
        move_dial(in->dial_delta, fm1_shed_scale_pool_size(level));
        if (in->action & FM1_SHED_ACTION_CONFIRM) {
            answer(now, fm1_shed_question_check_scale(&question, fm1_shed_scale_pool_at(level, dial_index)));
            return;
        }
        break;
    case FM1_SHED_EX_ARPEGGIO:
    case FM1_SHED_EX_BASS_MOVEMENT:
    case FM1_SHED_EX_MELODIC_DICTATION:
        if (in->note >= 0) {
            int r = fm1_shed_question_check_sequence_note(&question, &sequence_progress, in->note);
            if (r == 0) {
                answer(now, 0);
                return;
            }
            if (fm1_shed_question_sequence_done(&question, sequence_progress)) {
                answer(now, 1);
                return;
            }
        }
        break;
    default:
        break;
    }
    /* Common controls: only reached when the exercise-specific handling
     * above didn't already answer the question this poll. */
    if (in->action & FM1_SHED_ACTION_REPEAT_CADENCE) {
        fm1_shed_player_silence();
        fm1_shed_player_cadence(current_key, now + 80U);
    } else if (in->action & FM1_SHED_ACTION_REPEAT_STIMULUS) {
        fm1_shed_player_silence();
        schedule_stimulus(now + 80U);
    } else if (in->action & FM1_SHED_ACTION_REVEAL) {
        answer(now, 0);
    } else if (in->action & FM1_SHED_ACTION_NEXT) {
        start_question(now); /* skip, unscored */
    }
}

static void handle_input(uint32_t now, const struct fm1_shed_input *in)
{
    if (state != SHED_STATE_MENU && (in->action & FM1_SHED_ACTION_TOGGLE_TYPE)) {
        exercise_type = (enum fm1_shed_exercise_type)((exercise_type + 1U) % FM1_SHED_EX_TYPE_COUNT);
        if (state == SHED_STATE_QUESTION)
            start_question(now);
        return;
    }
    switch (state) {
    case SHED_STATE_MENU: {
        int idx = (int)menu_index + in->dial_delta;
        idx %= (int)FM1_SHED_EX_TYPE_COUNT;
        if (idx < 0)
            idx += (int)FM1_SHED_EX_TYPE_COUNT;
        menu_index = (unsigned)idx;
        if (in->action & FM1_SHED_ACTION_REPEAT_STIMULUS) { /* PLAY doubles as "start" on the menu */
            exercise_type = (enum fm1_shed_exercise_type)menu_index;
            new_session(now);
        }
        break;
    }
    case SHED_STATE_QUESTION:
        handle_question_input(now, in);
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
    out->menu_index = menu_index;
    out->level = level;
    out->question = state == SHED_STATE_FEEDBACK ? &question : NULL;
    out->chord_pick = exercise_type == FM1_SHED_EX_CHORD_QUALITY ?
        fm1_shed_chord_pool_at(level, dial_index) : FM1_SHED_CHORD_COUNT;
    out->tension_pick = exercise_type == FM1_SHED_EX_TENSION ?
        fm1_shed_tension_pool_at(level, dial_index) : FM1_SHED_TENSION_COUNT;
    out->scale_pick = exercise_type == FM1_SHED_EX_SCALE ?
        fm1_shed_scale_pool_at(level, dial_index) : FM1_SHED_SCALE_COUNT;
    out->sequence_progress = is_sequence_exercise(exercise_type) ? sequence_progress : 0;
    out->sequence_total = question.sequence_count;
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
    menu_index = 0;
    level = 0;
    dial_index = 0;
    sequence_progress = 0;
    last_correct = 0;
    state = SHED_STATE_MENU;
    fm1_shed_session_reset(&session);
    fm1_shed_scores_reset(&scores);
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
        return (int)active;
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

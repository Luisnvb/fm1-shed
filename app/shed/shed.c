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

/* The 9 exercises (section 5) plus vamp mode (section 6) as one more menu
 * entry -- not one more fm1_shed_exercise_type; see ui.c's MENU case. */
enum { SHED_MENU_COUNT = FM1_SHED_EX_TYPE_COUNT + 1 };

enum shed_state { SHED_STATE_MENU, SHED_STATE_QUESTION, SHED_STATE_FEEDBACK, SHED_STATE_SUMMARY, SHED_STATE_VAMP };

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

/* Vamp mode (section 6). vamp_ear_active marks a QUESTION/FEEDBACK cycle
 * that came from vamp mode's "oído" round (GLO) rather than the 20-question
 * quiz flow: it reuses the TENSION/SCALE machinery wholesale (same question
 * struct, same dial-pick input handling, same FEEDBACK screen) but must not
 * touch session/scores/level, and returns to SHED_STATE_VAMP afterwards
 * instead of the next quiz question or the summary. */
static unsigned vamp_choice;
static enum fm1_shed_vamp_view vamp_view;
static unsigned vamp_ear_active;

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
 * that knows how each of the nine exercises sounds. Also reused by vamp
 * mode's "oído" round for TENSION, where question.chord_root is the vamp's
 * current chord instead of a fresh cadence's tonic. */
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
 * difficulty -- skipped for vamp mode's "oído" round, which isn't part of
 * the quiz flow those belong to), shows the correct one on the keys when
 * it's key-based, and on a miss replays the stimulus a moment later
 * (SHED_SPEC.md section 5 "reproducción de la respuesta correcta tras un
 * fallo"; skipped for "oído" too, to avoid panic-silencing the vamp that's
 * still supposed to be playing underneath). */
static void answer(uint32_t now, int correct)
{
    if (!vamp_ear_active) {
        fm1_shed_session_record(&session, correct);
        if (is_pooled(exercise_type))
            fm1_shed_scores_record(&scores, exercise_type, current_pool_index, correct);
        level = fm1_shed_session_adapt_level(&session, level, SHED_MAX_LEVEL);
    }
    last_correct = correct;
    if (exercise_type == FM1_SHED_EX_DEGREE)
        fm1_shed_ui_show_answer_leds(fm1_shed_pitch_class(current_key + question.degree));
    else if (exercise_type == FM1_SHED_EX_INTERVAL_MELODIC || exercise_type == FM1_SHED_EX_INTERVAL_HARMONIC)
        fm1_shed_ui_show_answer_leds(
            fm1_shed_pitch_class(fm1_shed_interval_reference_note(current_key) + question.interval));
    if (!correct && !vamp_ear_active) {
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
     * above didn't already answer the question this poll. "Repeat cadence"
     * is skipped during vamp's "oído" round -- no fresh cadence was played
     * to repeat, and injecting one would clash with the still-running vamp. */
    if (in->action & FM1_SHED_ACTION_REPEAT_CADENCE) {
        if (!vamp_ear_active) {
            fm1_shed_player_silence();
            fm1_shed_player_cadence(current_key, now + 80U);
        }
    } else if (in->action & FM1_SHED_ACTION_REPEAT_STIMULUS) {
        fm1_shed_player_silence();
        schedule_stimulus(now + 80U);
    } else if (in->action & FM1_SHED_ACTION_REVEAL) {
        answer(now, 0);
    } else if (in->action & FM1_SHED_ACTION_NEXT) {
        if (vamp_ear_active) {
            vamp_ear_active = 0;
            state = SHED_STATE_VAMP; /* skip the round, the vamp never stopped */
        } else {
            start_question(now); /* skip, unscored */
        }
    }
}

/* Vamp mode (SHED_SPEC.md section 6). `which` 0 = current step, 1 = next;
 * matches fm1_shed_player_vamp_step_index(). */
static void vamp_step_info(unsigned which, enum fm1_shed_chord_type *type, enum fm1_shed_context *context, int *root)
{
    const struct fm1_shed_vamp *vamp = fm1_shed_vamp(vamp_choice);
    unsigned index = fm1_shed_player_vamp_step_index(which);
    if (!vamp || index >= vamp->count) {
        *type = FM1_SHED_CHORD_COUNT;
        *context = FM1_SHED_CONTEXT_NONE;
        *root = -1;
        return;
    }
    *type = vamp->steps[index].chord;
    *context = vamp->steps[index].context;
    *root = fm1_shed_pitch_class(current_key + vamp->steps[index].offset);
}

static void start_vamp(uint32_t now)
{
    const struct fm1_shed_vamp *vamp = fm1_shed_vamp(vamp_choice);
    fm1_shed_player_silence();
    fm1_shed_player_vamp_stop();
    fm1_shed_ui_clear_leds();
    if (vamp)
        fm1_shed_player_vamp_start(vamp->steps, vamp->count, current_key, now);
    vamp_ear_active = 0;
    state = SHED_STATE_VAMP;
}

/* The "oído" variant (section 6): a short phrase on the lead track, over
 * the still-running vamp, built from either the tension pool (when the
 * current chord is a plain dominant 7) or the current chord's suggested
 * scale otherwise -- then the same TENSION/SCALE question/dial-pick/
 * feedback machinery as exercises #5/#7, just rooted at the vamp's current
 * chord instead of a fresh cadence. */
static void start_vamp_ear(uint32_t now)
{
    enum fm1_shed_chord_type type;
    enum fm1_shed_context context;
    int root;
    uint8_t phrase[5];
    unsigned n = 0;

    vamp_step_info(0, &type, &context, &root);
    if (root < 0)
        return;
    vamp_ear_active = 1;
    dial_index = 0;
    if (type == FM1_SHED_CHORD_DOM7) {
        const struct fm1_shed_chord *c = fm1_shed_chord(type);
        exercise_type = FM1_SHED_EX_TENSION;
        fm1_shed_question_generate(&question, FM1_SHED_EX_TENSION, root, level, &rng);
        if (c && c->count > 3)
            phrase[n++] = (uint8_t)fm1_shed_pitch_class(root + c->intervals[3]); /* the 7th, as an approach */
        phrase[n++] = (uint8_t)fm1_shed_pitch_class(root + fm1_shed_tension_semitones(question.tension));
    } else {
        const struct fm1_shed_chord_scale *row = fm1_shed_chord_scale_lookup(type, context);
        uint8_t notes[FM1_SHED_SCALE_MAX_NOTES];
        unsigned count, i;
        exercise_type = FM1_SHED_EX_SCALE;
        question.type = FM1_SHED_EX_SCALE;
        question.key = root;
        question.degree = -1;
        question.chord = FM1_SHED_CHORD_COUNT;
        question.chord_root = -1;
        question.interval = 0;
        question.tension = FM1_SHED_TENSION_COUNT;
        question.scale = row ? row->scale : FM1_SHED_SCALE_IONIAN;
        question.sequence_count = 0;
        count = fm1_shed_scale_pitch_classes(question.scale, root, notes);
        n = count < 5U ? count : 5U;
        for (i = 0; i < n; i++)
            phrase[i] = notes[(i * 2U) % count]; /* a little melodic movement, not strictly ascending */
    }
    fm1_shed_player_lead_phrase(phrase, n, 72, now + 80U, 280U);
    state = SHED_STATE_QUESTION;
}

static void handle_vamp_input(uint32_t now, const struct fm1_shed_input *in)
{
    unsigned i;
    for (i = 0; i < FM1_SHED_KEY_COUNT; i++) {
        if (in->keys_pressed & (1U << i))
            fm1_shed_player_lead_note(FM1_SHED_KEY_BASE_NOTE + i, 1, now);
        if (in->keys_released & (1U << i))
            fm1_shed_player_lead_note(FM1_SHED_KEY_BASE_NOTE + i, 0, now);
    }
    if (in->action & FM1_SHED_ACTION_REVEAL) { /* EDIT: cycle the LED view */
        vamp_view = (enum fm1_shed_vamp_view)((vamp_view + 1U) % FM1_SHED_VAMP_VIEW_COUNT);
    } else if (in->action & FM1_SHED_ACTION_REPEAT_CADENCE) { /* SEQ: next vamp */
        vamp_choice = (vamp_choice + 1U) % FM1_SHED_VAMP_COUNT;
        start_vamp(now);
    } else if (in->action & FM1_SHED_ACTION_NEXT) { /* ARP: next key */
        current_key = fm1_shed_next_key(current_key, FM1_SHED_KEY_FOURTHS, &rng);
        start_vamp(now);
    } else if (in->action & FM1_SHED_ACTION_CONFIRM) { /* GLO: oído round */
        start_vamp_ear(now);
    }
}

static void handle_input(uint32_t now, const struct fm1_shed_input *in)
{
    if (state != SHED_STATE_MENU && state != SHED_STATE_VAMP && !vamp_ear_active &&
        (in->action & FM1_SHED_ACTION_TOGGLE_TYPE)) {
        exercise_type = (enum fm1_shed_exercise_type)((exercise_type + 1U) % FM1_SHED_EX_TYPE_COUNT);
        if (state == SHED_STATE_QUESTION)
            start_question(now);
        return;
    }
    switch (state) {
    case SHED_STATE_MENU: {
        int idx = (int)menu_index + in->dial_delta;
        idx %= (int)SHED_MENU_COUNT;
        if (idx < 0)
            idx += (int)SHED_MENU_COUNT;
        menu_index = (unsigned)idx;
        if (in->action & FM1_SHED_ACTION_REPEAT_STIMULUS) { /* PLAY doubles as "start" on the menu */
            if (menu_index < FM1_SHED_EX_TYPE_COUNT) {
                exercise_type = (enum fm1_shed_exercise_type)menu_index;
                new_session(now);
            } else {
                vamp_choice = 0;
                vamp_view = FM1_SHED_VAMP_VIEW_ARPEGGIO;
                start_vamp(now);
            }
        }
        break;
    }
    case SHED_STATE_QUESTION:
        handle_question_input(now, in);
        break;
    case SHED_STATE_FEEDBACK:
        if (in->action & FM1_SHED_ACTION_NEXT) {
            if (vamp_ear_active) {
                vamp_ear_active = 0;
                state = SHED_STATE_VAMP;
                fm1_shed_ui_show_answer_leds(-1);
            } else if (fm1_shed_session_done(&session)) {
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
    case SHED_STATE_VAMP:
        handle_vamp_input(now, in);
        break;
    }
}

static void build_ui_state(struct fm1_shed_ui_state *out)
{
    out->screen = state == SHED_STATE_MENU ? FM1_SHED_SCREEN_MENU :
                  state == SHED_STATE_QUESTION ? FM1_SHED_SCREEN_QUESTION :
                  state == SHED_STATE_FEEDBACK ? FM1_SHED_SCREEN_FEEDBACK :
                  state == SHED_STATE_SUMMARY ? FM1_SHED_SCREEN_SUMMARY : FM1_SHED_SCREEN_VAMP;
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
    out->session_length = (state == SHED_STATE_VAMP || vamp_ear_active) ? 0 : FM1_SHED_SESSION_LENGTH;
    out->streak = session.streak;
    out->best_streak = session.best_streak;
    out->correct = last_correct;
    out->percent = fm1_shed_session_percent(&session);

    out->vamp_index = vamp_choice;
    out->vamp_view = vamp_view;
    if (state == SHED_STATE_VAMP) {
        enum fm1_shed_chord_type next_type;
        enum fm1_shed_context context, next_context;
        const struct fm1_shed_chord_scale *row;
        vamp_step_info(0, &out->vamp_chord, &context, &out->vamp_root);
        vamp_step_info(1, &next_type, &next_context, &out->vamp_next_root);
        out->vamp_next_chord = next_type;
        row = fm1_shed_chord_scale_lookup(out->vamp_chord, context);
        out->vamp_scale = row ? row->scale : FM1_SHED_SCALE_IONIAN;
    }
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
    vamp_choice = 0;
    vamp_view = FM1_SHED_VAMP_VIEW_ARPEGGIO;
    vamp_ear_active = 0;
    state = SHED_STATE_MENU;
    fm1_shed_session_reset(&session);
    fm1_shed_scores_reset(&scores);
}

void shed_exit(void)
{
    fm1_shed_player_vamp_stop(); /* so a later shed_enter() doesn't inherit a stale running loop */
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
    fm1_shed_player_vamp_tick(now);
    {
        struct fm1_shed_input input;
        struct fm1_shed_ui_state ui_state;
        fm1_shed_ui_read(controls, &input);
        handle_input(now, &input);
        if (state == SHED_STATE_VAMP) {
            enum fm1_shed_chord_type type;
            enum fm1_shed_context context;
            int root;
            const struct fm1_shed_chord_scale *row;
            vamp_step_info(0, &type, &context, &root);
            row = fm1_shed_chord_scale_lookup(type, context);
            fm1_shed_ui_show_vamp_leds(vamp_view, type, root, row ? row->scale : FM1_SHED_SCALE_IONIAN, now);
        }
        build_ui_state(&ui_state);
        fm1_shed_ui_draw(&ui_state, now);
    }
    return 1;
}

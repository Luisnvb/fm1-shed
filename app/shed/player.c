/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "player.h"
#include "instrument.h"
#include <stddef.h>

/* Factory DX7 programs (app/fm_factory.inc, V15 bank): "PIANO 1", "BASS 1",
 * "VOICE 1" (the lead/live-play track, SHED_SPEC.md section 3's "pista C").
 * Chosen from the factory names only (section 2's open question on the
 * single best voice per role is still unverified by ear; easy to change
 * here once it is). */
enum { SHED_TRACKS = 3 };
static const unsigned char claim_track[SHED_TRACKS] = {SHED_TRACK_PIANO, SHED_TRACK_BASS, SHED_TRACK_LEAD};
static const unsigned char claim_program[SHED_TRACKS] = {0, 34, 67};

struct shed_track_backup {
    uint8_t type, program, role, channel, muted;
    struct groove_step cells[GROOVE_STEPS];
};
static struct shed_track_backup backup[SHED_TRACKS];
static unsigned claimed;
static unsigned transport_was_playing;
static unsigned project_was_clean;

struct shed_event {
    uint32_t t_ms;
    uint8_t message[3];
    uint8_t bytes;
};
enum { SHED_EVENT_QUEUE = 48 };
static struct shed_event queue[SHED_EVENT_QUEUE];
static unsigned queue_count;

static void schedule(uint32_t t_ms, uint8_t status, uint8_t data1, uint8_t data2, uint8_t bytes)
{
    /* Sized generously for one cadence (3 chords) plus one target chord or
     * note; silently dropping past that just means a glitched stimulus, not
     * a crash, and Fase 2 never schedules that much at once. */
    if (queue_count >= SHED_EVENT_QUEUE)
        return;
    queue[queue_count].t_ms = t_ms;
    queue[queue_count].message[0] = status;
    queue[queue_count].message[1] = data1;
    queue[queue_count].message[2] = data2;
    queue[queue_count].bytes = bytes;
    queue_count++;
}

static void note_on(uint32_t t_ms, unsigned track, unsigned note, unsigned velocity)
{
    /* Same local-MIDI shape as app/screen.c's post() for physical keys:
     * 0x90 + channel, note, velocity. */
    schedule(t_ms, (uint8_t)(0x90U + groove_midi_input_channel(track)), (uint8_t)note, (uint8_t)velocity, 3);
}

static void note_off(uint32_t t_ms, unsigned track, unsigned note)
{
    schedule(t_ms, (uint8_t)(0x80U + groove_midi_input_channel(track)), (uint8_t)note, 0, 3);
}

static unsigned rootless_notes(enum fm1_shed_chord_type type, int root_pc, enum fm1_shed_voicing variant,
    uint8_t out[FM1_SHED_CHORD_MAX_NOTES])
{
    const struct fm1_shed_chord *c = fm1_shed_chord(type);
    int rel[FM1_SHED_CHORD_MAX_NOTES];
    unsigned n, i;
    if (!c || c->count < 2)
        return 0;
    n = c->count - 1;
    for (i = 0; i < n; i++)
        rel[i] = c->intervals[i + 1]; /* drop the root, keep the rest ascending */
    if (variant == FM1_SHED_VOICING_B && n > 1) {
        /* Move the top note down an octave to the bottom: same pitch
         * classes, tighter span, smoother voice leading chord to chord. */
        int top = rel[n - 1];
        for (i = n - 1; i > 0; i--)
            rel[i] = rel[i - 1];
        rel[0] = top - 12;
    }
    root_pc = fm1_shed_pitch_class(root_pc);
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)(FM1_SHED_PIANO_REGISTER + root_pc + rel[i]);
    return n;
}

void fm1_shed_player_claim(void)
{
    unsigned i;
    struct groove_view transport = {0};
    if (claimed)
        return;
    project_was_clean = !groove_project_dirty();
    groove_get_view(claim_track[0], 0, &transport);
    transport_was_playing = transport.playing;
    if (transport_was_playing) {
        static const uint8_t stop_msg[1] = {0xfc};
        groove_midi_local(stop_msg, 1);
    }
    for (i = 0; i < SHED_TRACKS; i++) {
        unsigned t = claim_track[i];
        struct groove_view v = {0};
        groove_get_view(t, 0, &v);
        backup[i].type = (uint8_t)groove_source_type(t);
        backup[i].program = v.program;
        backup[i].role = (uint8_t)groove_track_role(t);
        backup[i].channel = (uint8_t)groove_midi_input_channel(t);
        backup[i].muted = v.muted;
        groove_sequence_read(t, 0, backup[i].cells, GROOVE_STEPS);

        groove_track_role_set(t, GROOVE_TRACK_NORMAL);
        if (backup[i].type != GROOVE_DX7) {
            groove_source_set(t, GROOVE_DX7);
            groove_sequence_source_changed(t);
        }
        groove_get_view(t, 0, &v);
        groove_control_edit(t, (int)claim_program[i] - (int)v.program, 0, 0);
        groove_midi_input_set(t, groove_channel(t));
        if (backup[i].muted)
            groove_mix_toggle(t, 0);
    }
    claimed = 1;
    queue_count = 0;
}

void fm1_shed_player_release(void)
{
    unsigned i;
    if (!claimed)
        return;
    fm1_shed_player_silence();
    for (i = 0; i < SHED_TRACKS; i++) {
        unsigned t = claim_track[i];
        struct groove_view v = {0};
        if (groove_source_type(t) != backup[i].type) {
            groove_source_set(t, backup[i].type);
            groove_sequence_source_changed(t);
        }
        groove_get_view(t, 0, &v);
        groove_control_edit(t, (int)backup[i].program - (int)v.program, 0, 0);
        groove_track_role_set(t, backup[i].role);
        groove_midi_input_set(t, backup[i].channel);
        groove_sequence_replace(t, 0, backup[i].cells, GROOVE_STEPS);
        groove_get_view(t, 0, &v);
        if ((unsigned)v.muted != backup[i].muted)
            groove_mix_toggle(t, 0);
    }
    if (transport_was_playing) {
        static const uint8_t continue_msg[1] = {0xfb};
        groove_midi_local(continue_msg, 1);
    }
    if (project_was_clean)
        groove_project_clean();
    claimed = 0;
}

void fm1_shed_player_silence(void)
{
    static const uint8_t panic_msg[1] = {0xff}; /* app/screen.c's HOME+PLAY panic byte */
    queue_count = 0;
    groove_midi_local(panic_msg, 1);
}

void fm1_shed_player_tick(uint32_t now)
{
    unsigned i = 0;
    while (i < queue_count) {
        if ((int32_t)(now - queue[i].t_ms) >= 0) {
            groove_midi_local(queue[i].message, queue[i].bytes);
            queue[i] = queue[--queue_count];
        } else {
            i++;
        }
    }
}

uint32_t fm1_shed_player_chord(enum fm1_shed_chord_type type, int root, enum fm1_shed_voicing variant,
    uint32_t start_ms, uint32_t duration_ms)
{
    uint8_t notes[FM1_SHED_CHORD_MAX_NOTES];
    unsigned n = rootless_notes(type, root, variant, notes);
    unsigned i, hold = duration_ms > 60U ? duration_ms - 40U : duration_ms; /* short gap before the next event */
    uint8_t bass = (uint8_t)(FM1_SHED_BASS_REGISTER + fm1_shed_pitch_class(root));
    for (i = 0; i < n; i++) {
        note_on(start_ms, SHED_TRACK_PIANO, notes[i], 95);
        note_off(start_ms + hold, SHED_TRACK_PIANO, notes[i]);
    }
    note_on(start_ms, SHED_TRACK_BASS, bass, 100);
    note_off(start_ms + hold, SHED_TRACK_BASS, bass);
    return start_ms + duration_ms;
}

uint32_t fm1_shed_player_note_register(int pitch_class, int base_note, uint32_t start_ms, uint32_t duration_ms)
{
    uint8_t note = (uint8_t)(base_note + fm1_shed_pitch_class(pitch_class));
    unsigned hold = duration_ms > 60U ? duration_ms - 40U : duration_ms;
    note_on(start_ms, SHED_TRACK_PIANO, note, 100);
    note_off(start_ms + hold, SHED_TRACK_PIANO, note);
    return start_ms + duration_ms;
}

uint32_t fm1_shed_player_note(int pitch_class, uint32_t start_ms, uint32_t duration_ms)
{
    return fm1_shed_player_note_register(pitch_class, FM1_SHED_PIANO_REGISTER, start_ms, duration_ms);
}

uint32_t fm1_shed_player_sequence(const uint8_t *pitch_classes, unsigned count, int base_note,
    int ascending, uint32_t start_ms, uint32_t note_ms)
{
    uint32_t t = start_ms;
    int last = base_note - 1;
    unsigned i;
    if (!pitch_classes)
        return start_ms;
    for (i = 0; i < count; i++) {
        int note = base_note + fm1_shed_pitch_class(pitch_classes[i]);
        if (ascending)
            while (note <= last)
                note += 12;
        /* pitch_class 0 against base_note `note`: fm1_shed_pitch_class(0) is
         * 0, so this plays exactly `note` without re-reducing it mod 12
         * (which would undo the octave bump above). */
        t = fm1_shed_player_note_register(0, note, t, note_ms);
        last = note;
    }
    return t;
}

uint32_t fm1_shed_player_cadence(int key, uint32_t start_ms)
{
    enum { CHORD_MS = 850 };
    uint32_t t = start_ms;
    key = fm1_shed_pitch_class(key);
    t = fm1_shed_player_chord(FM1_SHED_CHORD_MIN7, fm1_shed_pitch_class(key + 2), FM1_SHED_VOICING_A, t, CHORD_MS);
    t = fm1_shed_player_chord(FM1_SHED_CHORD_DOM7, fm1_shed_pitch_class(key + 7), FM1_SHED_VOICING_B, t, CHORD_MS);
    t = fm1_shed_player_chord(FM1_SHED_CHORD_MAJ7, key, FM1_SHED_VOICING_A, t, CHORD_MS);
    return t;
}

/* Vamp mode (SHED_SPEC.md section 6): scheduled a step at a time so an
 * indefinite loop never has to sit fully queued (SHED_EVENT_QUEUE is
 * sized for one bounded exercise stimulus, not a whole song form). */
static const struct fm1_shed_vamp_step *vamp_steps;
static unsigned vamp_count, vamp_index;
static int vamp_key;
static uint32_t vamp_next_ms;
static unsigned vamp_active;

void fm1_shed_player_vamp_start(const struct fm1_shed_vamp_step *steps, unsigned count, int key, uint32_t now)
{
    if (!steps || !count) {
        vamp_active = 0;
        return;
    }
    vamp_steps = steps;
    vamp_count = count;
    vamp_key = fm1_shed_pitch_class(key);
    vamp_index = 0;
    fm1_shed_player_chord(steps[0].chord, fm1_shed_pitch_class(key + steps[0].offset), FM1_SHED_VOICING_A,
        now, steps[0].duration_ms);
    vamp_next_ms = now + steps[0].duration_ms;
    vamp_active = 1;
}

void fm1_shed_player_vamp_tick(uint32_t now)
{
    const struct fm1_shed_vamp_step *s;
    unsigned next_index;
    if (!vamp_active)
        return;
    /* A little lead time keeps this robust to irregular poll timing;
     * fm1_shed_player_chord() just enqueues a future event either way. */
    if ((int32_t)(now + 250U - vamp_next_ms) < 0)
        return;
    next_index = (vamp_index + 1U) % vamp_count;
    s = &vamp_steps[next_index];
    fm1_shed_player_chord(s->chord, fm1_shed_pitch_class(vamp_key + s->offset),
        (next_index & 1U) ? FM1_SHED_VOICING_B : FM1_SHED_VOICING_A, vamp_next_ms, s->duration_ms);
    vamp_index = next_index;
    vamp_next_ms += s->duration_ms;
}

void fm1_shed_player_vamp_stop(void)
{
    vamp_active = 0;
}

unsigned fm1_shed_player_vamp_step_index(unsigned which)
{
    return vamp_active && vamp_count ? (which ? (vamp_index + 1U) % vamp_count : vamp_index) : 0;
}

void fm1_shed_player_lead_note(unsigned midi_note, unsigned on, uint32_t now)
{
    if (on)
        note_on(now, SHED_TRACK_LEAD, midi_note, 100);
    else
        note_off(now, SHED_TRACK_LEAD, midi_note);
}

uint32_t fm1_shed_player_lead_phrase(const uint8_t *pitch_classes, unsigned count, int base_note,
    uint32_t start_ms, uint32_t note_ms)
{
    uint32_t t = start_ms;
    unsigned i, hold = note_ms > 60U ? note_ms - 40U : note_ms;
    if (!pitch_classes)
        return start_ms;
    for (i = 0; i < count; i++) {
        uint8_t note = (uint8_t)(base_note + fm1_shed_pitch_class(pitch_classes[i]));
        note_on(t, SHED_TRACK_LEAD, note, 100);
        note_off(t + hold, SHED_TRACK_LEAD, note);
        t += note_ms;
    }
    return t;
}

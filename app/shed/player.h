/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_PLAYER_H
#define FM1_SHED_PLAYER_H

/* Sequences Shed's cadences/chords/notes over time via groove_midi_local()
 * (SHED_SPEC.md section 3 "player.h/.c") and claims/releases the two
 * tracks it plays on (section 3 "No pisar el proyecto del usuario").
 * Depends on the FM-1 SDK; not host-testable. */

#include "theory.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Project tracks Shed plays on: 0 piano (comping/melody), 1 bass (roots). */
enum { SHED_TRACK_PIANO = 0, SHED_TRACK_BASS = 1 };

/* Rootless piano voicing variant (SHED_SPEC.md section 3, "tipo A/B
 * (3-5-7-9 / 7-9-3-5)"): A stacks the chord ascending with the root
 * dropped; B drops the root too but moves the top note down an octave to
 * the bottom, which is what keeps a ii-V-I's upper structure close together
 * instead of jumping an octave chord to chord. */
enum fm1_shed_voicing { FM1_SHED_VOICING_A = 0, FM1_SHED_VOICING_B };

/* Saves track 0/1's source, voice, role, input channel, mute state and
 * sequencer pattern, stops transport if it was running, and sets both
 * tracks up for Shed (DX7 "PIANO 1" / "BASS 1", input channel matching
 * groove_channel(), role NORMAL, unmuted). A second call before release()
 * is a no-op. */
void fm1_shed_player_claim(void);

/* Silences both tracks and restores exactly what claim() saved, including
 * pattern cells a source change may have cleared. No-op if never claimed. */
void fm1_shed_player_release(void);

/* Empties the event queue and sends an immediate panic (all notes off) so
 * nothing from the previous stimulus keeps ringing. */
void fm1_shed_player_silence(void);

/* Sends any queued event whose time has come. Call every control poll
 * while a track is claimed. */
void fm1_shed_player_tick(uint32_t now);

/* Schedules a ii-V-I cadence in `key` (pitch class 0..11, major) starting
 * at start_ms: m7(ii) - 7(V) - maj7(I), rootless piano voicings
 * alternating A/B/A for voice leading, bass on each chord's root. Returns
 * the time it finishes. */
uint32_t fm1_shed_player_cadence(int key, uint32_t start_ms);

/* Schedules one chord (rootless piano voicing + bass root) starting at
 * start_ms, held for most of duration_ms. Returns start_ms + duration_ms. */
uint32_t fm1_shed_player_chord(enum fm1_shed_chord_type type, int root, enum fm1_shed_voicing variant,
    uint32_t start_ms, uint32_t duration_ms);

/* Schedules a single melodic note (piano, middle register) starting at
 * start_ms, held for most of duration_ms. Returns start_ms + duration_ms. */
uint32_t fm1_shed_player_note(int pitch_class, uint32_t start_ms, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
#endif

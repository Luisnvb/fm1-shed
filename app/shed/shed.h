/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SHED_H
#define FM1_SHED_H

/* Entry point wired into app/screen.c's control loop (SHED_SPEC.md section 3
 * "Aislamiento": this is one of the few hooks allowed in an existing file).
 * Everything Shed does lives under app/shed/ and this module depends on the
 * FM-1 SDK (fm1.h, instrument.h); unlike theory.h/exercises.h it is not
 * host-testable and has no tests/shed/ coverage.
 *
 * Toggle gesture: hold HOME and tap SCL to enter Shed from the normal UI,
 * and the same HOME+SCL to leave it (SHED_SPEC.md section 3 "Entrada al
 * modo" asked for an unused HOME+X combo; neither button appears together
 * in any existing shortcut in screen.c as of V14). */

#include "fm1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Claims track 0 (piano) and track 1 (bass): saves their source, voice,
 * role, input channel, mute state and sequencer pattern, stops transport if
 * it was running, then sets both tracks up for Shed (SHED_SPEC.md "No pisar
 * el proyecto del usuario"). Starts a fresh 20-question session. */
void shed_enter(uint32_t now);

/* Silences both tracks (all notes off) and restores everything shed_enter()
 * saved, including the pattern cells a source change may have cleared and
 * transport if it was playing. Leaves the project exactly as found. */
void shed_exit(void);

/* Call once per control poll, right after fm1_controls_read(), before any
 * other handling of `controls`. Returns 1 if Shed owns this frame (the
 * caller must do nothing else this poll) or 0 if Shed is inactive and the
 * caller should run its normal control loop unchanged.
 *
 * Edge-detects the HOME+SCL gesture against its own last-seen buttons (not
 * screen.c's previous_buttons), calls shed_enter()/shed_exit() on it, ticks
 * the player's event queue and the active exercise, and redraws the screen
 * while active. After shed_exit() it keeps returning 1 until every button
 * and key is released, so screen.c's previous_keys/previous_buttons (which
 * Shed's active frames never update) resume from a clean, all-released
 * snapshot instead of one stale from whenever Shed activated. */
int shed_tick(const struct fm1_controls *controls, uint32_t now);

#ifdef __cplusplus
}
#endif
#endif

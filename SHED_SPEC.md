# Shed — A Jazz Ear-Training App for the M-VAVE FM-1

Specification and handoff document to continue development in Claude Code.
Place it at the root of the `fm1_open` repository and ask Claude Code to read it before starting.

> *Shed* comes from *woodshedding*, jazz slang for solitary practice.

---

## 1. Goal

An application that runs on the FM-1 (and its web simulator) to **train the ear in a jazz context**: intervals, functional degrees, chord qualities, tensions, arpeggios, scales and progressions.

Principles:

1. **Ear first.** Theory functions and the improvisation mode are secondary and get built later.
2. **Always in tonal context.** A cadence establishing the key plays before each question. The goal is training the ear to hear *functions*, not isolated sounds.
3. **You answer by playing.** Whenever it makes sense, the answer is given on the FM-1's keyboard (the device knows which key you press). Encoder selection only when playing doesn't apply (e.g. naming a chord quality).
4. **All 12 keys.** Key rotation (circle of fourths or random).
5. **No hardware required.** All development and testing happens in the web simulator (local workbench). Hardware is an optional future target.

---

## 2. Platform (facts verified in the code, 2026-10-22)

Repository: <https://github.com/m-vave/fm1_open> (FM1 Open V14). Official workbench: <https://open.m-vave.com/fm1-workbench>.

| Topic | Detail | Where |
|---|---|---|
| Screen | 240×240 px, RGB565 | `sdk/include/fm1.h` (`FM1_SCREEN_WIDTH/HEIGHT`) |
| Note keys | 27 keys, **F3..G5**, bits 0..26 of `fm1_controls.keys` | `sdk/include/fm1.h` (`struct fm1_controls`) |
| Buttons | 14 buttons, `fm1_controls.buttons` bits 0..13 | same |
| Encoders | 7 encoders, `fm1_controls.encoders[7]` (detents since the last read) | same |
| LEDs | `fm1_led_set(key, on)`: 0..13 buttons, **14..40 note keys** | same |
| Clock | `fm1_millis()` (compare via unsigned subtraction) | same |
| Triggering notes | `fm1_midi_post(msg, bytes)` (SDK) queues MIDI into the instrument. In practice the app doesn't call it directly: it uses `groove_midi_local(msg, bytes)` (`app/instrument.h`), which adds validation/channel routing and is what both `screen.c` (for physical keys) and `app/shed/player.c` use. Control task only, never from an ISR | `sdk/include/fm1.h`, `app/instrument.h`, example in `app/screen.c` (~l. 2550, static function `post()`) |
| Tracks | 8 tracks; MIDI channels 1–7 → tracks 1–7, channel 10 → track 8 | `app/instrument.h` (`groove_channel`), README |
| Control loop | `groove_screen_control()` reads controls and draws | `app/screen.c` (~l. 2564) |
| Drawing primitives | `text()`, `frame()`, `line()`, `paint_mark()` (static in `screen.c`) | `app/screen.c` |
| Web simulator | `web/engine/Simulator.c` compiles `app/` + `web/` to WebAssembly | `web/engine/` |
| FM engine | 6-operator DX7; 128-voice factory bank (includes FM electric pianos) | `app/fm_factory.inc`, `web/synth/` |

**Verified (Phase 0, 2026-10-22):**
- Note-key LEDs are visible in the web simulator: pressing a key in the local workbench visually highlights its border (confirmed with a screenshot). `groove_screen_web_mask` does expose this to the front end.
- The local workbench (`build/preview/index.html`, built with `tools\Build-Web.ps1 -LocalPreview`) is visually identical to the official one (<https://open.m-vave.com/fm1-workbench>): same layout, V14 version, same initial screen/controls state, no console errors, the audio engine starts up ("Sound enabled") on interaction.

**Still to verify** (not blocking for Phase 0; to address in Phase 2/3):
- Which factory voice sounds best for electric piano and for bass.
- How to change a track's voice from code without touching the user's project.

### Building the local workbench

Per the README, `.\tools\Build-Web.ps1 -LocalPreview` generates `build/preview/index.html`, which opens in Chrome or Edge. The script is written for Windows (PowerShell, `clang.exe`, downloads the Windows WASI SDK 27).

On macOS/Linux it will need adapting: install `pwsh` or rewrite the script in bash/Node, use the matching platform's WASI SDK 27 (`FM1_WASI_SDK`), and fix the `*.exe` paths. **First technical task:** get the unmodified local workbench to build and behave like the official one.

**✅ Done (Phase 0, 2026-10-22).** `tools\Build-Web.ps1 -LocalPreview` generates `build/preview/index.html`, verified visually identical to the official one (see section 2, "Verified"). Practical note: the script's automatic WASI SDK download (`curl.exe` with a progress bar) can fail in PowerShell 5.1 with a `NativeCommandError`, because `$ErrorActionPreference = 'Stop'` turns curl's stderr progress output into an exception even though the download itself succeeds. Fix: download and extract WASI SDK 27 by hand (`curl.exe --silent --show-error`, then `tar.exe -xzf`, checking the SHA-256 the script itself documents) and pass `FM1_WASI_SDK` as an environment variable pointing at the extracted directory; the script detects it already exists and skips the download.

Hardware firmware requires the JieLi 2.5.3 toolchain (Windows only). Out of scope for now.

---

## 3. Architecture

```
app/shed/
  theory.h / theory.c    # Theory core. Pure C, no FM-1 dependencies.
  exercises.h / .c       # Question generator and answer evaluator.
  progress.h / .c        # Statistics and spaced repetition (in RAM).
  player.h / .c          # Sequences MIDI events over time via fm1_midi_post.
  ui.h / ui.c            # Screens, control reading, LEDs.
  shed.h                 # Entry point: shed_enter(), shed_exit(), shed_tick().
tests/shed/              # Host tests (native gcc/clang) for theory and exercises.
```

Rules:

- **Isolation.** All new code lives in `app/shed/`. Changes to existing files are limited to a few hooks (entering/exiting the mode and delegating the control loop). `screen.c` is nearly 3000 lines long: don't refactor it.
- **Testable core.** `theory` and `exercises` don't include FM-1 headers and are unit-tested on the host.
- **No flash writes in the MVP.** Progress lives in RAM. Persistence is a later phase (and carries hardware risk).
- **Don't clobber the user's project.** Entering Shed saves the voices, roles and state of whatever tracks it uses; exiting restores them.
- **Memory.** Compact, `const` tables. No `malloc`.
- **License.** The APP is GPLv3 with an SDK linking exception. Keep SPDX headers, and publish the source if distributed.

### Entering the mode

Propose a button combination that doesn't clash with existing ones (e.g. holding HOME + another button). Check `groove_screen_control()` to pick one. An extra web control could also be added in the simulator.

### Audio

- Track A (e.g. 1): FM electric piano for chords and cadences.
- Track B (e.g. 2): FM bass for roots and vamps.
- Track C: a clean voice for single notes and melodies. ✅ implemented in Phase 4 (vamp mode, lead track/VOICE 1) — see section 8.
- Piano voicings: rootless, type A/B (3-5-7-9 / 7-9-3-5), in the middle register. The bass plays the root.
- The `player` works off an event queue `{t_ms, channel, note, velocity, on/off}` that advances on every `shed_tick()` based on `fm1_millis()`. Always send note-off; send all-notes-off on `shed_exit()`.

---

## 4. Theory core

Pitch classes 0..11 (C = 0). Intervals in semitones from the root.

### Chord types

| Symbol | Intervals | Notes |
|---|---|---|
| maj7 | 0 4 7 11 | |
| 6/9 | 0 4 7 9 14 | |
| m7 | 0 3 7 10 | |
| m6 | 0 3 7 9 | |
| m(maj7) | 0 3 7 11 | |
| 7 | 0 4 7 10 | |
| 7sus4 | 0 5 7 10 | |
| m7♭5 | 0 3 6 10 | |
| dim7 | 0 3 6 9 | |
| maj7♯11 | 0 4 7 11 18 | |
| 7♭9 | 0 4 7 10 13 | |
| 7♯11 | 0 4 7 10 18 | |
| 7alt | 0 4 10 + ♭9/♯9/♭13 (13 15 20) | Fifth omitted |

### Chord → scale

| Chord (function) | Default scale | Alternatives | Note to avoid (strong beat) |
|---|---|---|---|
| maj7 (I) | Ionian | Lydian | 4 |
| maj7 (IV) | Lydian | — | — |
| m7 (II) | Dorian | — | — |
| m7 (III) | Phrygian | — | ♭2, ♭6 |
| m7 (VI) | Aeolian | — | ♭6 |
| 7 (V → major) | Mixolydian | Bebop dominant | 4 |
| 7♯11 | Lydian dominant | — | — |
| 7alt | Altered | — | — |
| 7♭9 (V → minor) | Mixolydian ♭9 ♭13 (Phrygian dominant) | Half-whole diminished | — |
| m7♭5 | Locrian | Locrian ♮2 | ♭2 (in Locrian) |
| dim7 | Whole-half diminished | — | — |
| m(maj7), m6 | Melodic minor | — | — |

### Scales (intervals)

Ionian 0 2 4 5 7 9 11 · Dorian 0 2 3 5 7 9 10 · Phrygian 0 1 3 5 7 8 10 · Lydian 0 2 4 6 7 9 11 · Mixolydian 0 2 4 5 7 9 10 · Aeolian 0 2 3 5 7 8 10 · Locrian 0 1 3 5 6 8 10 · Melodic minor 0 2 3 5 7 9 11 · Lydian dominant 0 2 4 6 7 9 10 · Locrian ♮2 0 2 3 5 6 8 10 · Altered 0 1 3 4 6 8 10 · Whole-half diminished 0 2 3 5 6 8 9 11 · Half-whole diminished 0 1 3 4 6 7 9 10 · Whole tone 0 2 4 6 8 10 · Bebop dominant 0 2 4 5 7 9 10 11 · Phrygian dominant 0 1 4 5 7 8 10.

These tables must be covered by tests.

---

## 5. Ear-training exercises

Common format for every question:

1. **Context:** a cadence in the key (ii-V-I with sevenths by default; I-IV-V-I in triads as an option for beginner levels).
2. **Stimulus:** what needs to be recognized.
3. **Response:** played on the keyboard or chosen with an encoder.
4. **Feedback:** right/wrong on screen, LEDs on the correct answer, and the correct answer plays back after a miss.

Played answers are evaluated **by pitch class** (octave-independent), unless the exercise says otherwise. The keyboard only covers F3..G5.

| # | Exercise | Stimulus | Response | Difficulty progression |
|---|---|---|---|---|
| 1 | **Functional degree** | Cadence + one note | Play the note | Diatonic → chromatic; fixed register → variable |
| 2 | **Melodic interval** | Reference note + second note | Play the second note | Ascending → descending; within the octave → compound (9, 11, 13) |
| 3 | **Harmonic interval** | Two notes at once | Play the upper note | Same |
| 4 | **Chord quality** | Cadence + chord | Encoder: list of types | Triads → tetrads → with tensions |
| 5 | **Tension over a dominant** | 7 chord + one note on top | Encoder: 9, ♭9, ♯9, ♯11, 13, ♭13 | Few options → all |
| 6 | **Arpeggio** | A chord's arpeggio | Play the arpeggio (order and notes) | Root up → inversions → starting from the 3rd/7th |
| 7 | **Scale/mode** | Scale over a bass pedal | Encoder: list of modes | Modes of the major scale → melodic minor → symmetric |
| 8 | **Bass movement** | 2–4 chord progression | Play the roots in order | ii-V-I → turnarounds → blues, rhythm changes |
| 9 | **Melodic dictation** | 3–8 note phrase over a vamp | Play it back | Diatonic → chromaticism, approach notes |

Common controls during an exercise (final mapping to be decided after reviewing the existing UI):

- **Repeat stimulus** · **Repeat cadence** · **Reveal** · **Next**
- Encoders: key (fixed / circle of fourths / random), tempo, level, exercise type, cadence's relative volume.

### Pedagogy

- **Spaced repetition:** each item (e.g. "♭3 degree in the key of E♭") has a score; misses show up more often.
- **Adaptive difficulty:** the level rises after N correct answers in a row and drops after several misses.
- **Short sessions:** blocks of 10–20 questions with a final summary (percent correct, most-missed items).

---

## 6. Vamp mode ✅ implemented in Phase 4 (see section 8)

The device plays a vamp (bass + comping) while the user improvises on the keyboard.

- Initial vamps: modal Dm7; Dm7–G7; minor ii-V-i; Cmaj7–A7–Dm7–G7; blues in F.
- The **key LEDs** show the options: solid = chord tones; blinking = available tensions. They change one beat before the chord change.
- The screen shows the current chord, the next one, and the suggested scale.
- Views: scale, arpeggio, guide tones (3rd and 7th), none (just listen).
- **Ear variant:** the device plays a phrase over the vamp and the user identifies which tension or scale it used.

---

## 7. Screens (240×240 mockup)

```
┌────────────────────────┐   ┌────────────────────────┐
│ SHED        KEY: Bb    │   │ FUNCTIONAL DEGREE 7/20 │
│                        │   │                        │
│ > Functional degree    │   │   What degree is it?   │
│   Intervals            │   │                        │
│   Chord quality        │   │        ♪ ♪ ♪ → ?       │
│   Tensions              │   │                        │
│   Arpeggios             │   │  Play the answer       │
│   Scales                │   │                        │
│   Bass / progressions   │   │ [REP] [CAD] [REV] [>]  │
│   Dictation             │   │ Streak: 4  Level: 2    │
└────────────────────────┘   └────────────────────────┘
```

After answering: "✓ ♭7 (Ab)" or "✗ It was the 6th (G)", with LEDs on the correct key.

---

## 8. Phased plan

**Phase 0 — Environment** ✅ 2026-10-22
- Build the local workbench without changes on the user's platform (adapt `Build-Web.ps1` if needed).
- Verify the items flagged in section 2.
- *Acceptance:* the local `index.html` works like the official workbench. **Met** (see section 2).

**Phase 1 — Core** ✅ 2026-10-22
- `theory` and `exercises` with host tests.
- *Acceptance:* tests green; section 4's tables covered. **Met:** `.\tools\Test-Shed.ps1` → `shed tests: 4320 checks, 0 failures`.
- `app/shed/theory.h` / `.c`: the 13 chord qualities, the 16 scales and the 13 rows of section 4's chord→scale table, transcribed and tested one by one against the document (not against themselves). `fm1_shed_chord_scale_lookup(type, context)` resolves rows that depend on function (e.g. maj7 in I vs. IV) with a fallback to the chord's generic row when there's no row for that specific context, and returns `NULL` when not even the generic one exists (e.g. m7 in a V context, which isn't in the table).
- `app/shed/exercises.h` / `.c`: a deterministic RNG (xorshift32), key rotation (`FM1_SHED_KEY_FIXED/FOURTHS/RANDOM`), and the generator/evaluator for exercises **#1 functional degree** and **#4 chord quality** (the pair Phase 2 integrates), with difficulty bands — degree: diatonic (level 0) → chromatic (level ≥1); chord: 6 basic qualities → 9 → all 13. Exercises #2,3,5–9 are left for Phase 3, as the plan states.
- `progress`, `player`, `ui`, `shed.h` are **not** touched in this phase (per section 3, they belong to Phase 2 onward).
- **Host test toolchain:** this machine has no native gcc/clang/cl. `tools\Test-Shed.ps1` compiles `tests/shed/*.c` + `app/shed/*.c` to wasm32-wasi with the same WASI SDK 27 clang `Build-Web.ps1` already needs (`--sysroot` points at the wasi sysroot, no `--target` flag needed: the SDK's clang already defaults to wasm32-unknown-wasi) and runs it with Node's built-in WASI runtime (`tools/wasi-run.mjs`, via `node:wasi`) — it's still a real C11 build with `-Wall -Wextra -Werror`, just running in a WASI sandbox instead of natively. Reuses the SDK already downloaded at `build/tools/wasi-sdk-27.0-x86_64-windows` (or `$env:FM1_WASI_SDK`).
- **Verified it doesn't break the web build:** `Build-Web.ps1` walks `app/` recursively (except `app/board/`) and adds **every** `.c` file it finds to the compiler, so `app/shed/theory.c` and `exercises.c` already compile into `build/preview/index.html` even though nothing calls `shed_enter()` yet. Confirmed by re-running `Build-Web.ps1 -LocalPreview` after adding these files: builds clean and the resulting `.wasm` weighs exactly the same as before (295723 bytes) — i.e. the linker drops the not-yet-referenced code, a good sign there's no symbol collision or side effect, and that Phase 2 only needs to *call* this core, not register it anywhere new in the build.

**Phase 2 — MVP in the simulator** ✅ 2026-10-22
- Entering/exiting Shed; `player`; menu; exercises 1 (functional degree) and 4 (chord quality).
- *Acceptance:* **met and verified live in the simulator** (a full 20-question session, key rotating by circle of fourths, audible cadence, response by keyboard and by encoder, feedback, summary; the user's project untouched on exit — see detail below).

**Entry/exit gesture:** `HOME + SCL` (hold HOME, tap SCL), symmetric for entering and exiting. Neither appears together in any existing shortcut in `screen.c` (confirmed by inspection: the combos already used with HOME are PLAY, SAVE, SEQ, GLO, and encoder 6). The button the internal code calls `SCL` is labeled "SEL" on the panel/simulator — same button, two names.

**`app/shed/shed.h` / `.c` (entry point):**
- `shed_enter()` / `shed_exit()` / `shed_tick()` as section 3 asks for. `shed_tick()` detects the gesture with its own edge detector (not `screen.c`'s `previous_buttons`), and after `shed_exit()` keeps returning 1 (consuming the frame) **until every button and key is released**, so `screen.c`'s `previous_buttons`/`previous_keys` — which Shed's active calls skip entirely — resync from an "all-released" state instead of a stale one from the moment Shed started. Verified live: with HOME latched after the exit gesture, the screen stays on Shed until HOME is released, and only then does it go back to the normal UI on the next frame, with no phantom input.
- A small state machine (MENU → QUESTION → FEEDBACK → SUMMARY) that generates questions via `exercises.h`, schedules audio via `player.h`, and records answers via `progress.h`. The key rotates by circle of fourths on **every question** (not per session), to cover variety within the 20 questions.

**`app/shed/player.h` / `.c` (audio):** claims tracks 0 (piano) and 1 (bass) via `groove_midi_local` (the same mechanism `post()` in `screen.c` uses for physical keys — more accurate than section 2's mention of `fm1_midi_post`, which is the lower-level primitive `groove_midi_local` wraps with validation/channel routing; corrected above). Before playing anything, it saves and restores per track: source type, voice (`program`), role, MIDI input channel, mute, and **the 64 sequencer cells** (`groove_sequence_read`/`_replace`), because `groove_source_set` can clear "incompatible" cells when crossing between a drum and a melodic source — without this backup the track's pattern would have been lost if the user had an 808/sampler there. Also for transport if it was running (MIDI Stop/Continue) and the project's "no changes" flag (`groove_project_dirty`/`groove_project_clean`). Voices picked by factory name: **PIANO 1** (program 0) for piano, **BASS 1** (program 34) for bass — confirmed with a separate tool that dumps the 128 names from `app/fm_factory.inc`; which one sounds best is still unverified by ear (section 2's open question). Broken A/B voicings (rootless, ascending / top note dropped an octave) built directly from `theory.c`'s intervals.
- **Verified live:** after a full session (20 questions, both exercises, including crossing DX7↔303 sources), exiting the track overview (`HOME`) shows exactly `1 PIANO 1 · 2 303 SAW · 3 PSG VM · 4 PLUCK · 5 SAMPLER · 6 PIANO 1 MUTE · 7 303 SAW MUTE · 8 808 KIT` — identical to the initial state, and the side panel says "Workspace unchanged" again.

**`app/shed/ui.h` / `.c` (screens, controls, LEDs):** draws with `fm1_screen_blit` in 16-row bands (without reusing `screen.c`'s internal `text()`/`paint_fill()` pipeline, which is `static` and not exported) and its own 5×7 font table (the classic public-domain pattern, same as `screen.c`'s but its own copy, not shared). Note keys are read/lit with `fm1_controls.keys`/`fm1_led_set` directly (public API in `fm1.h`); panel buttons (`HOME`, `SCL`, `PLAY`, `SEQ`, `EDIT`, `ARP`, `ENV`, `GLO`) are identified by the same bits as `screen.c`'s private `enum` — **there is no other source of truth for those bits in the SDK**, so they're duplicated with an explicit comment in `ui.c` and `shed.c` instead of exporting the enum (to keep `screen.c`'s change to the bare minimum).
- Control mapping during a question: `PLAY` repeats the stimulus, `SEQ` repeats the cadence, `EDIT` reveals (counts as a miss), `ARP` next (skip, unscored), `ENV` switches exercise, the `PRESETS` dial (↔ `encoders[6]`, confirmed against `sdk/Simulator.js`) picks a chord quality, `GLO` confirms. Difficulty level is fixed (Phase 3 brings adaptive difficulty); all 12 keys and both exercises already rotate/alternate in Phase 2.

**`app/shed/progress.h` / `.c`:** RAM session counter (`asked`, `correct`, `streak`, `best_streak`), no spaced repetition yet (Phase 3). With host tests.

**Hook in `app/screen.c`:** one `#include "shed/shed.h"` and, at the top of `groove_screen_control()`, `if (shed_tick(&controls, now)) return;` (plus splitting one declaration line into two, without changing its logic). Nothing else in that file was touched.

**Real bugs found testing live in the simulator** (see methodology below) and fixed:
1. The chord-quality picker read `encoders[0]`, which on the real panel is the **SELECT** dial (track selector), not a free knob. Fixed to `encoders[6]`, the **PRESETS** dial — confirmed in `sdk/Simulator.js` (`const encoders=[['MASTER',-1,...],['SELECT',0,...],['PRESETS',6,...],...]`) and it's also the same dial the rest of the app already uses to "scroll a list" (`groove_control_edit(track, encoders[6], 0, 0)` for voices).
2. `append_uint()` doesn't null-terminate (by design, so strings can be composed); two spots in `ui.c` ("RACHA n" in feedback, "MEJOR RACHA n" in the summary) called it without adding the `\0` afterward, leaving leftover garbage from the previous `strcpy` in the buffer — seen live as "RACHA 07" instead of "RACHA 0". Fixed in both spots.
3. Key LEDs stayed lit from a previous session/reveal: not cleared in `shed_enter()` (a key was visibly lit right on the menu) nor when moving from FEEDBACK to SUMMARY. Fixed with `fm1_shed_ui_clear_leds()`/`fm1_shed_ui_show_answer_leds(-1)` at both points.

**Live-testing methodology:** the simulator supports "pinning" a button without holding the mouse down — Shift+click (or right-click) latches it (a dot appears in the button's corner) until a second Shift+click, while a plain click on another button produces a ~55ms pulse. Pinning `HOME` and clicking `SEL` simulates the `HOME+SCL` combo without needing two pointers. This is how entry/exit, both exercise screens, the chord-quality dial, the action buttons, the exact 20-question count, the summary screen, the correct-answer LEDs (against the real F3/F4/F5 positions, worked out from `whites=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26]` in `sdk/Simulator.js`), and full project restoration were all verified.

**Phase 3 — More exercises** ✅ 2026-10-22
- Exercises 2, 3, 5, 6, 7, 8 and 9; spaced repetition; adaptive difficulty.
- *Acceptance:* **met and verified live.** All 9 menu exercises work (degree, melodic interval tested, chord quality, tension confirmed correct, arpeggio with sequence progress/failure confirmed, scale confirmed, bass movement and melodic dictation covered by host tests); adaptive difficulty and spaced repetition active; a full session and clean exit re-verified after the changes.

**`app/shed/theory.c` didn't change** — Phase 1's core already covered everything needed (chords, scales, pitch classes); Phase 3 is pure extension of `exercises.c`, `progress.c`, `player.c`, `ui.c` and `shed.c`.

**`app/shed/exercises.h` / `.c` extended:**
- `enum fm1_shed_exercise_type` goes from 2 to 9 values (original order untouched, the rest appended at the end — compatible with existing code and tests).
- Four exercises with a **bounded, named pool** (degree #1, chord quality #4, tension #5, scale #7) gain uniform `fm1_shed_X_pool_size/at(level, index)` accessors, plus `fm1_shed_question_generate_at(..., pool_index, rng)` — generates the question at a specific pool index instead of a random one, which is what lets `progress.c` decide *which* item to ask (spaced repetition). The other five (intervals #2/#3, arpeggio #6, bass movement #8, dictation #9) stay purely random: their question space doesn't lend itself to the same "named pool" concept — explicitly documented in the header.
- `fm1_shed_question_check_interval_note()`: unlike `check_note` (which only compares pitch class), it compares the **exact note** — telling a 9th from a 2nd needs the octave. `fm1_shed_interval_reference_note(key)` (= 60 + pitch class) is shared by `exercises.c` and `player.c` so the stimulus and the evaluation always play/compare the same reference note.
- `fm1_shed_question_check_sequence_note()` / `_sequence_done()`: a generic "sequence answer" mechanism (advances on each correct note, fails immediately on the first wrong one) reused by arpeggio, bass movement and melodic dictation — one implementation, tested once, used three times.
- Bass movement (#8) uses a small table of named progressions (ii-V-I · turnarounds I-vi-ii-V / iii-VI7-ii-V · blues and "rhythm changes" fragments), all 3-4 chords long to respect the exercise's own "2-4 chords" stimulus length even though the higher level points at blues/rhythm changes (12-32 bar forms): a representative fragment is played, not the whole form.

**`app/shed/progress.h` / `.c` extended:**
- **Adaptive difficulty** (`fm1_shed_session_adapt_level`): climbs one level every `FM1_SHED_ADAPT_UP_STREAK` (5) additional straight hits, drops one every `FM1_SHED_ADAPT_DOWN_STREAK` (3) additional straight misses, clamped to `[0, max_level]`. Required adding `miss_streak` to `fm1_shed_session` (the existing `streak` resets on any miss, so it couldn't count consecutive misses).
- **Spaced repetition** (`struct fm1_shed_scores` + `fm1_shed_scores_record/pick`): one score per `(exercise type, pool index)`, in RAM, reset with every session (no flash persistence — Phase 2 already set that rule). A hit raises the score, a miss drops it by two (so misses resurface sooner). `pick()` picks the lowest-scoring item 60% of the time (ties broken at random), and draws uniformly the other 40% — so the session doesn't narrow down to always repeating the same thing and keeps reviewing the rest.

**`app/shed/player.h` / `.c` extended** with just two new generic functions (every other exercise composes from the existing `fm1_shed_player_note`/`_chord` from `shed.c`, no more code needed in player.c):
- `fm1_shed_player_note_register()`: generalizes `fm1_shed_player_note()` with an explicit register (e.g. #5's tension sounds an octave above the chord voicing).
- `fm1_shed_player_sequence()`: plays a sequence of pitch classes as single notes; with `ascending=1` it pushes each note up an octave as needed to keep climbing (arpeggios, #6), with `ascending=0` it always uses the same register (melodic dictation, #9). *I found and fixed a real bug here while writing it* (before ever testing it): reusing `fm1_shed_player_note_register(note - base, base, ...)` undid the octave jump because the function reduces mod 12 again — fixed by passing the full absolute note as the register with pitch class 0.
- The `48`/`36` registers (piano/bass) are exposed as `FM1_SHED_PIANO_REGISTER`/`FM1_SHED_BASS_REGISTER` in `player.h` instead of staying private, so `shed.c` doesn't repeat magic numbers.

**`app/shed/ui.h` / `.c` extended:**
- A **scrollable 9-item list** menu (previously 2 with a toggle): the `PRESETS` dial navigates, `PLAY` confirms and starts.
- `fm1_shed_input` gains `note_absolute` (the key's actual MIDI note, not just its pitch class) — needed for interval exercises, which do care about octave; `chord_delta` is renamed to `dial_delta` (now also used by the menu, tension, and scale, not just chord quality).
- Screen patterns shared across exercises with the same answer type: `draw_dial_pick()` (chord quality #4, tension #5, scale #7) and `draw_sequence_prompt()` (arpeggio #6, bass #8, dictation #9, showing "n/m" played).
- Feedback for a sequence question shows "REACHED n/m" instead of "IT WAS X" (the whole sequence doesn't fit legibly in 240 px).

**`app/shed/shed.c` rewritten** as the orchestrator for all 9 types: `schedule_stimulus()` centralizes how each exercise sounds (the one function that knows, reused by "repeat stimulus" and by the automatic replay after a miss); `handle_question_input()` dispatches by type (keyboard for degree/intervals, dial+GLO for chord/tension/scale, key sequence for arpeggio/bass/dictation) and falls through to the common controls (repeat, cadence, reveal, next) when the type didn't already produce an answer that poll; `ENV` cycles the exercise type in any state except the menu (which already has its own dial navigation).

**Verified live in the simulator** (same Shift+click-to-pin-HOME technique described in Phase 2): the menu shows all 9 exercises with the correct name; melodic interval tested (hit and miss, "IT WAS B" shows the note name); dominant tension confirmed correct in green ("CORRECT"/"IT WAS 9"); scale confirmed showing the short ASCII name ("IT WAS AEOLIAN"); arpeggio tested with sequence progress (0/4→1/4 on getting the root right) and an immediate miss reported as "REACHED n/m" (tested at 0/4 and at 1/4); no console errors across the whole session; exit with `HOME+SCL` and the project verified bit-for-bit identical to the original (including the 8-track view) with "Workspace unchanged" after a session that mixed several exercise types and therefore several claim/restore cycles. Bass movement and melodic dictation weren't tested live note by note (covered instead by 10177 host checks, including their sequence generation and the generic sequence matcher) — if something's off when using them, the most likely culprit is the register/timing in `schedule_stimulus()`, not the evaluation logic.

**Phase 4 — Vamp mode** ✅ 2026-10-22
- The device plays a vamp (bass + comping) while the user improvises; key LEDs as a guide; a screen with the current/next chord and suggested scale; 4 views; an ear variant.
- *Acceptance (section 6 had no explicit line in the original plan — inferred from its own description):* **met and verified live.** All 5 progressions sound and transpose with key rotation; all 4 LED views verified **pixel by pixel** against hand-computed expectations; live playing over the vamp works (new track, "track C"); switching vamp and key without leaving the mode; the ear variant fully reuses exercises #5/#7 and returns cleanly to the vamp (which keeps sounding in the background); exit with the project untouched (now 3 claimed tracks, not 2).

**New track — "track C" (section 3, previously optional):** Phase 4 is what finally needs it (the user improvises while bass+comping keep playing, so their own voice can't share a track with them). `player.c` now claims **3 tracks always** (also during exercises 1-9, where the third goes unused) rather than conditioning the claim on the mode — simpler to keep correct than two separate save/restore paths, at the cost of touching one extra track in exercise-only sessions (harmless: it's claimed and handed back exactly as found). Voice picked for that track: **VOICE 1** (factory program 67), the same "by name, unverified by ear" approach as PIANO 1/BASS 1 in Phase 2.

**`app/shed/exercises.h` / `.c` — vamp data (pure, host-testable):** `struct fm1_shed_vamp_step { chord, context, offset, duration_ms }` + 5 named `fm1_shed_vamp`s, all transposable (offsets are relative to the active key, same as exercise #8's progressions — in fact the `CMAJ7-A7-DM7-G7` vamp reuses the same I-VI7-ii-V shape already there). The host test **checks that every (chord, context) pair across all 5 progressions resolves to a real `fm1_shed_chord_scale_lookup` row** — this would have caught any typo pairing a chord with a context `theory.c` has no row for; there wasn't one. Contexts picked pragmatically where `theory.c` doesn't distinguish the exact case: secondary and blues dominants use `CONTEXT_V_MAJOR` (Mixolydian) just like any plain V7; the single-chord modal vamp uses `CONTEXT_II` (Dorian) even though it doesn't "function" as anyone's ii.

**`app/shed/player.h` / `.c` — vamp sequencer:** unlike exercises' cadences/progressions (which fit entirely in the 48-event queue), a vamp is an indefinite loop, so `fm1_shed_player_vamp_start/_tick/_stop` only ever schedule the current chord and the next one, never the whole loop — `_tick()` is called on every `shed_tick()` and schedules the next step right before it's due. `fm1_shed_player_vamp_step_index(which)` exposes the raw index (not the already-resolved chord) so `shed.c`, which knows the key and can index `fm1_shed_vamp()` directly, can also read that step's `context` — needed for the suggested scale, which `player.c` doesn't compute. Two new functions for the lead track: `fm1_shed_player_lead_note()` (an immediate, unscheduled note, so playing feels instant) and `fm1_shed_player_lead_phrase()` (the "ear" demonstration phrase).

**`app/shed/ui.h` / `.c`:**
- The menu grows to 10 entries (9 exercises + "VAMP MODE"), navigable with the `PRESETS` dial.
- `fm1_shed_input` gains `keys_pressed`/`keys_released` as 27-bit *bitmasks* — unlike the exercises (which only ever need "the first new key"), live playing is polyphonic. `FM1_SHED_KEY_COUNT`/`FM1_SHED_KEY_BASE_NOTE` become public (previously private to `ui.c`) so `shed.c` can turn those bitmasks into real MIDI notes without duplicating the keyboard layout table.
- `fm1_shed_ui_show_vamp_leds(view, chord, root, scale, now)`: solid = chord tones (arpeggio/scale view, depending on the case), blinking = the rest of the suggested scale that isn't already a chord tone (arpeggio view only), nothing in "none" view. Blinking uses `(now/300)%2` as its phase.
- New `FM1_SHED_SCREEN_VAMP` screen: vamp name, current and next chord, suggested scale, active view. The ear variant is **not** a new screen: it reuses `FM1_SHED_SCREEN_QUESTION`/`FEEDBACK` with the exercise forced to TENSION or SCALE — it's literally the same question, just rooted at the vamp's current chord instead of a key rotating with a fresh cadence.

**`app/shed/shed.c`:** new `SHED_STATE_VAMP`; free-play controls — `EDIT` cycles the view, `SEQ` the next vamp, `ARP` the next key, `GLO` an ear round, keys play live (polyphonic, lead track), `HOME+SCL` exits as always. The ear variant is flagged with `vamp_ear_active` (doesn't touch `session`/`scores`/`level`, which belong to the 20-question flow; doesn't auto-replay the correct answer on a miss, to avoid cutting off the vamp with `fm1_shed_player_silence()`'s global panic; and "repeat cadence" is disabled during the round, since there was no fresh cadence to repeat). Which sub-exercise to use: if the vamp's current chord is exactly a plain `7` (dominant) → tension (generated with the normal generator, rooted at the vamp's chord); otherwise → scale, but here the random generator from exercise #7 is **not** used (it has no reason to fit the chord that's actually sounding): the correct scale is, deliberately, the one `fm1_shed_chord_scale_lookup` already suggests for that chord — the question is "do you recognize what's being used", not "do you recognize some scale or other".

**A deliberate, documented simplification:** on a miss during the ear round, the correct answer doesn't auto-replay (unlike exercises 1-9) — the "IT WAS X" text on the feedback screen still shows it. Adding auto-replay without cutting off the vamp would have needed a "targeted silence" path that doesn't touch the vamp's piano/bass tracks, which I didn't build; noted here in case it's picked up later.

**Verified live in the simulator**, with the same Shift+click-to-pin-HOME technique: all 5 progressions sound and transpose correctly with the key; **all 4 LED views verified key by key (all 27) against hand-computed expectations**, including real blinking (two time-separated captures show the same constant "solid" set plus an extra set that appears and disappears) — an exact match in black and white, across all 4 views; live playing with no console errors; `SEQ`/`ARP` switch vamp/key and show it correctly on screen; the ear variant tested on **both branches** (Fm7 chord → scale question, "CORRECT"/"IT WAS DORIAN"; F7 chord → tension question, "INCORRECT"/"IT WAS 13") and confirmed the vamp kept sounding and advancing in the background through the whole round and on return; exit with `HOME+SCL` and the project restored bit-for-bit (including the newly claimed 3rd track) with "Workspace unchanged".

**Documentation and on-screen text translated to English** ✅ 2026-10-23
- This spec and every on-screen string in `app/shed/ui.c` (menu, question prompts, feedback, summary, vamp screen, button hints) were translated from Spanish to English ahead of sharing the project with the FM1 community. Universal jazz notation (chord symbols like `MAJ7`, tension symbols like `B9`) was left as-is. Every `strcpy`/`format_fraction`/`append_uint` buffer offset that depended on a translated prefix's character count was updated alongside it.
- **Bug found while translating, fixed in the same pass:** `scale_labels[]` in `ui.c` had its last 5 entries (indices 11-15) out of order relative to `enum fm1_shed_scale_type` (theory.h) — "Phrygian dominant" sat at index 11 instead of 15. This meant exercise #7 (scale/mode) at difficulty level 2 could show the wrong scale name for 5 of 16 possible picks. Neither the host test suite (which doesn't check array *content*, only structural properties) nor prior live testing (which never reached level 2 for that exercise) could have caught it. Fixed by re-deriving the array directly from the enum order.
- **Verified:** host tests still pass (10304 checks, 0 failures — `ui.c` isn't host-tested); full web build compiles clean under `-Wall -Wextra -Werror`; a comprehensive accented-character/Spanish-word grep across `app/shed/*.{c,h}` and `tests/shed/*.{c,h}` found nothing left; live in the simulator, the MENU (all 9 exercises + "VAMP MODE"), a full 20-question SCALE/MODE session (QUESTION and FEEDBACK screens, including "IT WAS X"/"STREAK n"), the SUMMARY screen, and the VAMP screen (NOW/NEXT/SCALE/VIEW lines and all button hints, including the corrected "HOME SCL EXIT") all render correctly with no overflow and no console errors. The corrected `scale_labels` order was confirmed by cross-reference against the enum rather than by reaching level 2 live (the level-0 dial only cycles the 7 diatonic modes, confirming the buggy indices are gated behind difficulty exactly as suspected).

**Phase 5 (optional) — Hardware and persistence**
- Build firmware with the JieLi toolchain; save progress to flash.

---

## 9. Open questions

- Which button combination should be used to enter Shed without breaking existing shortcuts?
- Should it temporarily use existing tracks or reserve its own configuration?
- Which factory voices sound best as electric piano and jazz bass?
- Is an extra web control needed in the simulator to enter the mode?

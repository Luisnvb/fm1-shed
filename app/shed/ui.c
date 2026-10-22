/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "ui.h"
#include <string.h>

/* Physical button bit positions. fm1.h documents only "buttons 0..13"; the
 * actual per-button identity is app/screen.c's own private
 * `enum { OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP,
 * SEQ, PLAY, REC }` (screen.c, just above groove_screen_control), which is
 * not exported -- there is no other place in this codebase that names
 * these bits. Duplicated here rather than exporting it, to keep this
 * module's only change to screen.c the two-line shed_tick() hook. Keep in
 * sync with that enum if it ever moves. */
enum {
    BTN_SCL  = 1U << 3,
    BTN_ENV  = 1U << 4,
    BTN_EDIT = 1U << 6,
    BTN_GLO  = 1U << 7,
    BTN_HOME = 1U << 8,
    BTN_ARP  = 1U << 10,
    BTN_SEQ  = 1U << 11,
    BTN_PLAY = 1U << 12
};

/* fm1.h: "keys; bits 0..26: F3..G5". F3 is MIDI note 53 (C4 = 60). */
enum { KEY_COUNT = 27, KEY_BASE_NOTE = 53, LED_KEY_OFFSET = 14 };

enum { BACK = 0x0000, PANEL = 0x18e3, TEXT = 0xef5d, ACCENT = 0x07ff, DIM = 0x738e,
       GREEN = 0x07e0, RED = 0xf800, AMBER = 0xfd20 };

static const char *const pitch_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
/* ASCII-safe labels (this font has no flat/sharp glyphs): indexed the same
 * as enum fm1_shed_chord_type in theory.h. */
static const char *const chord_labels[FM1_SHED_CHORD_COUNT] = {
    "MAJ7", "6/9", "M7", "M6", "M(MAJ7)", "7", "7SUS4",
    "M7B5", "DIM7", "MAJ7#11", "7B9", "7#11", "7ALT"
};
static const char *const exercise_names[FM1_SHED_EX_TYPE_COUNT] = {
    "GRADO FUNCIONAL", "CALIDAD ACORDE"
};

/* Standard public-domain 5x7 glyph shapes (the same ones countless open
 * embedded projects ship, e.g. the classic Sondaar font) -- digits then
 * uppercase letters, five columns per glyph, bit 0 at the top. */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
};
/* A handful of symbols beyond digits/letters: chord labels and the summary
 * screen need '#', '/', '(', ')', '%', '.', '-', ':'. */
static const uint8_t punctuation[][6] = {
    {'#', 0x14, 0x7f, 0x14, 0x7f, 0x14}, {'/', 0x60, 0x18, 0x06, 0x01, 0},
    {'(', 0, 0x1c, 0x22, 0x41, 0},       {')', 0, 0x41, 0x22, 0x1c, 0},
    {'%', 0x63, 0x13, 0x08, 0x64, 0x63}, {'.', 0, 0x60, 0x60, 0, 0},
    {'-', 0x08, 0x08, 0x08, 0x08, 0x08}, {':', 0, 0x36, 0x36, 0, 0}
};

enum { BAND_ROWS = 16, BAND_COUNT = FM1_SCREEN_HEIGHT / BAND_ROWS };
static uint16_t band[FM1_SCREEN_WIDTH * BAND_ROWS];

static void rect(unsigned band_top, unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    unsigned row, col, first, end;
    if (!w || !h || x >= FM1_SCREEN_WIDTH || y + h <= band_top || y >= band_top + BAND_ROWS)
        return;
    if (x + w > FM1_SCREEN_WIDTH)
        w = FM1_SCREEN_WIDTH - x;
    first = y < band_top ? band_top : y;
    end = y + h < band_top + BAND_ROWS ? y + h : band_top + BAND_ROWS;
    for (row = first; row < end; row++)
        for (col = x; col < x + w; col++)
            band[(row - band_top) * FM1_SCREEN_WIDTH + col] = color;
}

static const uint8_t *glyph(char c)
{
    static const uint8_t blank[5] = {0};
    unsigned i;
    char letter = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    if (c >= '0' && c <= '9')
        return font[c - '0'];
    if (letter >= 'A' && letter <= 'Z')
        return font[10 + letter - 'A'];
    for (i = 0; i < sizeof punctuation / sizeof punctuation[0]; i++)
        if ((uint8_t)c == punctuation[i][0])
            return punctuation[i] + 1;
    return blank;
}

/* scale 1..4, 6*scale px wide and 7*scale px tall per character. */
static void put_text(unsigned band_top, unsigned x, unsigned y, const char *s, unsigned scale,
    uint16_t color, uint16_t background)
{
    unsigned w = 6U * scale, h = 7U * scale;
    if (!s || !scale || x >= FM1_SCREEN_WIDTH || y + h <= band_top || y >= band_top + BAND_ROWS)
        return;
    for (; *s && x + w <= FM1_SCREEN_WIDTH; s++, x += w) {
        const uint8_t *shape = glyph(*s);
        unsigned first = y < band_top ? band_top : y;
        unsigned end = y + h < band_top + BAND_ROWS ? y + h : band_top + BAND_ROWS;
        unsigned row, col;
        for (row = first; row < end; row++) {
            unsigned mask = 1U << ((row - y) / scale);
            for (col = x; col < x + w; col++) {
                unsigned column = (col - x) / scale;
                band[(row - band_top) * FM1_SCREEN_WIDTH + col] =
                    column < 5U && (shape[column] & mask) ? color : background;
            }
        }
    }
}

static unsigned text_width(const char *s, unsigned scale)
{
    return s ? (unsigned)strlen(s) * 6U * scale : 0;
}

static uint32_t last_draw_ms = ~0U;
static uint32_t previous_keys;
static unsigned previous_buttons;

void fm1_shed_ui_reset(const struct fm1_controls *controls)
{
    previous_keys = controls ? controls->keys : 0;
    previous_buttons = controls ? controls->buttons : 0;
    last_draw_ms = ~0U;
}

void fm1_shed_ui_read(const struct fm1_controls *controls, struct fm1_shed_input *input)
{
    uint32_t key_down;
    unsigned button_down, i;
    input->note = -1;
    input->action = 0;
    input->chord_delta = 0;
    if (!controls)
        return;
    key_down = controls->keys & ~previous_keys;
    button_down = controls->buttons & ~previous_buttons;
    for (i = 0; i < KEY_COUNT; i++)
        if (key_down & (1U << i)) {
            input->note = (int)((KEY_BASE_NOTE + i) % 12U);
            break;
        }
    if (button_down & BTN_PLAY) input->action |= FM1_SHED_ACTION_REPEAT_STIMULUS;
    if (button_down & BTN_SEQ)  input->action |= FM1_SHED_ACTION_REPEAT_CADENCE;
    if (button_down & BTN_EDIT) input->action |= FM1_SHED_ACTION_REVEAL;
    if (button_down & BTN_ARP)  input->action |= FM1_SHED_ACTION_NEXT;
    if (button_down & BTN_ENV)  input->action |= FM1_SHED_ACTION_TOGGLE_TYPE;
    if (button_down & BTN_GLO)  input->action |= FM1_SHED_ACTION_CONFIRM;
    input->chord_delta = controls->encoders[6]; /* PRESETS dial: same "scroll a list" role as elsewhere in the app */
    previous_keys = controls->keys;
    previous_buttons = controls->buttons;
}

static unsigned append_uint(char *buf, unsigned pos, unsigned value)
{
    char digits[4];
    unsigned n = 0;
    if (!value)
        digits[n++] = '0';
    while (value) {
        digits[n++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (n)
        buf[pos++] = digits[--n];
    return pos;
}

static void format_fraction(char *buf, unsigned a, unsigned b)
{
    unsigned pos = append_uint(buf, 0, a);
    buf[pos++] = '/';
    pos = append_uint(buf, pos, b);
    buf[pos] = 0;
}

/* Draws every element of `state`'s screen that intersects band [band_top,
 * band_top + BAND_ROWS); called once per band, each time redrawing the
 * whole logical screen (no dirty-rect tracking -- Shed's screen changes a
 * few times a second at most, so this stays cheap enough without it). */
static void draw_screen(const struct fm1_shed_ui_state *state, unsigned band_top)
{
    char line[24];
    rect(band_top, 0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, BACK);
    put_text(band_top, 6, 4, "SHED", 2, ACCENT, BACK);
    if (state->key >= 0) {
        strcpy(line, "KEY ");
        strcpy(line + 4, pitch_names[state->key % 12]);
        put_text(band_top, FM1_SCREEN_WIDTH - 6 - text_width(line, 1), 8, line, 1, DIM, BACK);
    }
    if (state->session_length) {
        unsigned shown = state->asked < state->session_length ? state->asked + 1 : state->session_length;
        format_fraction(line, shown, state->session_length);
        put_text(band_top, FM1_SCREEN_WIDTH - 6 - text_width(line, 1), 18, line, 1, DIM, BACK);
    }
    rect(band_top, 0, 26, FM1_SCREEN_WIDTH, 1, PANEL);

    switch (state->screen) {
    case FM1_SHED_SCREEN_MENU: {
        unsigned i;
        put_text(band_top, 8, 38, "EJERCICIO", 1, DIM, BACK);
        for (i = 0; i < FM1_SHED_EX_TYPE_COUNT; i++) {
            unsigned y = 58 + i * 20;
            unsigned selected = state->exercise == (enum fm1_shed_exercise_type)i;
            put_text(band_top, 8, y, selected ? ">" : " ", 1, ACCENT, BACK);
            put_text(band_top, 20, y, exercise_names[i], 1, selected ? ACCENT : DIM, BACK);
        }
        put_text(band_top, 8, 120, "ENV", 1, TEXT, BACK);
        put_text(band_top, 44, 120, "CAMBIAR", 1, DIM, BACK);
        put_text(band_top, 8, 134, "PLAY", 1, TEXT, BACK);
        put_text(band_top, 50, 134, "EMPEZAR", 1, DIM, BACK);
        break;
    }
    case FM1_SHED_SCREEN_QUESTION:
        put_text(band_top, 8, 38, exercise_names[state->exercise], 1, TEXT, BACK);
        if (state->exercise == FM1_SHED_EX_DEGREE) {
            put_text(band_top, 8, 70, "ESCUCHA", 2, TEXT, BACK);
            put_text(band_top, 8, 96, "TOCA LA NOTA", 1, DIM, BACK);
        } else {
            put_text(band_top, 8, 62, "QUE ACORDE ES?", 1, TEXT, BACK);
            put_text(band_top, 8, 84, chord_labels[state->chord_pick], 2, ACCENT, BACK);
            put_text(band_top, 8, 142, "PRESETS ELEGIR", 1, DIM, BACK);
            put_text(band_top, 8, 154, "GLO CONFIRMAR", 1, DIM, BACK);
        }
        put_text(band_top, 8, 172, "PLAY REPETIR", 1, DIM, BACK);
        put_text(band_top, 8, 184, "SEQ CADENCIA", 1, DIM, BACK);
        put_text(band_top, 8, 196, "EDIT REVELAR", 1, DIM, BACK);
        put_text(band_top, 8, 208, "ARP SIGUIENTE", 1, DIM, BACK);
        put_text(band_top, 8, 226, "HOME+SCL SALIR", 1, DIM, BACK);
        break;
    case FM1_SHED_SCREEN_FEEDBACK:
        put_text(band_top, 8, 38, exercise_names[state->exercise], 1, TEXT, BACK);
        put_text(band_top, 8, 60, state->correct ? "CORRECTO" : "INCORRECTO", 2,
            state->correct ? GREEN : RED, BACK);
        if (state->question) {
            strcpy(line, "ERA ");
            if (state->question->type == FM1_SHED_EX_DEGREE)
                strcpy(line + 4, pitch_names[fm1_shed_pitch_class(state->question->key + state->question->degree)]);
            else
                strcpy(line + 4, chord_labels[state->question->chord]);
            put_text(band_top, 8, 90, line, 1, TEXT, BACK);
        }
        strcpy(line, "RACHA ");
        line[append_uint(line, 6, state->streak)] = 0;
        put_text(band_top, 8, 112, line, 1, DIM, BACK);
        put_text(band_top, 8, 150, "ARP SIGUIENTE", 1, DIM, BACK);
        put_text(band_top, 8, 164, "HOME+SCL SALIR", 1, DIM, BACK);
        break;
    case FM1_SHED_SCREEN_SUMMARY:
        put_text(band_top, 8, 44, "SESION COMPLETA", 1, ACCENT, BACK);
        strcpy(line, "ACIERTOS ");
        format_fraction(line + 9, state->asked && state->asked < state->session_length ?
            state->asked : state->session_length, state->session_length);
        put_text(band_top, 8, 68, line, 1, TEXT, BACK);
        {
            unsigned pos = append_uint(line, 0, state->percent);
            line[pos++] = '%';
            line[pos] = 0;
        }
        put_text(band_top, 8, 92, line, 2, state->percent >= 70 ? GREEN : AMBER, BACK);
        strcpy(line, "MEJOR RACHA ");
        line[append_uint(line, 12, state->best_streak)] = 0;
        put_text(band_top, 8, 130, line, 1, DIM, BACK);
        put_text(band_top, 8, 160, "ARP NUEVA SESION", 1, DIM, BACK);
        put_text(band_top, 8, 174, "HOME+SCL SALIR", 1, DIM, BACK);
        break;
    }
}

void fm1_shed_ui_draw(const struct fm1_shed_ui_state *state, uint32_t now)
{
    unsigned b;
    if (!state)
        return;
    if (last_draw_ms != ~0U && (uint32_t)(now - last_draw_ms) < 50U)
        return;
    last_draw_ms = now;
    for (b = 0; b < BAND_COUNT; b++) {
        unsigned top = b * BAND_ROWS;
        draw_screen(state, top);
        fm1_screen_blit(0, top, FM1_SCREEN_WIDTH, BAND_ROWS, band, (size_t)FM1_SCREEN_WIDTH * BAND_ROWS);
    }
}

void fm1_shed_ui_show_answer_leds(int pitch_class)
{
    unsigned i;
    for (i = 0; i < KEY_COUNT; i++) {
        unsigned note = (KEY_BASE_NOTE + i) % 12U;
        fm1_led_set(LED_KEY_OFFSET + i, pitch_class >= 0 && (int)note == pitch_class);
    }
}

void fm1_shed_ui_clear_leds(void)
{
    unsigned i;
    for (i = 0; i < 41; i++)
        fm1_led_set(i, 0);
}

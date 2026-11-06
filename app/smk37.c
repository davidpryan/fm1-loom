/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 on the M-VAVE SMK-37 (fm1-base board = "smk37", PLAT_SMK37; felucca-bsp boards/smk37/): what the SMK's
 * controls do beyond the FM-1's. fm1-base's board layer gives LOOM-8 the FM-1's panel as it is (the 14 functions on
 * the SMK's buttons, the seven knob roles on K1..K4 and K6..K8, K5 a virtual MASTER, the middle 27 keys as
 * plat_keys), so ui.c runs unchanged; this file, included by app.c after ui.c and midicc.c (whose statics it uses),
 * adds the rest (docs/SMK37.md):
 *  - the 16 pads are the FM-1's 16 white keys (the steps, the scenes, a kit's sounds, SAVE's slots, the throws ...),
 *    the top row 1-8, the bottom row 9-16, with the lights those keys would have, in the track's ink: lit, mid (a
 *    step with notes), dim (a scene that exists). A pad's velocity is the key's
 *  - the keybed's white keys always play the track on the screen, with velocity, never a step: the pads have the
 *    steps. Its black keys F#3..F#5 are the FM-1's eleven black keys, with their jobs where a page gives them one (a
 *    kit's sounds, the tracks' mutes and solos, a bar's jump), playing elsewhere, as on the FM-1. The ten keys
 *    outside F3..G5 (C3..E3, G#5..C6) play too, on the same octave
 *  - < and > are the bar shown (Steps, Bar, a weave), the scenes 1-16 / 17-32 on the pads (Arrange, Song), back and
 *    a rest in step record, and the page before / after elsewhere; OCTAVE- / OCTAVE+ are the keyboard's octave (a
 *    held step: its nudge; SEQ held on Steps: the pattern's octave, as on the FM-1)
 *  - the faders are the levels of tracks 1-4; with FADER BANK held, 5-8. A fader counts once it moves, so a saved
 *    level stands until then. KNOB BANK has no job yet
 *  - the pitch wheel bends the track on the screen (+-2 semitones, as MIDI pitch bend), the mod wheel is its CC 1
 *  - the sustain pedal holds the keybed's notes until it lifts
 *  - the TRS jack sends everything USB gets (fm1-base's TO_TRS): the MIDI OUT engine's notes, CC OUT, the clock
 * Nothing here has been tried on a unit yet (fm1-base's SMK-37 layer is unconfirmed too, 2026-11). */
#define SMK_K0 5                                   /* the FM-1's key 0 (F3) is the SMK's key 5 */
#define SMK_OUTER(b) ((b) < SMK_K0 || (b) >= SMK_K0 + NKEYS)   /* a keybed key outside the FM-1's 27 */
static struct {
    uint64_t keys;                                 /* the keybed as read last (bit = key of the 37) */
    uint64_t sus;                                  /* keys let go under the pedal: their notes on until it lifts */
    uint32_t pads;                                 /* the pads as read last */
    uint8_t outer[NKEYS37];                        /* an outer key's note while down (0: none) */
    int fader[4];                                  /* each fader's last reading (-1: not read yet) */
    int bend, mod, pedal;
} smk = {.fader = {-1, -1, -1, -1}};

static uint32_t smk_white_bits(uint32_t pads)      /* the pads as the FM-1's white keys' bits */
{
    uint32_t k = 0;
    for (int p = 0; p < NPADS; p++)
        if (pads >> p & 1u)
            k |= 1u << WHITE_K[p];
    return k;
}
/* the FM-1's 27 keys as the SMK has them: the keybed's middle 27 and the pads (on the white keys). A pad and its key
 * down together count once */
static uint32_t smk_keys_now(void)
{
    return ((uint32_t)(plat_keys37() >> SMK_K0) & ((1u << NKEYS) - 1u)) | smk_white_bits(plat_pads());
}

/* the ten keys outside the FM-1's 27: they play (as keyboard_down's keys do, a kit's sounds spread over them; the
 * conductor's keys too), nothing else */
static void smk_outer(int b, int on, int vel)
{
    int trk = sel_trk(), note, srec = ui.rec_armed && !playing();
    if (!on) {
        note = smk.outer[b];
        smk.outer[b] = 0;
        if (!note)
            return;
        if (trk == AUX_BRAIN)
            engine_brain_key(note, 0);
        else
            engine_key(is_aux(trk) ? inst_trk() : trk, note, 0, 0);
        chord_del(note);
        if (srec)
            step_rec_key_up();
        return;
    }
    if (trk == AUX_WEAVE || trk == AUX_PUNCH)
        return;
    if (trk == AUX_BRAIN) {
        note = key_note(b - SMK_K0);
        if (note < 1 || note > 127)
            return;
        smk.outer[b] = (uint8_t)note;
        engine_brain_key(note, 1);
        chord_remember(note);
        if (srec)
            step_rec_key(note);
        return;
    }
    if (is_aux(trk))
        trk = inst_trk();
    note = is_drum(trk) ? b % DL_NSOUND + 1 : key_note(b - SMK_K0);
    if (note < 1 || note > 127)
        return;
    smk.outer[b] = (uint8_t)note;
    engine_key(trk, note, 1, vel);
    chord_add(note);
    if (!is_drum(trk) && !is_aux(sel_trk()))
        chord_remember(note);
    if (srec && !is_aux(sel_trk())) {
        step_rec_key(note);
        if (ui.srec_down == 1 && vel)
            tpat(trk)->st[ui.cursor].vel = (uint8_t)vel;
    }
}

/* a keybed key let go: under the pedal, a playing note stays (sus) until the pedal lifts */
static int smk_sustain(int b)
{
    int k = b - SMK_K0;
    if (!smk.pedal)
        return 0;
    if (SMK_OUTER(b) ? !smk.outer[b] : ui.kmode[k] != KM_PLAY)
        return 0;
    smk.sus |= (uint64_t)1 << b;
    return 1;
}
static void smk_pedal_up(void)
{
    uint64_t sus = smk.sus;
    smk.sus = 0;
    for (int b = 0; b < NKEYS37; b++) {
        if (!(sus >> b & 1u) || (smk.keys >> b & 1u))   /* (a key still down plays on) */
            continue;
        if (SMK_OUTER(b))
            smk_outer(b, 0, 0);
        else if (ui.kmode[b - SMK_K0] == KM_PLAY)
            key_up(b - SMK_K0);
    }
}

/* the keys' edges, in place of input()'s loop: the FM-1's 27 from the pads (the white keys) and the keybed (its
 * velocity), the outer keys, a key struck again while down, the pedal */
static void smk_key_edges(uint32_t btn, uint32_t keys)
{
    uint64_t k37 = plat_keys37(), re = plat_keys37_retrig(), kch;
    uint32_t pads = plat_pads(), pch = pads ^ smk.pads, ch = keys ^ ui.keys, i;
    int pedal = plat_pedal();
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++) {
        int w, pad;
        if (!(ch >> i & 1u))
            continue;
        w = ordinal(WHITE_K, 16, (int)i);
        pad = w >= 0 && (pch >> w & 1u);           /* this white key's change is its pad's */
        if (keys >> i & 1u) {
            ui.card = 0;
            ui.btn_used |= btn;
            key_src = pad;
            key_vel = pad ? plat_pad_vel(w) : plat_key_vel((int)i + SMK_K0);
            if (key_vel < 1)
                key_vel = 100;
            smk.sus &= ~((uint64_t)1 << (i + SMK_K0));
            key_down((int)i);
        } else if (ui.kmode[i] == KM_SAVE) {
            ui.kmode[i] = KM_NONE;
            save_key((int)i);
        } else if (pad || !smk_sustain((int)i + SMK_K0)) {
            key_up((int)i);
        }
    }
    key_src = 0;
    key_vel = 127;
    smk.pads = pads;
    kch = k37 ^ smk.keys;
    for (i = 0; i < NKEYS37; i++) {                /* the outer keys */
        if (!SMK_OUTER((int)i) || !(kch >> i & 1u))
            continue;
        if (k37 >> i & 1u) {
            int vel = plat_key_vel((int)i);
            ui.card = 0;
            ui.btn_used |= btn;
            smk.sus &= ~((uint64_t)1 << i);
            smk_outer((int)i, 1, vel < 1 ? 100 : vel);
        } else if (!smk_sustain((int)i)) {
            smk_outer((int)i, 0, 0);
        }
    }
    smk.keys = k37;
    for (i = 0; re && i < NKEYS37; i++) {          /* struck again while down: the note again, at its new velocity */
        int vel;
        if (!(re >> i & 1u))
            continue;
        vel = plat_key_vel((int)i);
        if (vel < 1)
            vel = 100;
        if (SMK_OUTER((int)i)) {
            smk_outer((int)i, 0, 0);
            smk_outer((int)i, 1, vel);
        } else if (ui.kmode[i - SMK_K0] == KM_PLAY) {
            key_up((int)i - SMK_K0);
            key_vel = vel;
            key_down((int)i - SMK_K0);
            key_vel = 127;
        }
    }
    if (smk.pedal && !pedal)
        smk_pedal_up();
    smk.pedal = pedal;
}

/* the faders (the tracks' levels) and the wheels (the track on the screen's bend and CC 1), once they move */
static void smk_controls(void)
{
    int t = inst_trk(), v;
    for (int i = 0; i < 4; i++) {
        v = plat_fader(i);
        if (smk.fader[i] < 0) {
            smk.fader[i] = v;                      /* (where it stands at power-on: nothing moved) */
            continue;
        }
        if (v == smk.fader[i])
            continue;
        smk.fader[i] = v;
        cc_rel = 0;
        cc_par((ui.btn >> B_FBANK & 1u) ? 4 + i : i, TP_LEVEL, v);
    }
    v = plat_wheel(PLAT_WHEEL_PITCH);
    if (v != smk.bend) {
        smk.bend = v;
        engine_bend(t, v * 200 / 8192);
    }
    v = plat_wheel(PLAT_WHEEL_MOD);
    if (v != smk.mod) {
        smk.mod = v;
        cc_rel = 0;
        cc_track(t, 1, v);
    }
}

/* < and >: the bar shown, the scenes' half, step record's cursor, else the page; KNOB BANK and FADER BANK: nothing
 * on their own (FADER BANK is read held, smk_controls) */
static void smk_button_up(int b)
{
    int d = b == B_RIGHT ? 1 : -1;
    char t[8];
    if (b != B_LEFT && b != B_RIGHT)
        return;
    if (ui.rec_armed && !playing() && is_seqt(sel_trk())) {
        step_rec_move(d);
        num_s((uint32_t)ui.cursor + 1u, t);
        say(d > 0 ? "REST" : "BACK", t);
        return;
    }
    if (ui.page == P_ARRANGE || ui.page == P_SONG) {
        ui.scene_pg = (uint8_t)(d > 0);
        say("SCENES", ui.scene_pg ? "17 to 32" : "1 to 16");
        return;
    }
    if (steps_page(ui.page) || (ui.page == P_WEAVE && wv_from(sel_trk()))) {
        ui.bar = (uint8_t)clampi(ui.bar + d, 0, NBAR - 1);
        num_s((uint32_t)ui.bar + 1u, t);
        say("BAR", t);
        return;
    }
    page_turn(d);
}

/* the pads' lights: what the FM-1's white keys would show (leds()), in the track's ink */
static uint32_t smk_rgb(uint16_t c, int level)     /* an RGB565 ink as 0xRRGGBB: lit, mid (3/8), dim (1/8) */
{
    uint32_t r = (c >> 11 & 31u) * 255u / 31u, g = (c >> 5 & 63u) * 255u / 63u, b = (c & 31u) * 255u / 31u;
    if (level == 1)
        r = r * 3u / 8u, g = g * 3u / 8u, b = b * 3u / 8u;
    else if (level == 2)
        r /= 8u, g /= 8u, b /= 8u;
    return r << 16 | g << 8 | b;
}
static void smk_pad_lights(uint32_t lit, uint32_t mid, uint32_t dim, int trk)
{
    uint16_t ink = trk_ink(trk);
    for (int p = 0; p < NPADS; p++) {
        uint32_t m = 1u << WHITE_K[p];
        plat_pad_rgb(p, lit & m ? smk_rgb(ink, 0) : mid & m ? smk_rgb(ink, 1) : dim & m ? smk_rgb(ink, 2) : 0u);
    }
}

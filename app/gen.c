/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's generators (SEQ's third page, Gen): a pattern written from a few knobs, from the pattern as it was when
 * the page's first knob turned (the base: undo's copy, pages.c undo), so turning back gets it back and undo puts it
 * as it was. Five of them:
 *   EUCLID  hits spread as evenly as they go over a cycle of steps, rotated (Bresenham's: Bjorklund's patterns)
 *   GRIDS   Mutable Instruments Grids' map of drum patterns (seq/grids_map.h: Emilie Gillet's, GPL-3.0-or-later),
 *           X and Y between its 25 patterns, density: how many hits; a kit's kick, snare and hat (sounds 1-3)
 *   TURING  Music Thing's Turing Machine: a shift register of LENGTH bits looping, each bit leaving it flipped by
 *           the chance CHANGE (0: locked, a loop of LENGTH; 100: new every time); its value the note, a bit the gate
 *   WALK    a random walk in the conductor's scale: each note a step of up to STRIDE degrees from the last, within
 *           RANGE; DENSITY the chance a step plays
 *   MUTATE  the base changed in AMOUNT of its steps: their pitch moved, their hits moved, dropped or added
 * A melodic track's notes are the conductor's key and scale (seq.brain_key, brain_scale) from the note a step
 * writes (ui.rem[0]); EUCLID and GRIDS keep the base's notes where it had them (the rhythm changes, the tune stays).
 * A kit: the sound shown (black keys) alone, the rest of the base as it was (GRIDS: sounds 1-3). The random ones
 * draw from a seed (PRESETS: a new one), so the same knobs give the same pattern.
 * Nothing runs per sample or per step: a render when a knob turns. Pure: no UI, no state (tests/host/gen_test.c). */
#include "seq/grids_map.h"

enum { GEN_EUCLID, GEN_GRIDS, GEN_TURING, GEN_WALK, GEN_MUTATE, NGEN };
static const char *const GEN_NAME[NGEN] = {"euclid", "grids", "turing", "walk", "mutate"};
/* each generator's three knobs: their names (a kit's where they differ), lowest, highest, starting value */
static const struct { const char *name[3], *kname[3]; uint8_t lo[3], hi[3], def[3]; } GEN_K[NGEN] = {
    {{"HITS", "STEPS", "ROTATE"}, {0, 0, 0}, {0, 1, 0}, {16, 16, 15}, {4, 16, 0}},
    {{"X", "Y", "DENSITY"}, {0, 0, 0}, {0, 0, 0}, {100, 100, 100}, {50, 50, 50}},
    {{"LENGTH", "CHANGE", "RANGE"}, {0, 0, "DENSITY"}, {2, 0, 1}, {16, 100, 20}, {8, 10, 7}},
    {{"DENSITY", "STRIDE", "RANGE"}, {0, 0, "SPREAD"}, {0, 1, 1}, {100, 7, 20}, {70, 2, 7}},
    {{"AMOUNT", "KIND", "SPREAD"}, {0, 0, 0}, {0, 0, 1}, {16, 3, 7}, {4, 3, 2}}};
enum { MU_PITCH, MU_MOVE, MU_DROPADD, MU_ALL, NMU };
static const char *const MU_NAME[NMU] = {"pitch", "move", "drop/add", "all"};

static uint32_t gen_rnd(uint32_t *r)                 /* xorshift32 (never 0) */
{
    uint32_t x = *r ? *r : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}
static int gen_chance(uint32_t *r, int pct) { return (int)(gen_rnd(r) % 100u) < pct; }
static int gen_euclid_hit(int i, int hits, int steps, int rot)   /* step i of the cycle a hit */
{
    if (steps < 1 || hits < 1)
        return 0;
    if (hits > steps)
        hits = steps;
    i = ((i + rot) % steps + steps) % steps;
    return (i * hits) % steps < hits;
}
static uint8_t gen_mix8(uint8_t a, uint8_t b, uint8_t bal) { return (uint8_t)((a * (255u - bal) + b * bal) >> 8); }
static uint8_t gen_grids_level(int step, int part, int x, int y)   /* Grids' ReadDrumMap: x, y 0..255 */
{
    int i = x >> 6, j = y >> 6, o = part * 32 + (step & 31);
    uint8_t fx = (uint8_t)(x << 2), fy = (uint8_t)(y << 2);
    uint8_t a = GRIDS_NODE[GRIDS_MAP[i][j]][o], b = GRIDS_NODE[GRIDS_MAP[i + 1][j]][o];
    uint8_t c = GRIDS_NODE[GRIDS_MAP[i][j + 1]][o], d = GRIDS_NODE[GRIDS_MAP[i + 1][j + 1]][o];
    return gen_mix8(gen_mix8(a, b, fx), gen_mix8(c, d, fx), fy);
}

static void gen_note(step_t *st, int note, int vel)  /* a fresh step of one note */
{
    step_t z;
    memset(&z, 0, sizeof z);
    z.note[0] = (uint8_t)(note < 1 ? 1 : note > 127 ? 127 : note);
    z.vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
    *st = z;
}
static int gen_has(const step_t *st, int kit, int sound)   /* the step plays (a kit: the sound, 1..11) */
{
    return kit ? kit_has(st, sound) : st->note[0] != 0;
}
static void gen_hit(step_t *st, int sound, int vel)  /* a kit step: the sound added, at vel */
{
    if (!kit_mask(st))
        st->vel = (uint8_t)vel;
    kit_set(st, kit_mask(st) | 1u << (sound - 1));
}
static void gen_unhit(step_t *st, int sound) { kit_set(st, kit_mask(st) & ~(1u << (sound - 1))); }
/* a melodic hit at step i: the base's step there, or the last one before it (its notes, no tricks), or the root */
static void gen_keep_note(step_t *st, const tpat_t *base, int i, int len, int root, int vel)
{
    for (int k = 0; k < len; k++) {
        const step_t *b = &base->st[((i - k) % len + len) % len];
        if (b->note[0]) {
            if (k == 0)
                *st = *b;
            else {
                gen_note(st, b->note[0], b->vel);
                for (int n = 1; n < SN; n++)
                    st->note[n] = b->note[n];
            }
            return;
        }
    }
    gen_note(st, root, vel);
}

typedef struct {
    int kit;                  /* 1: a kit's steps (sound), 0: notes */
    int sound;                /* a kit: the sound written, 1..11 */
    int root, key, scale;     /* notes: the note a step writes, the conductor's key and scale */
    uint32_t seed;
} gen_ctx_t;

/* out: the base's settings and length, its steps generated (out may not be base) */
static void gen_render(tpat_t *out, const tpat_t *base, int type, const uint8_t v[3], const gen_ctx_t *c)
{
    int len = base->len < 1 ? 1 : base->len > NSTEP ? NSTEP : base->len, kit = c->kit, s = c->sound;
    int root = scale_snap(c->root ? c->root : 60, c->key, c->scale);
    uint32_t r = c->seed * 2654435761u + (uint32_t)type * 40503u + 1u;
    *out = *base;
    if (type != GEN_MUTATE)                          /* (the generated: notes all new; a kit, the sounds written) */
        for (int i = 0; i < len; i++) {
            if (!kit)
                memset(&out->st[i], 0, sizeof out->st[i]);
            else if (type == GEN_GRIDS)
                for (int p = 1; p <= 3; p++)
                    gen_unhit(&out->st[i], p);
            else
                gen_unhit(&out->st[i], s);
        }
    switch (type) {
    case GEN_EUCLID:
        for (int i = 0; i < len; i++) {
            int cyc = v[1] < 1 ? 1 : v[1];
            if (!gen_euclid_hit(i % cyc, v[0], cyc, v[2]))
                continue;
            int vel = i % cyc == 0 ? 120 : 100;       /* (the cycle's first: an accent) */
            if (kit)
                gen_hit(&out->st[i], s, vel);
            else
                gen_keep_note(&out->st[i], base, i, len, root, vel);
        }
        break;
    case GEN_GRIDS: {
        int x = v[0] * 255 / 100, y = v[1] * 255 / 100, th = 255 - v[2] * 255 / 100;
        x = x > 255 ? 255 : x;                       /* (x >> 6 at most 3: the map's last column is the next's) */
        y = y > 255 ? 255 : y;
        for (int i = 0; i < len; i++)
            for (int p = 0; p < (kit ? 3 : 1); p++) {
                int lv = gen_grids_level(i, p, x, y);
                if (lv <= th || !v[2])
                    continue;
                if (kit)
                    gen_hit(&out->st[i], p + 1, lv > 192 ? 120 : 90);
                else
                    gen_keep_note(&out->st[i], base, i, len, root, lv > 192 ? 120 : 90);
            }
        break;
    }
    case GEN_TURING: {
        int n = v[0] < 2 ? 2 : v[0] > 16 ? 16 : v[0];
        uint32_t reg = gen_rnd(&r) & ((1u << n) - 1u), top = 1u << (n - 1);
        for (int i = 0; i < len; i++) {
            uint32_t b = reg & 1u;
            if (gen_chance(&r, v[1]))
                b ^= 1u;
            reg = reg >> 1 | (b ? top : 0u);
            int val = (int)(n >= 8 ? reg >> (n - 8) : reg << (8 - n)) & 255;   /* the register's top 8 bits */
            if (kit) {
                if (val * 100 / 256 < v[2] * 5)      /* (DENSITY: the knob's 1..20 in fives of %) */
                    gen_hit(&out->st[i], s, 70 + (val & 63) * 57 / 63);
            } else if (reg & 2u)
                gen_note(&out->st[i], scale_step(root, c->key, c->scale, val * (v[2] + 1) / 256), 80 + (val & 31) * 47 / 31);
        }
        break;
    }
    case GEN_WALK: {
        int lo = -(v[2] / 2), hi = v[2] - v[2] / 2, d = 0, vel = 100;
        for (int i = 0; i < len; i++) {
            int mv = (int)(gen_rnd(&r) % (uint32_t)(2 * v[1] + 1)) - v[1];
            if (!gen_chance(&r, v[0]))
                continue;
            if (kit) {                               /* a kit: its velocity walks, SPREAD below 127 */
                int lov = 127 - v[2] * 5;
                vel += mv * 8;
                vel = vel > 127 ? 254 - vel : vel < lov ? 2 * lov - vel : vel;
                vel = vel > 127 ? 127 : vel < lov ? lov : vel;
                gen_hit(&out->st[i], s, vel);
            } else {
                d += mv;
                d = d > hi ? 2 * hi - d : d < lo ? 2 * lo - d : d;   /* (off the edge: back, as a reflection) */
                d = d > hi ? hi : d < lo ? lo : d;
                gen_note(&out->st[i], scale_step(root, c->key, c->scale, d), 90 + (int)(gen_rnd(&r) % 30u));
            }
        }
        break;
    }
    case GEN_MUTATE: {
        int kind = v[1] % NMU;
        for (int a = 0; a < v[0]; a++) {
            int i = (int)(gen_rnd(&r) % (uint32_t)len), k = kind == MU_ALL ? (int)(gen_rnd(&r) % 3u) : kind;
            int sp = (int)(gen_rnd(&r) % (uint32_t)v[2]) + 1, dir = gen_rnd(&r) & 1u ? 1 : -1;
            step_t *st = &out->st[i];
            if (k == MU_PITCH) {                     /* a note moved SPREAD degrees at most; a kit's hit, its velocity */
                if (!gen_has(st, kit, s))
                    continue;
                if (kit) {
                    int nv = st->vel + dir * sp * 10;
                    st->vel = (uint8_t)(nv > 127 ? 127 : nv < 20 ? 20 : nv);
                } else
                    for (int nn = 0; nn < SN; nn++)
                        if (st->note[nn])
                            st->note[nn] = (uint8_t)scale_step(st->note[nn], c->key, c->scale, dir * sp);
            } else if (k == MU_MOVE) {               /* a hit moved up to SPREAD steps, onto an empty one */
                int j = ((i + dir * sp) % len + len) % len;
                if (!gen_has(st, kit, s) || gen_has(&out->st[j], kit, s))
                    continue;
                if (kit) {
                    gen_hit(&out->st[j], s, st->vel);
                    gen_unhit(st, s);
                } else {
                    out->st[j] = *st;
                    memset(st, 0, sizeof *st);
                }
            } else if (gen_has(st, kit, s)) {        /* drop it, or (an empty step) add one */
                if (kit)
                    gen_unhit(st, s);
                else
                    memset(st, 0, sizeof *st);
            } else if (kit)
                gen_hit(st, s, 90);
            else
                gen_keep_note(st, base, i, len, root, 90);
        }
        break;
    }
    default: break;
    }
}

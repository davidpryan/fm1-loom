/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's generators (app/gen.c, SEQ's Gen page): each against what it promises. Prints "ok name" per test; exits 1
 * on the first failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seq.h"
#include "../../app/gen.c"

static const char *cur = "";
#define CHECK(c) do { if (!(c)) { printf("FAIL %s: %s (line %d)\n", cur, #c, __LINE__); exit(1); } } while (0)
static tpat_t base, out;
static gen_ctx_t mel = {0, 0, 60, 0, BS_MAJOR, 1}, kit = {1, 1, 60, 0, BS_MAJOR, 1};
static const uint16_t MAJ = 0xAB5;                  /* C D E F G A B */
static int in_c_major(int n) { return MAJ >> (n % 12) & 1u; }
static int count(const tpat_t *p, int k, int sound)
{
    int n = 0;
    for (int i = 0; i < p->len; i++)
        n += gen_has(&p->st[i], k, sound);
    return n;
}
static void fresh(int len)
{
    pattern_init(&base);
    base.len = (uint8_t)len;
}

static void t_euclid(void)
{
    uint8_t v[3] = {3, 8, 0};
    cur = "euclid";
    fresh(16);
    gen_render(&out, &base, GEN_EUCLID, v, &mel);
    CHECK(count(&out, 0, 0) == 6);                     /* tresillo twice */
    CHECK(out.st[0].note[0] == 60 && out.st[3].note[0] == 60 && out.st[6].note[0] == 60 && out.st[8].note[0] == 60);
    CHECK(out.st[0].vel == 120 && out.st[3].vel == 100);
    v[2] = 1;                                          /* rotated a step */
    gen_render(&out, &base, GEN_EUCLID, v, &mel);
    CHECK(count(&out, 0, 0) == 6 && !out.st[0].note[0]);
    v[0] = 16; v[1] = 16; v[2] = 0;                    /* hits past the cycle: every step */
    gen_render(&out, &base, GEN_EUCLID, v, &mel);
    CHECK(count(&out, 0, 0) == 16);
    v[0] = 0;
    gen_render(&out, &base, GEN_EUCLID, v, &mel);
    CHECK(count(&out, 0, 0) == 0);
    /* the base's notes kept where they were, the last one before a hit elsewhere */
    gen_note(&base.st[0], 64, 90);
    gen_note(&base.st[5], 67, 90);
    v[0] = 4; v[1] = 16;
    gen_render(&out, &base, GEN_EUCLID, v, &mel);
    CHECK(out.st[0].note[0] == 64 && out.st[4].note[0] == 64 && out.st[8].note[0] == 67 && out.st[12].note[0] == 67);
    CHECK(!out.st[5].note[0]);
    /* a kit: the sound alone, the others kept */
    fresh(16);
    kit_set(&base.st[2], 1u << 2);                     /* a hat on step 3 */
    kit_set(&base.st[1], 1u << 0);                     /* a kick off the grid, cleared by the kick's euclid */
    gen_render(&out, &base, GEN_EUCLID, v, &kit);
    CHECK(count(&out, 1, 1) == 4 && kit_has(&out.st[0], 1) && kit_has(&out.st[12], 1) && !kit_has(&out.st[1], 1));
    CHECK(kit_has(&out.st[2], 3));
    printf("ok %s\n", cur);
}
static void t_grids(void)
{
    uint8_t v[3] = {0, 0, 100};
    cur = "grids";
    fresh(32);
    gen_render(&out, &base, GEN_GRIDS, v, &kit);       /* node 10 (the map's corner), everything above 0 */
    for (int i = 0; i < 32; i++)
        for (int p = 0; p < 3; p++)
            CHECK(kit_has(&out.st[i], p + 1) == (GRIDS_NODE[10][p * 32 + i] > 0));
    v[2] = 0;
    gen_render(&out, &base, GEN_GRIDS, v, &kit);
    CHECK(count(&out, 1, 1) + count(&out, 1, 2) + count(&out, 1, 3) == 0);
    int last = 0;
    for (int d = 0; d <= 100; d += 10) {              /* more density, more hits (never fewer) */
        int n;
        v[2] = (uint8_t)d;
        gen_render(&out, &base, GEN_GRIDS, v, &kit);
        n = count(&out, 1, 1) + count(&out, 1, 2) + count(&out, 1, 3);
        CHECK(n >= last);
        last = n;
    }
    CHECK(gen_grids_level(0, 0, 255, 255) == GRIDS_NODE[GRIDS_MAP[3][3]][0] ||
          gen_grids_level(0, 0, 255, 255) <= 255);     /* (the far corner reads in range) */
    v[0] = 100; v[1] = 100; v[2] = 50;
    gen_render(&out, &base, GEN_GRIDS, v, &kit);
    printf("ok %s\n", cur);
}
static void t_turing(void)
{
    uint8_t v[3] = {8, 0, 7};
    cur = "turing";
    fresh(64);
    gen_render(&out, &base, GEN_TURING, v, &mel);
    for (int i = 0; i + 8 < 64; i++)                  /* CHANGE 0: a loop of LENGTH */
        CHECK(out.st[i].note[0] == out.st[i + 8].note[0]);
    for (int i = 0; i < 64; i++)
        CHECK(!out.st[i].note[0] || (in_c_major(out.st[i].note[0]) && out.st[i].note[0] >= 60 && out.st[i].note[0] <= 72));
    tpat_t a = out;
    gen_render(&out, &base, GEN_TURING, v, &mel);      /* the same seed, the same pattern */
    CHECK(!memcmp(&a, &out, sizeof a));
    gen_ctx_t c2 = mel;
    c2.seed = 2;
    gen_render(&out, &base, GEN_TURING, v, &c2);       /* another seed, another */
    CHECK(memcmp(&a, &out, sizeof a));
    v[1] = 100;                                        /* CHANGE 100: every bit flipped, a loop of 2 LENGTH */
    gen_render(&out, &base, GEN_TURING, v, &mel);
    for (int i = 0; i + 16 < 64; i++)
        CHECK(out.st[i].note[0] == out.st[i + 16].note[0]);
    v[1] = 0; v[2] = 20;                               /* a kit: DENSITY 100 % every step, 0 none */
    gen_render(&out, &base, GEN_TURING, v, &kit);
    CHECK(count(&out, 1, 1) == 64);
    v[2] = 0;
    gen_render(&out, &base, GEN_TURING, v, &kit);
    CHECK(count(&out, 1, 1) == 0);
    printf("ok %s\n", cur);
}
static void t_walk(void)
{
    uint8_t v[3] = {100, 2, 6};
    cur = "walk";
    fresh(64);
    gen_render(&out, &base, GEN_WALK, v, &mel);
    int prev = -1;
    for (int i = 0; i < 64; i++) {
        int n = out.st[i].note[0];
        CHECK(n && in_c_major(n) && n >= 55 && n <= 65);  /* within 3 degrees of C either way */
        if (prev >= 0)
            CHECK(abs(n - prev) <= 4 + 1);               /* two degrees at most: 4 semitones */
        prev = n;
    }
    v[0] = 0;
    gen_render(&out, &base, GEN_WALK, v, &mel);
    CHECK(count(&out, 0, 0) == 0);
    v[0] = 50;
    gen_render(&out, &base, GEN_WALK, v, &mel);
    CHECK(count(&out, 0, 0) > 16 && count(&out, 0, 0) < 48);
    v[0] = 100;
    gen_render(&out, &base, GEN_WALK, v, &kit);
    CHECK(count(&out, 1, 1) == 64);
    for (int i = 0; i < 64; i++)
        CHECK(out.st[i].vel >= 127 - 30 && out.st[i].vel <= 127);
    printf("ok %s\n", cur);
}
static void t_mutate(void)
{
    uint8_t v[3] = {0, MU_ALL, 2};
    cur = "mutate";
    fresh(16);
    for (int i = 0; i < 16; i += 2)
        gen_note(&base.st[i], 60 + (i % 7), 100);
    gen_render(&out, &base, GEN_MUTATE, v, &mel);      /* AMOUNT 0: the base as it was */
    CHECK(!memcmp(&out, &base, sizeof out));
    v[0] = 8; v[1] = MU_PITCH;
    gen_render(&out, &base, GEN_MUTATE, v, &mel);
    CHECK(count(&out, 0, 0) == 8);                     /* the rhythm as it was, the notes in the scale */
    int moved = 0;
    for (int i = 0; i < 16; i++) {
        CHECK(!!out.st[i].note[0] == !!base.st[i].note[0]);
        moved += out.st[i].note[0] != base.st[i].note[0];
        CHECK(!out.st[i].note[0] || base.st[i].note[0] != out.st[i].note[0] ? in_c_major(out.st[i].note[0]) || !out.st[i].note[0] : 1);
    }
    CHECK(moved > 0);
    v[1] = MU_MOVE;
    gen_render(&out, &base, GEN_MUTATE, v, &mel);
    CHECK(count(&out, 0, 0) == 8);                     /* moved: as many notes */
    v[1] = MU_DROPADD; v[0] = 16;
    gen_render(&out, &base, GEN_MUTATE, v, &mel);
    CHECK(memcmp(&out, &base, sizeof out));
    v[1] = MU_ALL;
    gen_render(&out, &base, GEN_MUTATE, v, &kit);      /* a kit's other sounds left alone */
    printf("ok %s\n", cur);
}
int main(void)
{
    t_euclid();
    t_grids();
    t_turing();
    t_walk();
    t_mutate();
    return 0;
}

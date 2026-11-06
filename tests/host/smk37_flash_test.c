/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's flash map on the SMK-37 (app.h, dsp/sample.h, PLAT_SMK37) against the board's map (felucca-bsp
 * boards/smk37/fm1_flash.h): every piece of LOOM-8's data (the projects' pool, the resource region, the calibration
 * and presets pairs, OBJ_BLE, the three windows of the user sample bank) is sector aligned, inside a window fm1-base
 * writes (the data area, the settings pair, the two extra windows), clear of the sectors the board keeps (the boot
 * head, BTIF and USRTRIM, USR, key_mac, the app area, the update's staging, the autosave pair, the owner tag) and
 * clear of each other. The pool and the resource region fit the data area with the pairs; the bank's three windows
 * are what tools/sampleio.py lays a bank out for (BOARDS["smk37"]).
 *   cc -std=c99 -DPLAT_SMK37=1 -Ibase/bsp/boards/smk37 -Ibase/include -Iapp tests/host/smk37_flash_test.c
 * (tests/run_tests.sh runs it) */
#include <stdio.h>
#include <stdint.h>
#define FL_MAP_ONLY 1
#include "fm1_flash.h"
#define PLAT_SMK37 1
#include "plat.h"
#include "app.h"                                   /* LOOM-8's map on the SMK (PLAT_SMK37): APP_STORE_MAP, the pool, the
                                                   * resource region; dsp/sample.h the bank's windows */
#include "dsp/sample.h"
static const uint32_t MAP[OBJ_NOBJ] = APP_STORE_MAP;

typedef struct { const char *name; uint32_t lo, hi; } win_t;
static const win_t KEEP[] = {{"the boot head", FL_PROT0_LO, FL_PROT0_HI}, {"BTIF + USRTRIM", FL_PROT1_LO, FL_PROT1_HI},
                             {"USR", FL_PROT2_LO, FL_PROT2_HI}, {"key_mac", FL_PROT3_LO, FL_PROT3_HI},
                             {"the app area", FL_APP_LO, FL_APP_HI}, {"the update's staging", FL_OTA_LO, FL_OTA_HI},
                             {"the autosave pair", FL_AUTO_LO, FL_AUTO_HI}, {"the owner tag", FL_OWNER, FL_OWNER + FL_SECTOR},
                             {"the stock marker", FL_STOCK_MARK, FL_STOCK_MARK + FL_SECTOR}};
static const win_t OURS[] = {{"the projects' pool", APP_STORE_POOL_BASE, APP_STORE_POOL_BASE + APP_STORE_POOL_N * FL_SECTOR},
                             {"the resource region", APP_RES_BASE, APP_RES_BASE + APP_RES_SECTORS * FL_SECTOR},
                             {"the panel calibration", MAP[OBJ_PANEL], MAP[OBJ_PANEL] + 2u * FL_SECTOR},
                             {"the user presets", MAP[OBJ_PRESETS], MAP[OBJ_PRESETS] + 2u * FL_SECTOR},
                             {"OBJ_BLE", MAP[OBJ_BLE], MAP[OBJ_BLE] + 2u * FL_SECTOR},
                             {"the bank's window 0", SMP_REG0, SMP_REG0 + SMP_REG0_SIZE},
                             {"the bank's window 1", SMP_REG1, SMP_REG1 + SMP_REG1_SIZE},
                             {"the bank's window 2", SMP_REG2, SMP_REG2 + SMP_REG2_SIZE}};
#define N(a) (sizeof (a) / sizeof (a)[0])
static int bad;
static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    bad += !ok;
}
static int apart(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a1 <= b0 || b1 <= a0; }
static int inside(uint32_t lo, uint32_t hi, uint32_t wlo, uint32_t whi) { return lo >= wlo && hi <= whi; }

int main(void)
{
    uint32_t i, j;
    char b[120];
    printf("LOOM-8 on the SMK-37: the data area %#x..%#x, the extra windows %#x..%#x and %#x..%#x, the settings pair %#x\n",
           (unsigned)FL_DATA_LO, (unsigned)FL_DATA_HI, (unsigned)FL_XDATA0_LO, (unsigned)FL_XDATA0_HI,
           (unsigned)FL_XDATA1_LO, (unsigned)FL_XDATA1_HI, (unsigned)FL_GLOB_LO);
    for (i = 0; i < N(OURS); i++)
        printf("  %-22s %#07x..%#07x  %3u sectors\n", OURS[i].name, (unsigned)OURS[i].lo, (unsigned)OURS[i].hi,
               (unsigned)((OURS[i].hi - OURS[i].lo) >> 12));
    for (i = 0; i < N(OURS); i++) {
        const win_t *w = &OURS[i];
        int store = i < 5, ok;
        snprintf(b, sizeof b, "%s: sector aligned, inside the part", w->name);
        check(b, !(w->lo & 0xFFFu) && !(w->hi & 0xFFFu) && w->lo < w->hi && w->hi <= FL_FLASH_SIZE);
        ok = store ? inside(w->lo, w->hi, FL_DATA_LO, FL_DATA_HI) || inside(w->lo, w->hi, FL_GLOB_LO, FL_GLOB_HI)
                   : inside(w->lo, w->hi, FL_XDATA0_LO, FL_XDATA0_HI) || inside(w->lo, w->hi, FL_XDATA1_LO, FL_XDATA1_HI);
        snprintf(b, sizeof b, "%s: in %s", w->name, store ? "the store's windows (the data area, the settings pair)"
                                                        : "an extra window (plat_data, outside the store)");
        check(b, ok);
        for (j = 0; j < N(KEEP); j++) {
            snprintf(b, sizeof b, "%s clear of %s", w->name, KEEP[j].name);
            check(b, apart(w->lo, w->hi, KEEP[j].lo, KEEP[j].hi));
        }
        for (j = i + 1; j < N(OURS); j++) {
            snprintf(b, sizeof b, "%s clear of %s", w->name, OURS[j].name);
            check(b, apart(w->lo, w->hi, OURS[j].lo, OURS[j].hi));
        }
    }
    check("the pooled objects' map entries stand in the pool (not used: APP_STORE_POOL)",
          MAP[OBJ_PROJ0] == APP_STORE_POOL_BASE && MAP[OBJ_PROJ0 + NPROJ * PROJ_NOBJ - 1] == APP_STORE_POOL_BASE);
    check("no A/B pairs to move at the first boot (APP_STORE_POOL_OLD == _N)", APP_STORE_POOL_OLD == APP_STORE_POOL_N);
    check("the resource region holds the FM-1's payload (22 sectors, 88 KB: the 909 cymbals and the large type)",
          APP_RES_SECTORS == 22u && APP_RES_SECTORS * FL_SECTOR >= 88444u + 32u);
    check("the pool, the region and the two pairs fill the data area exactly",
          APP_STORE_POOL_N * FL_SECTOR + APP_RES_SECTORS * FL_SECTOR + 4u * FL_SECTOR == FL_DATA_HI - FL_DATA_LO);
    check("the bank is 108 KB, its header in the first window, 0xF00..0xF07 of every sector free (SMP_XDATA_FIRST 0)",
          SMP_BANK_SIZE == 0x1B000u && SMP_BANK_BASE == SMP_REG0 && SMP_XDATA_FIRST == 0);
    check("the bank holds the factory set's image (110,109 B: build/gen/factory_bank.bin on the FM-1's layout)",
          SMP_BANK_SIZE >= 110109u);
    check("plat.h's windows are the board's", PLAT_DATA_LO == FL_DATA_LO && PLAT_DATA_HI == FL_DATA_HI &&
          PLAT_XDATA0_LO == FL_XDATA0_LO && PLAT_XDATA1_HI == FL_XDATA1_HI && PLAT_GLOB == FL_GLOB_LO);
    printf("%s\n", bad ? "SMK37 FLASH TEST FAILED" : "smk37 flash test passed");
    return bad != 0;
}

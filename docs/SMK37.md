# LOOM-8 on the M-VAVE SMK-37

LOOM-8 builds for the M-VAVE SMK-37 keyboard (Pro, Elite and MKE-P37: the same hardware under three names) as well
as for the FM-1. The SMK-37 is the FM-1's chip with a different board: 37 velocity keys, 16 pads, 8 encoders, 4
faders, two wheels, a sustain pedal jack, the FM-1's screen and, where the FM-1 has MIDI IN, a TRS jack that is
MIDI OUT. fm1-base's SMK-37 board layer (keremimo's `feat/smk37-board`, charlesvestal/fm1-base#38, with felucca-bsp
`boards/smk37/`) gives an app the FM-1's panel as it is, so LOOM-8 runs there unchanged; `app/smk37.c` adds what the
SMK has beyond it, and `app/app.h` moves LOOM-8's flash around the sectors the SMK keeps.

**Nothing of this has been tried on a unit** (2026-11): fm1-base's SMK-37 layer itself is unconfirmed on hardware,
and so is everything below. Keep M-VAVE's stock firmware and M-UPGRADE at hand before installing.

## Building

```
BOARD=smk37 ./build.sh                      # app/build/loom-smk37.fwsc (the SMK-37 Pro)
BOARD=smk37 ./build.sh --release 0.14-smk37  # app/build/loom-smk37-0.14-smk37.fwsc, identity SMK-37 Pro_10...
BOARD=smk37 MODEL=elite ./build.sh          # the Elite (loom-smk37-elite.fwsc); MODEL=mke the MKE-P37
BOARD=smk37 ./build.sh --host               # the simulator: app/build/host/loom-smk37_host
```

`BOARD` and `MODEL` become fm1-base's `FM1B_BOARD` and `FM1B_MODEL`. The SMK's board layer (the pads, faders, wheels,
its LED pictures) takes about 1.6 KB more RAM than the FM-1's, so build.sh sets the pool margin fm1-base's build
check insists on to 3 KB for the SMK (`FM1B_POOL_SPARE`; fm1.toml's 4 KB on the FM-1; it is the margin for
what comes next in the build, not a reserve the firmware uses at run time). The package carries the model's identity, so the
installers (fm1-base's page, `base/tools/fm1_install.py`) refuse it on an FM-1 and an FM-1 package on an SMK. The
resource region (the 909 cymbals and the large type, `loom-smk37-res.bin`) is written after the firmware as on the
FM-1. The FM-1 build is not changed by any of this (`PLAT_SMK37` is 0 there).

## The flash

The SMK-37's 1 MiB flash is laid out by its stock firmware, and fm1-base keeps to the stock layout's free parts
(felucca-bsp `boards/smk37/fm1_flash.h`): never the boot head (0x00000..0x03FFF), BTIF and USRTRIM (the Bluetooth
pairing and the factory calibration curves, 0xC0000..0xC1FFF), USR (0xF4000..0xFDFFF) or key_mac (0xFF000). The
app area is 0x04000..0x9CFFF (0xA000 more than the FM-1's). Inside the windows fm1-base gives an app, two sectors
are the board's own and an app must leave them alone: the **autosave pair** 0xA1000..0xA2FFF and the **store's owner
tag** at 0xAF000 ("SMK37DATA" + a firmware family: fm1-base clears every window at the first boot after another
firmware's, so no firmware parses another's data; LOOM-8's family is `LOOM8`, app.h `APP_DATA_FAMILY`).

What fm1-base gives an app on the SMK, against the FM-1:

| Window | FM-1 | SMK-37 |
| --- | --- | --- |
| the data area (the store: `APP_STORE_MAP`, and `plat_data`) | 0x97000..0xDFFFF, 73 sectors | 0xC2000..0xE9FFF, 40 sectors |
| `PLAT_XDATA0` (big data, above the app area) | 0x93000..0x96FFF, 16 KB | 0x9D000..0xA0FFF, 16 KB |
| `PLAT_XDATA1` (big data) | 0xE5000..0xFBFFF, 92 KB | 0xA1000..0xBAFFF, 104 KB, the autosave pair and the owner tag inside |
| the update's staging (fm1-base's) | 0xE0000..0xE4FFF | 0xBB000..0xBFFFF |
| the settings pair (`PLAT_GLOB`, OBJ_BLE) | 0xFC000 | 0xEB000 |

LOOM-8's map on each board (`app/app.h`, `app/dsp/sample.h`; the SMK's checked against the board's protected
sectors by `tests/host/smk37_flash_test.c`, run by `tests/run_tests.sh`):

| LOOM-8's data | FM-1 | SMK-37 | Why it moved |
| --- | --- | --- | --- |
| the projects' pool (three slots' 21 objects, fm1-base storage.c) | 0x97000, 20 sectors | 0xC2000, 14 sectors | the SMK's data area has 33 sectors fewer: 14 is what is left beside the region and the pairs. Three projects of 7.6 KB packed always fit (the FM-1: 15 KB); past that, MEMORY FULL |
| the resource region (the 909 cymbals 71 KB, the large type 16 KB) | 0xAB000, 22 sectors | 0xD0000, 22 sectors | fm1-base's res.c keeps it in the data area |
| the panel calibration, the user presets (a pair each) | 0xDC000, 0xDE000 | 0xE6000, 0xE8000 | after the region, at the data area's end |
| OBJ_BLE (the radio's address and pairing) | 0xFC000 | 0xEB000 (`PLAT_GLOB`) | the SMK's settings pair |
| the user sample bank, window 0 (its header first) | 0xC1000, 108 KB | 0xA3000, 48 KB | the data area has no room left: the extra windows only, above the autosave pair and below the owner tag |
| the bank, window 1 | 0xE5000, 92 KB (`PLAT_XDATA1`) | 0xB0000, 44 KB | above the owner tag |
| the bank, window 2 | 0x93000, 16 KB (`PLAT_XDATA0`) | 0x9D000, 16 KB (`PLAT_XDATA0`) | the same window, where the SMK has it |

So the sample bank is 108 KB on the SMK against the FM-1's 216 KB: LOOM-8's own sounds (the factory set, 86 KB)
fit, with about 20 KB left for the player's own samples. All three of the SMK's windows are extra windows, so no
sector of the bank may hold an update record's mark (`SMP_XDATA_FIRST`, `tools/sampleio.py` `XDATA`). The tools
and the pages lay a bank out for the board they see: `tools/loom_samples.py --board=smk37` (or `LOOM_BOARD=smk37`),
and the samples, installer and backup pages pick the board from the port's name (an SMK build's USB product string
ends in "SMK-37").

The pool at 14 sectors was chosen over dropping the large type from the resource region (which would give 18): both
are one line in app.h. No A/B pairs ever existed on the SMK, so `APP_STORE_POOL_OLD` is the pool's size and nothing
moves at the first boot. Nothing of LOOM-8's lies at the same offset on both boards: what one board saved does not
carry over to the other; the backup page does that.

## The controls

The FM-1's names stay on the screen and in the manual; the SMK prints other words on the same buttons (fm1-base's
map: FX, SCALE as SEL, CHORD as ENV, NOTE REPEAT as LFO, PARA as EDIT, GLOBE as GLO, PATCH as HOME, BT as SAVE, ARP,
SEQ, SEQ PLAY as PLAY, SEQ REC as REC, OCTAVE-/+). HARDWARE CALIBRATION in Setup re-teaches them. The seven knob
roles are on K6 (SELECT), K7 (ALGORITHM), K8 (PRESETS) and K1..K4; K5 is a virtual MASTER.

| SMK-37 | In LOOM-8 (`app/smk37.c`) |
| --- | --- |
| the 16 pads | the FM-1's 16 white keys: the steps on Steps and Bar (a tap writes the remembered notes, again clears), the scenes on Arrange and Song, a kit's sounds with a step held, SAVE's slots and presets, the throws. Top row 1-8, bottom 9-16. Their lights are the white keys': the track's ink, lit, mid (a step with notes), dim (a scene that exists). A pad's velocity is the key's where the pads play (Mix, Setup...) |
| the keybed's white keys | always play the track on the screen, with velocity; never a step. In step record each key writes the cursor step at its velocity |
| the keybed's black keys F#3..F#5 | the FM-1's eleven black keys: a kit's sounds (Steps), the tracks' mutes (Arrange), mute / solo (Mix), the shared LFO's tracks, a bar's jump (Bar with SEQ held); they play elsewhere, as on the FM-1 |
| the keys outside F3..G5 (C3..E3, G#5..C6) | play, on the same octave |
| < and > | the bar shown (Steps, Bar, a weave); the scenes 1-16 / 17-32 on the pads (Arrange, Song); back and a rest in step record; the page before / after elsewhere |
| OCTAVE- / OCTAVE+ | the keys' octave on every page (a held step: its nudge; SEQ held on Steps: the pattern's octave) |
| the faders | the levels of tracks 1-4; with FADER BANK held, 5-8. A fader counts once it moves, so a saved level stands until then. Recorded while recording, as a knob is |
| the pitch wheel, the mod wheel | the track on the screen's bend (±2 semitones, as MIDI pitch bend) and its CC 1 (the LFO amount) |
| the sustain pedal | holds the keybed's notes until it lifts (plug it in before switching on: fm1-base reads its rest level at power-on) |
| KNOB BANK | nothing yet |
| the TRS jack | MIDI OUT: everything USB gets (the MIDI OUT engine, CC OUT, the clock; fm1-base's `TO_TRS`). The FM-1's jack is MIDI IN; the SMK has no MIDI IN jack, so a controller comes in over USB (the host port, through a powered hub) or Bluetooth |

Help cards (a button held alone a moment) have the SMK's words for < > and the banks.

## Tests

`tests/run_tests.sh` runs, beside the FM-1's: the SMK's flash map against the board's protected sectors
(`tests/host/smk37_flash_test.c`), the bank tool's SMK layout (`tests/host/bank_test.py`), the SMK simulator's build and
its scenarios (`tests/scenarios-smk37/`: the pads, the keybed, < >, the faders and wheels, the store). The
simulator's script has the SMK's words (fm1-base `host/plat_host.c`): `key37 K down VEL`, `pad P down VEL`,
`fader I V`, `wheel pitch|mod V`, `pedal down|up`, `padleds`, and the buttons `KBANK`, `FBANK`, `LEFT`, `RIGHT`.

## Not done

- Nothing tried on an SMK-37 (or an FM-1 with this base).
- No installer page for the SMK-37 on the site yet (`tools/make_pages.py` builds the FM-1's; RELAY-16's
  `tools/make_pages.py` shows how a page per model is made from fm1-base's installer).
- KNOB BANK has no job. The screen says "FM-1" in a few places.
- The 10 keys outside the FM-1's 27 do nothing on the WEAVE and THROW tracks (the FM-1's 27 do their jobs there).

#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build LOOM-8 with fm1-base (base/, a git submodule) (see BUILDING.md).
#   ./build.sh [--release X.Y] [--host | --web]   (screen mirroring in every build: base/web/mirror, the site's mirror/;
#                                                  --web: fm1-base's emulator page, app/web/emu/ its LOOM-8 bits)
#   BOARD=smk37 ./build.sh ...  the M-VAVE SMK-37 instead of the FM-1 (docs/SMK37.md): app/build/loom-smk37.fwsc,
#                               MODEL=pro (default) | elite | mke for the SMK-37 Pro, Elite or MKE-P37
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow (and libraqm, for the fonts; on macOS Homebrew's)
set -e
case $0 in */*) cd -- "${0%/*}";; esac      # (the repository: this script's folder, wherever it is called from)
: "${JIELI_TOOLCHAIN:=$HOME/.jieli/toolchain}" "${AC79_SDK:=$HOME/fw-AC79_AIoT_SDK}"
export JIELI_TOOLCHAIN AC79_SDK
PY="${PYTHON:-python3}"
case "${BOARD:-fm1}" in
    fm1) ;;
    smk37) export FM1B_BOARD=smk37 FM1B_MODEL="${MODEL:-pro}"
           # the SMK's board layer takes ~1.6 KB more RAM than the FM-1's: the pool's margin fm1.toml insists on (4 KB)
           # is 3 KB there (fm1-base's build check of what is left, not a runtime reserve: docs/SMK37.md)
           export FM1B_POOL_SPARE="${FM1B_POOL_SPARE:-3072}";;
    *) echo "build.sh: BOARD=$BOARD: fm1 or smk37"; exit 1;;
esac
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
case " $* " in
    *" --web "*)                            # the emulator: fm1-base's page with LOOM-8's bits (tools/emu_page.py)
        "$PY" base/tools/build.py app "$@"
        exec "$PY" tools/emu_page.py;;
esac
exec "$PY" base/tools/build.py app "$@"

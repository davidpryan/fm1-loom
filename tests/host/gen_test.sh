#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# LOOM-8's generators (SEQ's Gen page): build and run tests/host/gen_test.c against app/gen.c, seq.c and pattern.c.
#   sh tests/host/gen_test.sh
set -eu
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
mkdir -p build/host
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST \
    -Iapp -Iapp/seq -Iapp/dsp -Ibase/include \
    -o build/host/gen_test tests/host/gen_test.c app/seq/seq.c app/seq/pattern.c
build/host/gen_test

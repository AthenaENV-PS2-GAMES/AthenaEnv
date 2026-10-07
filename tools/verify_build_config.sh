#!/bin/sh
# Run in the PS2 toolchain container, from the repository root.
set -eu
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/athena-build-config-tests
mkdir -p "$OUT"
OBJECT=obj/quickjs-debug/core/strUtils.o
make DEBUG=1 "$OBJECT" > "$OUT/initial.log" 2>&1
before=$(stat -c '%y' "$OBJECT")
make DEBUG=1 "$OBJECT" > "$OUT/reuse.log" 2>&1
after=$(stat -c '%y' "$OBJECT")
test "$before" = "$after"
make DEBUG=1 EE_OPTFLAGS=-O1 "$OBJECT" > "$OUT/changed.log" 2>&1
after=$(stat -c '%y' "$OBJECT")
test "$before" != "$after"
make DEBUG=1 "$OBJECT" > "$OUT/restored.log" 2>&1
restored=$(stat -c '%y' "$OBJECT")
test "$restored" != "$after"
echo 'build_config: unchanged flags reuse objects; -O1 and restoration invalidate them'

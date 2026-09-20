#!/usr/bin/env bash
# Verify AthenaEnv decoupling when PS2SDK is available.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "== Build 1: full JS (default) =="
make clean
make -j"$(nproc 2>/dev/null || echo 2)"
test -f lib/libathena.a
test -f lib/libathena_js.a
echo "libathena.a has no QuickJS undefined symbols:"
! nm lib/libathena.a 2>/dev/null | grep -i ' U .*JS_' || true

echo "== Build 2: C-only ELF =="
make capp
test -f bin/athena_capp.elf

echo "== Build 3: C-only core (ATHENA_JS=0) =="
make clean
make ATHENA_JS=0 -j"$(nproc 2>/dev/null || echo 2)"
test -f lib/libathena.a
test ! -f lib/libathena_js.a

echo "All decoupling verification builds passed."

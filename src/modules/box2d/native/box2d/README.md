# Box2D 3.2.0 (vendored)

Erin Catto's Box2D (MIT, see `LICENSE`), version 3.2.0: the `src/` files of
the upstream repository, with the public headers in the module's
`include/box2d/`. It is compiled as part of the Box2D module; C applications
include `<box2d/box2d.h>` (and `<athena/box2d.h>` for the AthenaEnv helpers).

The EE builds the scalar code path: MIPS is neither x86 nor ARM, so
`core.h` selects `B2_SIMD_NONE` without `BOX2D_DISABLE_SIMD`. The thread
primitives of `timer.c` are the stubs for unknown platforms, which is why
worlds must run on one worker.

Local changes (each marked `AthenaEnv:` in the source):

- `atomic.h`: plain loads and stores on the EE (`PS2`, `_EE`, `__R5900__`).
  The R5900 has no LL/SC, so GCC lowers `__atomic_*` to libatomic calls the
  PS2SDK does not provide. Safe because every world runs on one worker.
- `include/box2d/base.h`, `core.c`: on the EE assertions follow the build
  type (on with `DEBUG=1`, off in release) through `B2_ASSERT_ENABLED`, as
  the PS2 build never defines `NDEBUG`. Defined in the public header so every
  file including Box2D agrees. Elsewhere the upstream rule applies.
- `timer.c`: `b2GetTicks()` reads the EE cycle counter (COP0 Count,
  294.912 MHz) instead of returning 0, so `b2World_GetProfile()` works.
- `include/box2d/constants.h`: `B2_MAX_WORLDS` is 8 instead of 128. Every
  slot is a static `b2World` (about 1.9 KB on the EE), so 128 slots cost
  about 242 KB of BSS in every build with the module. It is still
  overridable with `-DB2_MAX_WORLDS=n`.
- `world_snapshot.c`/`.h`: `b2IsValidSnapshotImage()` exposes the header
  checks of `b2World_Restore`, so an image can be rejected while the world
  is intact; the chain, sensor and island arrays skip `memset` for zero
  elements (`memset(NULL, 0, 0)` is undefined behavior, caught by UBSan).
- `arena_allocator.c`: the unused `b2Array_Pop()` result is cast to `void`
  (`-Wunused-value` once release builds compile assertions out).
- `core.h`: `B2_UNUSED()` passes its arguments to an unevaluated call
  instead of a comma expression, which GCC reports as `-Wunused-value` on
  every use.
- `physics_world.c`, `recording_replay.c`: `uint32_t` arguments of `%u`/`%X`
  are cast to `unsigned` (`uint32_t` is `long` on the EE, `-Wformat`).

The module's `init` hook (`athena_box2d_module_init`) installs an allocator
and an assertion handler that stop on the AthenaEnv crash screen with the
failed condition (or the allocation size) instead of a bare trap. The script
bindings validate their input so scripts cannot reach an assertion; see
`athena_box2d_*` in `<athena/box2d.h>` for the rules shared with C
applications.

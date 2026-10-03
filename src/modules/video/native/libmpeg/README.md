# libmpeg (vendored)

MPEG-1/2 decoder from ps2sdk (`ee/mpeg`, Eugene Plotnikov, AFL 2.0), taken from
the AthenaEnv ps2sdk fork (`fix-libmpeg` branch, which fixes ABI/`$at` register
bugs in `libmpeg_core.s`). It is compiled as part of the Video module instead
of linking the toolchain's `libmpeg.a`.

Local changes, on top of the fork:

- `libmpeg_core.c` `_req_data()`: end of data is sticky. The end code used to
  be injected again on every call, so a byte-by-byte start code search never
  matched and looped forever on streams without a trailing `0x000001B7`.
- `libmpeg.c` `_init_seq()` / `_get_first_picture()`: the init callback runs
  before the frame arena is allocated and may refuse the sequence by
  returning `NULL`; a failed arena allocation is reported the same way. The
  picture then fails instead of being decoded to address 0.
  `MPEG_AllocFailed()` tells the two cases apart.
- `libmpeg.c` `_destroy_seq()`: clears `m_pFrameArena` after freeing it
  (double free on the next `MPEG_Destroy()` otherwise).
- `MPEGSequenceInfo`: new `m_FrameRate` (exact rate; `m_MSPerFrame` is
  rounded) and `m_DisplayWidth`/`m_DisplayHeight` (picture size;
  `m_Width`/`m_Height` are rounded up to macroblocks). A forbidden
  `frame_rate_code` gives a rate of 0 instead of dividing by zero.
- `libmpeg_internal.h` includes `"libmpeg.h"` so this copy of the header is
  used instead of the toolchain's.

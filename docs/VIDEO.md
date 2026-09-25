# Video playback

The `video` module plays MPEG-1/2 video on the PS2's IPU (the hardware MPEG
decoder) through a vendored copy of ps2sdk's `libmpeg`. A frame can be drawn
directly, cropped, rotated or tinted, or used as an `Image` texture.

## Preparing a video

The player reads a **raw MPEG-1/2 elementary video stream** (`.m2v`): video
only, no container (MP4/MKV/AVI/MPEG-PS) and no audio. Requirements:

- **4:2:0 chroma.** 4:2:2 and 4:4:4 are refused.
- **At most 1024x1024** after rounding up to 16x16 macroblocks (the largest
  texture the GS samples).
- A standard frame rate (23.976, 24, 25, 29.97, 30, 50, 59.94 or 60 fps).
- No MPEG-2 scalable extensions.

```bash
ffmpeg -i input.mp4 -vf scale=640:360 -c:v mpeg2video -b:v 2000k -g 15 -an video.m2v
# its soundtrack, for Video.audio (see below):
ffmpeg -i input.mp4 -vn -c:a libvorbis -q:a 4 video.ogg
```

- Keep the size modest (640x360 or smaller) and the bitrate within a few
  Mbps: the EE converts and uploads every frame as a texture on top of
  running your script.
- `-g 15` (a short GOP) keeps loops and restarts quick.
- Heights that are not a multiple of 16 are fine: the decoded texture is
  rounded up (`frame.texHeight` is 368 for a 360-line video) and the padding
  is never drawn.

`Video.probe(path)` tells whether a file will play before opening it:

```js
const info = Video.probe("intro.m2v");
// { width: 640, height: 360, codedWidth: 640, codedHeight: 368, fps: 60,
//   frames: 3517, duration: 58.6, mpeg2: true, progressive: true,
//   chroma: "4:2:0", supported: true }
```

It reads the headers on the CPU, so it works while another video is open,
but it reads the whole file to count frames: expect a delay on disc.

## Playing

```js
const video = new Video("intro.m2v");
video.onEnd = () => console.log("done");
video.play();

while (!video.ended) {
    Screen.clear(Color.new(0, 0, 0));
    video.update();               // decodes the frames that are due
    video.draw(0, 44, 640, 360);  // omitted/0 size: the picture's own
    Screen.flip();
}
video.free();
```

- `update()` follows the stream's frame rate whatever your loop's rate: a
  60 fps video plays at 60 fps in a 30 fps loop (two frames per call). When
  the loop stalls, up to 3 late frames are decoded per call and the rest of
  the delay is dropped, so playback does not fast-forward afterwards.
- **Only one Video can be open at a time**: the IPU decoder is a single
  hardware resource. `free()` the current one before opening another;
  otherwise the constructor throws with `error.code === "busy"`.
- `loop = true` restarts from the beginning instead of ending; `loopCount`
  counts the restarts.
- `stop()` rewinds; `play()` after the end starts over.

### With sound

The player decodes video only. Put the soundtrack in its own file (WAV or
Ogg Vorbis, see the ffmpeg line above) and attach it as a `Sound.Stream`:

```js
const video = new Video("intro.m2v");
const music = Sound.Stream("intro.ogg");
video.audio = music;       // before play()
video.play();              // starts both
while (!video.ended) {
    Screen.clear(Color.new(0, 0, 0));
    video.update();        // picks the picture for music.position
    video.draw(0, 44, 640, 360);
    Screen.flip();
}
video.free();
music.free();              // the stream stays yours
```

- The audio is the clock: the picture shown is the one for the time
  heard (`audio.position`). Audio stalls on slow storage, pauses and
  emulator speed keep both in step; late pictures are dropped to catch up.
- `play()`, `pause()` and `stop()` drive the stream too (`play()`
  rewinds it when the video starts over) and `loop` sets its `loop`.
- Looping follows the audio: the video restarts when the audio wraps. A
  video shorter than its audio holds its last picture until then.
- When the audio has ended, the remaining pictures play on the EE clock, so
  `ended` always comes.
- `audio` can only be changed while the video is not playing; `null`
  detaches it. Do not seek the stream while attached: seeking back restarts
  the video and seeking ahead makes it decode every picture up to there.
- Any object with `position`, `ended`, `loop`, `play()`, `pause()`,
  `stop()` and `rewind()` works, so the Video module does not depend on
  Sound.

### Events

`update()` calls `onLoop(loopCount)` when a looping video restarts and
`onEnd()` when playback ends, with the Video as `this`. An exception thrown
by a handler is thrown by `update()`.

### Drawing

```js
video.draw(x, y);                        // picture size
video.draw(x, y, width, height);         // scaled
video.draw(x, y, {                       // any of:
    width: 320, height: 180,             //   destination size
    startx: 160, starty: 90,             //   source rectangle, in picture pixels
    endx: 480, endy: 270,
    angle: 0.5,                          //   rotation, radians
    color: Color.new(255, 255, 255, 64), //   tint; alpha 64 is half transparent
});
```

A source rectangle that is empty or goes outside the picture throws a
`RangeError`.

### As a texture

`video.frame` is an `Image` showing the current frame. It is the same object
on every access and follows playback, so set its size, angle or color once:

```js
const frame = video.frame;
frame.width = 128;
frame.height = 72;
// every frame, after video.update():
frame.draw(10, 10);
```

The Image borrows the decoder's buffer: changing its `pixels`, `palette`,
`bpp`, `texWidth` or `texHeight` throws a `TypeError`. After `video.free()`
it is no longer loaded (`ready()` is false and `draw()` throws).

## Errors

`new Video()` and `Video.probe()` throw an `InternalError` with a stable
`error.code`:

| `code` | Meaning |
|---|---|
| `open_failed` | The file could not be opened. |
| `busy` | Another Video is open (constructor only). |
| `invalid_format` | Not an MPEG-1/2 elementary video stream, or nothing decodable. |
| `unsupported_format` | Valid stream the player refuses: size, chroma or frame rate (constructor only; `probe()` reports `supported: false`). |
| `out_of_memory` | The decoder buffers could not be allocated. |
| `thread_failed` | The decoder thread could not be started (constructor only). |

## Limits

- Frames are decoded ahead by a "Video decoder" thread one priority below
  the script, which runs while the script waits for vsync in
  `Screen.flip()`. `update()` then only swaps in the frame that is due.
  A loop that never waits (vsync off, heavy scripts) leaves it no time: each
  `update()` then waits up to one frame period for a frame, so playback
  slows down instead of freezing.
- Up to four frame buffers (three above 8 MB of frames, e.g. 1024x1024):
  the shown frame, the previous one (its upload to the GS completes on the
  next flip) and frames decoded ahead. Call `update()` once per rendered
  frame.
- While a Video is open the decoder owns the IPU, DMA channels 3/4 and the
  scratchpad.
- The audio comes from a separate file (no demuxing of MPEG-PS/PSS).
- No seeking: playback starts from the beginning.
- A few frames still in the decoder when a looping video wraps are counted
  in the new pass of `currentFrame`.

## C API

Native applications (`RUNTIME=native`) use `<athena/video.h>`:
`athena_video_create()`, `athena_video_update()`, `athena_video_draw()` /
`athena_video_draw_ex()`, `athena_video_take_events()`,
`athena_video_probe()` and `athena_video_destroy()`. For sound, call
`athena_video_set_synced(video, true)` and, each frame,
`athena_video_update_synced(video, athena_sound_stream_get_position(s),
athena_sound_stream_ended(s))` instead of `athena_video_update()`; play,
pause and stop the stream alongside the video (see `<athena/video.h>`). A video left open is
released by the module's shutdown hook.

## Troubleshooting

- **`invalid_format`:** the file is probably a container (`.mp4`, `.mpg`
  program stream with audio). Extract the video stream with ffmpeg (`-an`).
- **`unsupported_format`:** check `Video.probe()`: `chroma` must be
  `4:2:0` and `codedWidth`/`codedHeight` at most 1024.
- **Choppy playback:** lower the resolution or bitrate; the frame rate is
  kept by skipping frames when decoding cannot keep up.

Tests: `bin/tests/video_test.js` (PCSX2/console, fixtures generated by
`bin/tests/video/make_fixtures.js`) and `tests/host/video_test.c`
(`docker compose run --rm host-tests`). Changes made to the vendored decoder
are listed in `src/modules/video/native/libmpeg/README.md`.

#ifndef ATHENA_VIDEO_H
#define ATHENA_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include <graphics.h>
#include <mpeg_player.h>

typedef struct AthenaVideo {
    MPEGPlayer *player;
} AthenaVideo;

AthenaVideo *athena_video_create(const char *path);
void athena_video_destroy(AthenaVideo *video);

void athena_video_play(AthenaVideo *video);
void athena_video_pause(AthenaVideo *video);
void athena_video_stop(AthenaVideo *video);
bool athena_video_update(AthenaVideo *video);
void athena_video_draw(AthenaVideo *video, float x, float y, float w, float h);

int athena_video_get_width(const AthenaVideo *video);
int athena_video_get_height(const AthenaVideo *video);
float athena_video_get_fps(const AthenaVideo *video);
bool athena_video_is_ready(const AthenaVideo *video);
bool athena_video_is_ended(const AthenaVideo *video);
bool athena_video_is_playing(const AthenaVideo *video);
bool athena_video_get_loop(const AthenaVideo *video);
void athena_video_set_loop(AthenaVideo *video, bool loop);
GSSURFACE *athena_video_get_texture(AthenaVideo *video);
int athena_video_get_current_frame(const AthenaVideo *video);

#endif /* ATHENA_VIDEO_H */

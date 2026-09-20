#include <stdlib.h>

#include <athena/video.h>

AthenaVideo *athena_video_create(const char *path)
{
    AthenaVideo *video;

    if (!path)
        return NULL;

    video = calloc(1, sizeof(*video));
    if (!video)
        return NULL;

    video->player = mpeg_player_create(path);
    if (!video->player) {
        free(video);
        return NULL;
    }

    return video;
}

void athena_video_destroy(AthenaVideo *video)
{
    if (!video)
        return;

    if (video->player)
        mpeg_player_destroy(video->player);

    free(video);
}

void athena_video_play(AthenaVideo *video)
{
    if (video && video->player)
        mpeg_player_play(video->player);
}

void athena_video_pause(AthenaVideo *video)
{
    if (video && video->player)
        mpeg_player_pause(video->player);
}

void athena_video_stop(AthenaVideo *video)
{
    if (video && video->player)
        mpeg_player_stop(video->player);
}

bool athena_video_update(AthenaVideo *video)
{
    return video && video->player && mpeg_player_update(video->player);
}

void athena_video_draw(AthenaVideo *video, float x, float y, float w, float h)
{
    if (video && video->player)
        mpeg_player_draw(video->player, x, y, w, h);
}

int athena_video_get_width(const AthenaVideo *video)
{
    return (video && video->player) ? mpeg_player_get_width(video->player) : 0;
}

int athena_video_get_height(const AthenaVideo *video)
{
    return (video && video->player) ? mpeg_player_get_height(video->player) : 0;
}

float athena_video_get_fps(const AthenaVideo *video)
{
    return (video && video->player) ? mpeg_player_get_fps(video->player) : 0.0f;
}

bool athena_video_is_ready(const AthenaVideo *video)
{
    return video && video->player && mpeg_player_is_ready(video->player);
}

bool athena_video_is_ended(const AthenaVideo *video)
{
    return !video || !video->player || mpeg_player_is_ended(video->player);
}

bool athena_video_is_playing(const AthenaVideo *video)
{
    return video && video->player &&
           mpeg_player_get_state(video->player) == MPEG_STATE_PLAYING;
}

bool athena_video_get_loop(const AthenaVideo *video)
{
    return video && video->player && video->player->loop;
}

void athena_video_set_loop(AthenaVideo *video, bool loop)
{
    if (video && video->player)
        video->player->loop = loop;
}

GSSURFACE *athena_video_get_texture(AthenaVideo *video)
{
    return (video && video->player) ? mpeg_player_get_texture(video->player) : NULL;
}

int athena_video_get_current_frame(const AthenaVideo *video)
{
    return (video && video->player) ? video->player->current_frame : 0;
}

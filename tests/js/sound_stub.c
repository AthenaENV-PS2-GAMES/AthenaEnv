/*
 * Sound stand-in for the Audio3D binding in the JS runner: __fakeSfx() makes
 * objects that athena_sfx_js_peek() accepts, the voice functions record what
 * they are given, and __soundLog() returns { plays, levels: [[channel,
 * volume, pan], ...], stops } since the last call. A voice "plays" until
 * stopped or until __soundEnd(channel).
 */
#include <stdbool.h>
#include <string.h>
#include <quickjs.h>
#include <athena/sound.h>
#include <athena/js/sound.h>

struct AthenaSfx { int volume, pan; };
static JSClassID fake_class;
static struct AthenaSfx *voice_owner[24];
static int plays, stops, level_count, levels[64][3];

AthenaSfx *athena_sfx_js_peek(JSValueConst value) { return fake_class ? JS_GetOpaque(value, fake_class) : NULL; }
int athena_sfx_get_volume(const AthenaSfx *s) { return s->volume; }
int athena_sfx_get_pan(const AthenaSfx *s) { return s->pan; }
int athena_sfx_set_volume(AthenaSfx *s, int v) { s->volume = v; return 0; }
int athena_sfx_set_pan(AthenaSfx *s, int p) { s->pan = p; return 0; }
int athena_sfx_play(AthenaSfx *s, int channel) {
    (void)channel;
    for (int i = 0; i < 24; i++) if (!voice_owner[i]) {
        voice_owner[i] = s; plays++;
        if (level_count < 64) { levels[level_count][0] = i; levels[level_count][1] = s->volume; levels[level_count][2] = s->pan; level_count++; }
        return i;
    }
    return -1;
}
int athena_sfx_stop(AthenaSfx *s, int channel) {
    if (channel >= 0 && channel < 24 && voice_owner[channel] == s) { voice_owner[channel] = NULL; stops++; }
    return 0;
}
int athena_sfx_set_channel_levels(AthenaSfx *s, int channel, int volume, int pan) {
    if (channel < 0 || channel >= 24 || voice_owner[channel] != s) return 0;
    if (level_count < 64) { levels[level_count][0] = channel; levels[level_count][1] = volume; levels[level_count][2] = pan; level_count++; }
    return 1;
}
const char *athena_sound_result_string(int result) { (void)result; return "stub"; }

static void fake_finalizer(JSRuntime *rt, JSValue v) {
    (void)rt; struct AthenaSfx *s = JS_GetOpaque(v, fake_class);
    for (int i = 0; i < 24; i++) if (voice_owner[i] == s) voice_owner[i] = NULL;
    js_free_rt(rt, s);
}
static JSClassDef fake_def = { "FakeSfx", .finalizer = fake_finalizer };
static JSValue js_fake_sfx(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    JSValue obj = JS_NewObjectClass(ctx, fake_class);
    struct AthenaSfx *s = js_mallocz(ctx, sizeof(*s));
    s->volume = 100; JS_SetOpaque(obj, s); return obj;
}
static JSValue js_sound_log(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    JSValue o = JS_NewObject(ctx), list = JS_NewArray(ctx);
    for (int i = 0; i < level_count; i++) {
        JSValue t = JS_NewArray(ctx);
        for (int k = 0; k < 3; k++) JS_SetPropertyUint32(ctx, t, k, JS_NewInt32(ctx, levels[i][k]));
        JS_SetPropertyUint32(ctx, list, i, t);
    }
    JS_SetPropertyStr(ctx, o, "plays", JS_NewInt32(ctx, plays));
    JS_SetPropertyStr(ctx, o, "stops", JS_NewInt32(ctx, stops));
    JS_SetPropertyStr(ctx, o, "levels", list);
    plays = stops = level_count = 0;
    return o;
}
static JSValue js_sound_end(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; int ch = 0;
    if (argc > 0) JS_ToInt32(ctx, &ch, argv[0]);
    if (ch >= 0 && ch < 24) voice_owner[ch] = NULL;
    return JS_UNDEFINED;
}
void sound_stub_init(JSContext *ctx) {
    if (!fake_class) JS_NewClassID(&fake_class);
    if (!JS_IsRegisteredClass(JS_GetRuntime(ctx), fake_class)) JS_NewClass(JS_GetRuntime(ctx), fake_class, &fake_def);
    memset(voice_owner, 0, sizeof(voice_owner));
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "__fakeSfx", JS_NewCFunction(ctx, js_fake_sfx, "__fakeSfx", 0));
    JS_SetPropertyStr(ctx, g, "__soundLog", JS_NewCFunction(ctx, js_sound_log, "__soundLog", 0));
    JS_SetPropertyStr(ctx, g, "__soundEnd", JS_NewCFunction(ctx, js_sound_end, "__soundEnd", 1));
    JS_FreeValue(ctx, g);
}

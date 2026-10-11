#include <string.h>
#include <athena/profiler.h>
#include <athena/float_bits.h>
#if defined(__mips__)
/* COP0 Count: the R5900 core clock, 294.912 MHz. One instruction, no
 * syscall (GetTimerSystemTime() goes through the kernel). */
#define TICKS_PER_MS 294912.0f
uint32_t athena_profiler_ticks(void) { uint32_t count; __asm__ __volatile__("mfc0 %0, $9" : "=r"(count)); return count; }
#else
#include <time.h>
#define TICKS_PER_MS 1000.0f /* microseconds: wraps after 71 minutes */
uint32_t athena_profiler_ticks(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint32_t)((uint64_t)ts.tv_sec*1000000u+(uint64_t)ts.tv_nsec/1000u);
}
#endif
float athena_profiler_ticks_to_ms(uint32_t ticks) { return (float)ticks/TICKS_PER_MS; }

typedef struct {
    char name[ATHENA_PROFILER_NAME_MAX+1];
    uint8_t kind;
    uint64_t ticks;      /* timer total of the current frame */
    float value;         /* counter total of the current frame */
    uint32_t calls;
    float history[ATHENA_PROFILER_HISTORY];
    uint16_t calls_history[ATHENA_PROFILER_HISTORY];
} Scope;
typedef struct { int id; uint32_t start; } Open;
static Scope scopes[ATHENA_PROFILER_MAX_SCOPES];
static uint32_t scope_count;
static Open stack[ATHENA_PROFILER_MAX_DEPTH];
static uint32_t depth,head,filled,errors,frame_start;
static int started;

static void register_frame(void) {
    if(scope_count) return;
    memcpy(scopes[0].name,"frame",6); scopes[0].kind=ATHENA_PROFILER_TIMER; scope_count=1;
}
static int valid_name(const char *name) {
    if(!name||!name[0]) return 0;
    size_t n=0;
    for(;name[n];n++) if(n>=ATHENA_PROFILER_NAME_MAX||(unsigned char)name[n]<32||(unsigned char)name[n]>126) return 0;
    return 1;
}
int athena_profiler_find(const char *name) {
    register_frame();
    if(!valid_name(name)) return ATHENA_PROFILER_EINVAL;
    for(uint32_t i=0;i<scope_count;i++) if(!strcmp(scopes[i].name,name)) return (int)i;
    return ATHENA_PROFILER_EINVAL;
}
int athena_profiler_scope(const char *name,AthenaProfilerKind kind) {
    if(kind!=ATHENA_PROFILER_TIMER&&kind!=ATHENA_PROFILER_COUNTER) return ATHENA_PROFILER_EINVAL;
    int id=athena_profiler_find(name);
    if(id>=0) return scopes[id].kind==kind?id:ATHENA_PROFILER_EKIND;
    if(!valid_name(name)) return ATHENA_PROFILER_EINVAL;
    if(scope_count>=ATHENA_PROFILER_MAX_SCOPES) return ATHENA_PROFILER_EFULL;
    Scope *s=&scopes[scope_count];
    memset(s,0,sizeof(*s)); strcpy(s->name,name); s->kind=(uint8_t)kind;
    return (int)scope_count++;
}
static int timer_id(int id) { return id>0&&(uint32_t)id<scope_count&&scopes[id].kind==ATHENA_PROFILER_TIMER; }
int athena_profiler_begin(int id) {
    if(!timer_id(id)) return ATHENA_PROFILER_EINVAL;
    if(depth>=ATHENA_PROFILER_MAX_DEPTH) { errors++; return ATHENA_PROFILER_EDEPTH; }
    if(!started) { started=1; frame_start=athena_profiler_ticks(); }
    scopes[id].calls++;
    stack[depth].id=id; stack[depth].start=athena_profiler_ticks(); depth++;
    return 0;
}
int athena_profiler_end(int id) {
    uint32_t now=athena_profiler_ticks();
    if(id>=0&&!timer_id(id)) return ATHENA_PROFILER_EINVAL;
    if(!depth||(id>=0&&stack[depth-1].id!=id)) { errors++; return ATHENA_PROFILER_EOPEN; }
    depth--;
    scopes[stack[depth].id].ticks+=(uint32_t)(now-stack[depth].start);
    return 0;
}
int athena_profiler_count(int id,float value) {
    if(id<=0||(uint32_t)id>=scope_count||scopes[id].kind!=ATHENA_PROFILER_COUNTER||!athena_float_isfinite(value))
        return ATHENA_PROFILER_EINVAL;
    scopes[id].value+=value; scopes[id].calls++;
    return 0;
}
float athena_profiler_frame(void) {
    register_frame();
    uint32_t now=athena_profiler_ticks();
    if(!started) { started=1; frame_start=now; }
    /* Open scopes: the time so far belongs to this frame. */
    for(uint32_t i=0;i<depth;i++) { scopes[stack[i].id].ticks+=(uint32_t)(now-stack[i].start); stack[i].start=now; }
    scopes[0].ticks=(uint32_t)(now-frame_start); scopes[0].calls=1; frame_start=now;
    for(uint32_t i=0;i<scope_count;i++) {
        Scope *s=&scopes[i];
        s->history[head]=s->kind==ATHENA_PROFILER_TIMER?(float)s->ticks/TICKS_PER_MS:s->value;
        s->calls_history[head]=s->calls>0xffffu?0xffffu:(uint16_t)s->calls;
        s->ticks=0; s->value=0; s->calls=0;
    }
    float frame_ms=scopes[0].history[head];
    head=(head+1)%ATHENA_PROFILER_HISTORY;
    if(filled<ATHENA_PROFILER_HISTORY) filled++;
    return frame_ms;
}
int athena_profiler_stats(int id,uint32_t frames,AthenaProfilerStats *out) {
    register_frame();
    if(id<0||(uint32_t)id>=scope_count||!out) return ATHENA_PROFILER_EINVAL;
    const Scope *s=&scopes[id];
    uint32_t n=frames&&frames<filled?frames:filled;
    memset(out,0,sizeof(*out));
    out->name=s->name; out->kind=(AthenaProfilerKind)s->kind; out->samples=n;
    if(!n) return 0;
    /* Newest first, sorted by insertion for the percentile. */
    float sorted[ATHENA_PROFILER_HISTORY],sum=0; uint32_t calls=0;
    for(uint32_t i=0;i<n;i++) {
        uint32_t at=(head+ATHENA_PROFILER_HISTORY-1-i)%ATHENA_PROFILER_HISTORY;
        float v=s->history[at]; sum+=v; calls+=s->calls_history[at];
        uint32_t j=i; while(j&&sorted[j-1]>v) { sorted[j]=sorted[j-1]; j--; }
        sorted[j]=v;
        if(!i) { out->last=v; out->last_calls=s->calls_history[at]; }
    }
    uint32_t rank=(n*95+99)/100; /* nearest rank */
    out->average=sum/(float)n; out->p95=sorted[rank?rank-1:0]; out->peak=sorted[n-1];
    out->average_calls=(float)calls/(float)n;
    return 0;
}
uint32_t athena_profiler_scope_count(void) { register_frame(); return scope_count; }
uint32_t athena_profiler_errors(void) { return errors; }
void athena_profiler_reset(int forget) {
    depth=0; head=0; filled=0; errors=0; started=0;
    if(forget) { scope_count=0; register_frame(); return; }
    for(uint32_t i=0;i<scope_count;i++) { scopes[i].ticks=0; scopes[i].value=0; scopes[i].calls=0; }
}
void athena_profiler_module_shutdown(void) { athena_profiler_reset(1); }

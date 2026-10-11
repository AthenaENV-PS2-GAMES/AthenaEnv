#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <athena/model3d.h>
/* Main thread only, like every loader. */
static char detail[160];
const char *athena_model3d_detail(void) { return detail; }
void athena_model3d_clear_detail(void) { detail[0]=0; }
void athena_model3d_set_detail(const char *format,...) {
    /* The first, most specific reason wins: callers up the stack only add
     * generic context. */
    if(detail[0]) return;
    va_list args; va_start(args,format);
    vsnprintf(detail,sizeof(detail),format,args);
    va_end(args);
}
void athena_model3d_detail_context(const char *format,...) {
    char context[64],merged[sizeof(detail)];
    va_list args; va_start(args,format);
    vsnprintf(context,sizeof(context),format,args);
    va_end(args);
    snprintf(merged,sizeof(merged),detail[0]?"%s: %s":"%s",context,detail);
    memcpy(detail,merged,sizeof(detail));
}

#if defined(__mips__)
#define LOADER_TICKS_PER_MS 294912.0f
uint32_t athena_model3d_clock(void) { uint32_t count; __asm__ __volatile__("mfc0 %0, $9" : "=r"(count)); return count; }
#else
#include <time.h>
#define LOADER_TICKS_PER_MS 1000.0f
uint32_t athena_model3d_clock(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint32_t)((uint64_t)ts.tv_sec*1000000u+(uint64_t)ts.tv_nsec/1000u);
}
#endif
float athena_model3d_clock_ms(uint32_t ticks) { return (float)ticks/LOADER_TICKS_PER_MS; }

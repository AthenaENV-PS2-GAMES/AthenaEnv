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

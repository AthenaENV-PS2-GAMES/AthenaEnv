#ifndef ATH_THREAD_H
#define ATH_THREAD_H

#include <ath_env.h>

JSModuleDef *athena_thread_init(JSContext *ctx);
/* Thread.readFileAsync() (ath_file_job.c). */
JSValue athena_thread_read_file_async(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv);

#endif /* ATH_THREAD_H */

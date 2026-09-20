#ifndef ATHENA_NATIVE_FACADE_H
#define ATHENA_NATIVE_FACADE_H

#ifdef ATHENA_NATIVE_COMPILER

#include <stdbool.h>
#include <stdint.h>

#include <quickjs/quickjs.h>
#include <native_compiler.h>

typedef struct AthenaNativeFuncInfo {
    int32_t code_size;
    int32_t arg_count;
    const char *return_type;
    char **arg_types;
} AthenaNativeFuncInfo;

bool athena_native_is_supported(void);
NativeCompiler *athena_native_compiler_get(void);
int athena_native_compiler_init(JSContext *ctx);
void athena_native_compiler_cleanup(void);

CompileResult athena_native_compile_function(JSContext *ctx, JSValueConst js_func,
                                             const NativeFuncSignature *sig);
void athena_native_func_release(NativeFunc *func);
JSValue athena_native_func_invoke(JSContext *ctx, NativeFunc *func,
                                  int argc, JSValueConst *argv);
int athena_native_func_query_info(NativeFunc *func, AthenaNativeFuncInfo *info);
void athena_native_func_info_free(AthenaNativeFuncInfo *info);
double athena_native_benchmark(JSContext *ctx, JSValueConst func, int iterations);
char *athena_native_disassemble(NativeCompiler *compiler);

#endif /* ATHENA_NATIVE_COMPILER */

#endif /* ATHENA_NATIVE_FACADE_H */

#ifdef ATHENA_NATIVE_COMPILER

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <athena/native_facade.h>

#ifdef PS2
#include <timer.h>
#endif

static NativeCompiler *g_compiler = NULL;

bool athena_native_is_supported(void)
{
#ifdef PS2
    return true;
#else
    return false;
#endif
}

NativeCompiler *athena_native_compiler_get(void)
{
    return g_compiler;
}

int athena_native_compiler_init(JSContext *ctx)
{
    if (g_compiler)
        return 0;

    g_compiler = malloc(sizeof(NativeCompiler));
    if (!g_compiler)
        return -1;

    if (native_compiler_init(g_compiler, ctx) < 0) {
        free(g_compiler);
        g_compiler = NULL;
        return -1;
    }

    return 0;
}

void athena_native_compiler_cleanup(void)
{
    if (!g_compiler)
        return;
    native_compiler_free(g_compiler);
    free(g_compiler);
    g_compiler = NULL;
}

CompileResult athena_native_compile_function(JSContext *ctx, JSValueConst js_func,
                                             const NativeFuncSignature *sig)
{
    CompileResult result = {0};

    if (athena_native_compiler_init(ctx) < 0) {
        result.success = false;
        result.error_msg = "Failed to initialize compiler";
        return result;
    }

    return native_compile_function(g_compiler, js_func, sig);
}

void athena_native_func_release(NativeFunc *func)
{
    if (!func)
        return;
    native_func_free(func);
    free(func);
}

JSValue athena_native_func_invoke(JSContext *ctx, NativeFunc *func,
                                  int argc, JSValueConst *argv)
{
    if (!func || !func->is_valid)
        return JS_ThrowTypeError(ctx, "Invalid native function");
    return native_func_call(ctx, func, argc, argv);
}

int athena_native_func_query_info(NativeFunc *func, AthenaNativeFuncInfo *info)
{
    if (!func || !func->is_valid || !info)
        return -1;

    info->code_size = (int32_t)func->code_size;
    info->arg_count = func->sig.arg_count;
    info->return_type = native_type_name(func->sig.return_type);
    info->arg_types = calloc(func->sig.arg_count, sizeof(char *));
    if (!info->arg_types)
        return -1;

    for (int i = 0; i < func->sig.arg_count; i++) {
        info->arg_types[i] = strdup(native_type_name(func->sig.arg_types[i]));
        if (!info->arg_types[i]) {
            athena_native_func_info_free(info);
            return -1;
        }
    }

    return 0;
}

void athena_native_func_info_free(AthenaNativeFuncInfo *info)
{
    if (!info)
        return;
    if (info->arg_types) {
        for (int i = 0; i < info->arg_count; i++)
            free(info->arg_types[i]);
        free(info->arg_types);
        info->arg_types = NULL;
    }
}

double athena_native_benchmark(JSContext *ctx, JSValueConst func, int iterations)
{
    JSValue empty_args[1] = { JS_UNDEFINED };
    clock_t start = clock();

    for (int i = 0; i < iterations; i++) {
        JSValue result = JS_Call(ctx, func, JS_UNDEFINED, 0, empty_args);
        JS_FreeValue(ctx, result);
    }

    clock_t end = clock();
    return ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
}

char *athena_native_disassemble(NativeCompiler *compiler)
{
    MipsEmitter *em;
    char *result;
    int pos = 0;
    size_t start;
    size_t count;
    uint32_t base_addr;

    if (!compiler)
        return NULL;

    em = &compiler->emitter;
    if (!em->buffer.code || em->buffer.count == 0)
        return NULL;

    result = malloc(16384);
    if (!result)
        return NULL;

    start = em->buffer.function_start;
    count = em->buffer.count;
    base_addr = (uint32_t)(uintptr_t)em->buffer.code;

    pos += snprintf(result + pos, 16384 - pos, "=== MIPS Disassembly ===\n");
    pos += snprintf(result + pos, 16384 - pos, "Code size: %zu instructions (%zu bytes)\n",
                    count - start, (count - start) * 4);
    pos += snprintf(result + pos, 16384 - pos, "Base address: 0x%08X\n\n", base_addr);

    static const char *reg_names[] = {
        "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
        "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
        "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
        "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
    };

    for (size_t i = start; i < count && pos < 16000; i++) {
        uint32_t instr = em->buffer.code[i];
        uint32_t addr = base_addr + (i * 4);
        uint32_t opcode = (instr >> 26) & 0x3F;
        uint32_t rs = (instr >> 21) & 0x1F;
        uint32_t rt = (instr >> 16) & 0x1F;
        uint32_t rd = (instr >> 11) & 0x1F;
        uint32_t shamt = (instr >> 6) & 0x1F;
        uint32_t funct = instr & 0x3F;
        int16_t imm = (int16_t)(instr & 0xFFFF);
        char mnemonic[32] = "???";
        char operands[64] = "";

        if (opcode == 0x00) {
            switch (funct) {
                case 0x00: strcpy(mnemonic, "SLL"); snprintf(operands, 64, "%s, %s, %d", reg_names[rd], reg_names[rt], shamt); break;
                case 0x02: strcpy(mnemonic, "SRL"); snprintf(operands, 64, "%s, %s, %d", reg_names[rd], reg_names[rt], shamt); break;
                case 0x03: strcpy(mnemonic, "SRA"); snprintf(operands, 64, "%s, %s, %d", reg_names[rd], reg_names[rt], shamt); break;
                case 0x08: strcpy(mnemonic, "JR"); snprintf(operands, 64, "%s", reg_names[rs]); break;
                case 0x09: strcpy(mnemonic, "JALR"); snprintf(operands, 64, "%s, %s", reg_names[rd], reg_names[rs]); break;
                case 0x18: strcpy(mnemonic, "MULT"); snprintf(operands, 64, "%s, %s", reg_names[rs], reg_names[rt]); break;
                case 0x12: strcpy(mnemonic, "MFLO"); snprintf(operands, 64, "%s", reg_names[rd]); break;
                case 0x21: strcpy(mnemonic, "ADDU"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                case 0x23: strcpy(mnemonic, "SUBU"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                case 0x24: strcpy(mnemonic, "AND"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                case 0x25: strcpy(mnemonic, "OR"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                case 0x26: strcpy(mnemonic, "XOR"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                case 0x2A: strcpy(mnemonic, "SLT"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                case 0x2B: strcpy(mnemonic, "SLTU"); snprintf(operands, 64, "%s, %s, %s", reg_names[rd], reg_names[rs], reg_names[rt]); break;
                default: snprintf(mnemonic, 32, "SPECIAL_%02X", funct); break;
            }
        } else if (opcode == 0x11) {
            uint32_t fmt = rs;
            uint32_t ft = rt;
            uint32_t fs = (instr >> 11) & 0x1F;
            uint32_t fd = (instr >> 6) & 0x1F;
            uint32_t fn = instr & 0x3F;

            if (fmt == 0x10) {
                switch (fn) {
                    case 0x00: strcpy(mnemonic, "ADD.S"); snprintf(operands, 64, "$f%d, $f%d, $f%d", fd, fs, ft); break;
                    case 0x01: strcpy(mnemonic, "SUB.S"); snprintf(operands, 64, "$f%d, $f%d, $f%d", fd, fs, ft); break;
                    case 0x02: strcpy(mnemonic, "MUL.S"); snprintf(operands, 64, "$f%d, $f%d, $f%d", fd, fs, ft); break;
                    case 0x03: strcpy(mnemonic, "DIV.S"); snprintf(operands, 64, "$f%d, $f%d, $f%d", fd, fs, ft); break;
                    case 0x04: strcpy(mnemonic, "SQRT.S"); snprintf(operands, 64, "$f%d, $f%d", fd, fs); break;
                    case 0x05: strcpy(mnemonic, "ABS.S"); snprintf(operands, 64, "$f%d, $f%d", fd, fs); break;
                    case 0x06: strcpy(mnemonic, "MOV.S"); snprintf(operands, 64, "$f%d, $f%d", fd, fs); break;
                    default: snprintf(mnemonic, 32, "FPU_%02X", fn); break;
                }
            } else if (fmt == 0x04) {
                strcpy(mnemonic, "MTC1");
                snprintf(operands, 64, "%s, $f%d", reg_names[rt], fs);
            } else if (fmt == 0x00) {
                strcpy(mnemonic, "MFC1");
                snprintf(operands, 64, "%s, $f%d", reg_names[rt], fs);
            } else {
                snprintf(mnemonic, 32, "COP1_%02X", fmt);
            }
        } else {
            switch (opcode) {
                case 0x04: strcpy(mnemonic, "BEQ"); snprintf(operands, 64, "%s, %s, %d", reg_names[rs], reg_names[rt], imm); break;
                case 0x05: strcpy(mnemonic, "BNE"); snprintf(operands, 64, "%s, %s, %d", reg_names[rs], reg_names[rt], imm); break;
                case 0x09: strcpy(mnemonic, "ADDIU"); snprintf(operands, 64, "%s, %s, %d", reg_names[rt], reg_names[rs], imm); break;
                case 0x0C: strcpy(mnemonic, "ANDI"); snprintf(operands, 64, "%s, %s, 0x%04X", reg_names[rt], reg_names[rs], (uint16_t)imm); break;
                case 0x0D: strcpy(mnemonic, "ORI"); snprintf(operands, 64, "%s, %s, 0x%04X", reg_names[rt], reg_names[rs], (uint16_t)imm); break;
                case 0x0E: strcpy(mnemonic, "XORI"); snprintf(operands, 64, "%s, %s, 0x%04X", reg_names[rt], reg_names[rs], (uint16_t)imm); break;
                case 0x0F: strcpy(mnemonic, "LUI"); snprintf(operands, 64, "%s, 0x%04X", reg_names[rt], (uint16_t)imm); break;
                case 0x23: strcpy(mnemonic, "LW"); snprintf(operands, 64, "%s, %d(%s)", reg_names[rt], imm, reg_names[rs]); break;
                case 0x2B: strcpy(mnemonic, "SW"); snprintf(operands, 64, "%s, %d(%s)", reg_names[rt], imm, reg_names[rs]); break;
                default: snprintf(mnemonic, 32, "OP_%02X", opcode); break;
            }
        }

        pos += snprintf(result + pos, 16384 - pos, "%04zX: 0x%08X  %-8s %s\n",
                        (i - start) * 4, instr, mnemonic, operands);
    }

    return result;
}

#endif /* ATHENA_NATIVE_COMPILER */

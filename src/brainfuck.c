/*
 *  dP                         oo          .8888b                   dP
 *  88                                     88   "                   88
 *  88d888b. 88d888b. .d8888b. dP 88d888b. 88aaa  dP    dP .d8888b. 88  .dP
 *  88'  `88 88'  `88 88'  `88 88 88'  `88 88     88    88 88'  `"" 88888"
 *  88.  .88 88       88.  .88 88 88    88 88     88.  .88 88.  ... 88  `8b.
 *  88Y8888' dP       `88888P8 dP dP    dP dP     `88888P' `88888P' dP   `YP
 *
 * Authored in 2013.  See README for a list of contributors.
 * Released into the public domain.
 *
 * Any being (not just humans) is free to copy, modify, publish, use, compile,
 * sell or distribute this software, either in source code form or as a
 * compiled binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to the
 * public domain.  We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors.  We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all present
 * and future rights to this software under copyright law.
 *
 * The software is provided AS IS, without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose and non-infringement.  In no event shall
 * the authors be liable for any claim, damages or other liability, whether in
 * an action of contract, tort or otherwise, arising from, out of or in
 * connection with the software or the use or other dealings in the software.
 *
 * This software is completely unlicensed. */

#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>

#define B_VERSION_STRING "0.4"
#define B_BUILD_FEATURES "core:llvm-ir:bin"

#define B_TRUE 1
#define B_FALSE 0

#define B_GENERIC_ADDRESS_SPACE 0

static char *B_INVOCATION = NULL;

static size_t B_CONTAINER_LENGTH = 30000;

static int B_SHOULD_READ_FROM_STDIN = B_FALSE;
static char const *B_INPUT_FILENAME = NULL;

static int B_SHOULD_EMIT_C_CODE = B_FALSE;
static char const *B_C_CODE_FILENAME = "brainfuck.c";

static int B_SHOULD_EMIT_LLVM_IR = B_FALSE;
static char const *B_LLVM_IR_FILENAME = "brainfuck.l";

static int B_SHOULD_OPTIMIZE_CODE = B_TRUE;
static int B_SHOULD_PRINT_BYTECODE_DISASSEMBLY = B_FALSE;
static int B_SHOULD_EXPLAIN_CODE = B_FALSE;
static int B_SHOULD_INTERPRET_CODE = B_TRUE;
static int B_SHOULD_COMPILE_AND_EXECUTE = B_FALSE;

enum instruction {
        B_INVALID = 0x00,
        B_MOVE_POINTER_LEFT = 0x3C, /* < */
        B_MOVE_POINTER_RIGHT = 0x3E, /* > */
        B_INCREMENT_CELL_VALUE = 0x2B, /* + */
        B_DECREMENT_CELL_VALUE = 0x2D, /* - */
        B_OUTPUT_CELL_VALUE = 0x2E, /* . */
        B_INPUT_CELL_VALUE = 0x2C, /* , */
        B_BRANCH_FORWARD = 0x5B, /* [ */
        B_BRANCH_BACKWARD = 0x5D, /* ] */
        B_TERMINATE = 0xFF
};

struct opcode {
        enum instruction instruction;
        size_t auxiliary;
};

struct program {
        struct opcode *opcodes;
        size_t number_of_opcodes;
};

static inline long get_file_length(FILE *file)
{
        long position = 0L;
        long length = 0L;

        if (file == NULL) {
                abort();
        }

        position = ftell(file);

        if (position == -1L || fseek(file, 0, SEEK_END) != 0) {
                abort();
        }

        length = ftell(file);

        if (length == -1L || fseek(file, position, SEEK_SET) != 0) {
                abort();
        }

        return length;
}

static inline char *read_file(char const *filename)
{
        char *contents = NULL;

        FILE *file = fopen(filename, "rt");
        long file_length = 0;

        if (file == NULL) {
                abort();
        }

        file_length = get_file_length(file);

        if (file_length == 0) {
                printf("%s: nothing to do\n", B_INVOCATION);
                fclose(file);
                abort();
        }

        contents = malloc(sizeof (char) * file_length);

        if (contents == NULL) {
                fclose(file);
                abort();
        }

        if (fread(contents, file_length, 1, file) != 1UL) {
                fclose(file);
                abort();
        }

        fclose(file);
        return contents;
}

static inline char *read_stdin(void)
{
        char buffer[1024];

        size_t content_size = 1;
        char *contents = malloc(sizeof (char) * 1024);

        if (contents == NULL) {
                abort();
        }

        *contents = '\0';

        while (fgets(buffer, 1024, stdin)) {
                char *old_contents = contents;

                content_size += strlen(buffer);
                contents = realloc(contents, sizeof (char) * content_size);

                if (contents == NULL) {
                        free(old_contents);
                        abort();
                }

                strcat(contents, buffer);
        }

        return contents;
}

static inline int is_brainfuck_command(int command)
{
        char const commands[] = { '<', '>', '+', '-', '.', ',', '[', ']' };
        return (memchr(commands, command, sizeof (commands)) != NULL);
}

static inline char *sanitize(char **source_code)
{
        int i = 0;
        int j = 0;

        char *output = NULL;

        if (source_code == NULL || *source_code == NULL) {
                abort();
        }

        output = malloc(sizeof (char) * strlen(*source_code));

        if (output == NULL) {
                abort();
        }

        for (i = 0; (*source_code)[i]; ++i) {
                if (is_brainfuck_command((*source_code)[i])) {
                        output[j++] = (*source_code)[i];
                }
        }

        free(*source_code);
        *source_code = output;

        return output;
}

static struct program *run_length_encode(char const *source_code)
{
        size_t i = 0;
        char const *command = NULL;

        struct program *program = NULL;
        struct opcode opcode;

        if (source_code == NULL) {
                abort();
        }

        program = malloc(sizeof (struct program));

        if (program == NULL) {
                abort();
        }

        program->opcodes =
                malloc(sizeof (struct opcode) * (strlen(source_code) + 1));

        if (program->opcodes == NULL) {
                free(program);
                abort();
        }

        opcode.instruction = B_INVALID;
        opcode.auxiliary = 0;

        for (command = source_code; *command; ++command) {
                switch (*command) {
                case B_OUTPUT_CELL_VALUE:
                case B_INPUT_CELL_VALUE:
                case B_BRANCH_FORWARD:
                case B_BRANCH_BACKWARD:
                        if (opcode.instruction != B_INVALID) {
                                memcpy(program->opcodes + i, &opcode,
                                        sizeof (opcode));
                                ++i;
                        }

                        opcode.instruction = *command;
                        opcode.auxiliary = 1;

                        break;

                default:
                        if (opcode.instruction != *command) {
                                if (opcode.instruction != B_INVALID) {
                                        memcpy(program->opcodes + i, &opcode,
                                                sizeof (opcode));
                                        ++i;
                                }

                                opcode.instruction = *command;
                                opcode.auxiliary = 1;

                                break;
                        }

                        ++(opcode.auxiliary);
                }
        }

        memcpy(program->opcodes + i, &opcode, sizeof (opcode));
        ++i;

        opcode.instruction = B_TERMINATE;
        opcode.auxiliary = 0;

        memcpy(program->opcodes + i, &opcode, sizeof (opcode));
        ++i;

        program->number_of_opcodes = i;
        return program;
}

static struct program *link_branches(struct program *program)
{
        int i = 0;
        int j = 0;

        long *stack = NULL;

        if (program == NULL || program->opcodes == NULL) {
                abort();
        }

        stack = malloc(sizeof (long) * program->number_of_opcodes);

        if (stack == NULL) {
                abort();
        }

        for (i = 0; i != (int) program->number_of_opcodes; ++i) {
                switch (program->opcodes[i].instruction) {
                case B_BRANCH_FORWARD:
                        stack[j++] = i;
                        break;

                case B_BRANCH_BACKWARD:
                        --j;

                        program->opcodes[i].auxiliary = stack[j];
                        program->opcodes[stack[j]].auxiliary = i;

                default:
                        break;
                }
        }

        return program;
}

static void interpret(struct program const *program)
{
        size_t i = 0;

        char *container = NULL;
        char *pointer = NULL;

        if (program == NULL || program->opcodes == NULL) {
                abort();
        }

        container = calloc(B_CONTAINER_LENGTH, sizeof (char));

        if (container == NULL) {
                abort();
        }

        pointer = container;

        for (; i != program->number_of_opcodes; ++i) {
                switch (program->opcodes[i].instruction) {
                case B_MOVE_POINTER_LEFT:
                        pointer -= program->opcodes[i].auxiliary;
                        break;

                case B_MOVE_POINTER_RIGHT:
                        pointer += program->opcodes[i].auxiliary;
                        break;

                case B_INCREMENT_CELL_VALUE:
                        *pointer += program->opcodes[i].auxiliary;
                        break;

                case B_DECREMENT_CELL_VALUE:
                        *pointer -= program->opcodes[i].auxiliary;
                        break;

                case B_OUTPUT_CELL_VALUE:
                        putchar(*pointer);
                        break;

                case B_INPUT_CELL_VALUE:
                        *pointer = getchar();
                        break;

                case B_BRANCH_FORWARD:
                        if (*pointer == 0) {
                                i = program->opcodes[i].auxiliary;
                        }

                        break;

                case B_BRANCH_BACKWARD:
                        if (*pointer != 0) {
                                i = program->opcodes[i].auxiliary;
                        }

                        break;

                case B_TERMINATE:
                        if (i != program->number_of_opcodes - 1) {
                                printf("%s: premature termination @ %zd\n",
                                        B_INVOCATION, i);
                        }

                default:
                        break;
                }
        }

        free(container);
}

static LLVMModuleRef optimize_llvm_module(LLVMModuleRef module)
{
        LLVMPassManagerRef manager = LLVMCreatePassManager();
        LLVMPassManagerBuilderRef builder = LLVMPassManagerBuilderCreate();
        LLVMPassManagerBuilderSetOptLevel(builder, 3);

        LLVMPassManagerBuilderPopulateModulePassManager(builder, manager);
        LLVMRunPassManager(manager, module);

        LLVMPassManagerBuilderDispose(builder);
        LLVMDisposePassManager(manager);

        return module;
}

static LLVMModuleRef build_llvm_module(struct program const *program)
{
        size_t i = 0;
        size_t k = 0;

        if (program == NULL || program->opcodes == NULL) {
                abort();
        }

        LLVMModuleRef module = LLVMModuleCreateWithName("brainfuck");
        LLVMBuilderRef builder = LLVMCreateBuilder();

        LLVMValueRef container = NULL;
        LLVMValueRef index = NULL;

        LLVMBasicBlockRef start = NULL;
        LLVMBasicBlockRef end = NULL;

        LLVMBasicBlockRef *stack = malloc(sizeof (LLVMBasicBlockRef) *
                program->number_of_opcodes);

        if (stack == NULL) {
                abort();
        }

        {
                LLVMTypeRef parameters[] = {
                        LLVMInt32Type(), LLVMInt32Type()
                };

                LLVMTypeRef result = LLVMPointerType(LLVMInt8Type(),
                        B_GENERIC_ADDRESS_SPACE);

                LLVMTypeRef function = LLVMFunctionType(result, parameters, 2,
                        B_FALSE);

                LLVMAddFunction(module, "calloc", function);
        }

        {
                LLVMTypeRef function = LLVMFunctionType(LLVMInt32Type(),
                        NULL, 0, B_FALSE);

                LLVMAddFunction(module, "getchar", function);
        }

        {
                LLVMTypeRef parameters[] = { LLVMInt32Type() };
                LLVMTypeRef function = LLVMFunctionType(LLVMInt32Type(),
                        parameters, 1, B_FALSE);

                LLVMAddFunction(module, "putchar", function);
        }

        {
                LLVMTypeRef function = LLVMFunctionType(LLVMVoidType(), NULL,
                        0, B_FALSE);

                LLVMValueRef main = LLVMAddFunction(module, "main", function);
                LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main, "entry");

                LLVMPositionBuilderAtEnd(builder, entry);
        }

        {
                LLVMValueRef function = LLVMGetNamedFunction(module, "calloc");
                LLVMValueRef arguments[] = {
                        LLVMConstInt(LLVMInt32Type(), B_CONTAINER_LENGTH,
                                B_FALSE),
                        LLVMConstInt(LLVMInt32Type(), sizeof (char), B_FALSE)
                };

                LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, B_FALSE);

                container = LLVMBuildCall(builder, function, arguments, 2,
                        "container");

                index = LLVMBuildAlloca(builder, LLVMInt32Type(), "index");
                LLVMBuildStore(builder, zero, index);
        }

        for (; i < program->number_of_opcodes; ++i) {
                switch (program->opcodes[i].instruction) {
                case B_MOVE_POINTER_LEFT: {
                        LLVMValueRef value = LLVMBuildLoad(builder, index, "");
                        LLVMValueRef amount = LLVMConstInt(LLVMInt32Type(),
                                program->opcodes[i].auxiliary,
                                B_GENERIC_ADDRESS_SPACE);

                        LLVMValueRef result = LLVMBuildSub(builder, value,
                                amount, "");

                        LLVMBuildStore(builder, result, index);
                        break;
                }

                case B_MOVE_POINTER_RIGHT: {
                        LLVMValueRef value = LLVMBuildLoad(builder, index, "");
                        LLVMValueRef amount = LLVMConstInt(LLVMInt32Type(),
                                program->opcodes[i].auxiliary,
                                B_GENERIC_ADDRESS_SPACE);

                        LLVMValueRef result = LLVMBuildAdd(builder, value,
                                amount, "");

                        LLVMBuildStore(builder, result, index);
                        break;
                }


                case B_INCREMENT_CELL_VALUE: {
                        LLVMValueRef offset = LLVMBuildLoad(builder, index,
                                "");

                        LLVMValueRef cell = LLVMBuildGEP(builder, container,
                                &offset, 1, "");

                        LLVMValueRef value = LLVMBuildLoad(builder, cell, "");
                        LLVMValueRef increment = LLVMBuildAdd(builder, value,
                                LLVMConstInt(LLVMInt8Type(),
                                        program->opcodes[i].auxiliary,
                                        B_FALSE),
                                "");

                        LLVMBuildStore(builder, increment, cell);
                        break;
                }

                case B_DECREMENT_CELL_VALUE: {
                        LLVMValueRef offset = LLVMBuildLoad(builder, index,
                                "");

                        LLVMValueRef cell = LLVMBuildGEP(builder, container,
                                &offset, 1, "");

                        LLVMValueRef value = LLVMBuildLoad(builder, cell, "");
                        LLVMValueRef decrement = LLVMBuildSub(builder, value,
                                LLVMConstInt(LLVMInt8Type(),
                                        program->opcodes[i].auxiliary,
                                        B_FALSE),
                                "");

                        LLVMBuildStore(builder, decrement, cell);
                        break;
                }

                case B_OUTPUT_CELL_VALUE: {
                        LLVMValueRef offset = LLVMBuildLoad(builder, index,
                                "");

                        LLVMValueRef cell = LLVMBuildGEP(builder, container,
                                &offset, 1, "");

                        LLVMValueRef value = LLVMBuildLoad(builder, cell, "");
                        LLVMValueRef character = LLVMBuildSExt(builder, value,
                                LLVMInt32Type(), "");

                        LLVMValueRef function = LLVMGetNamedFunction(module,
                                "putchar");

                        LLVMBuildCall(builder, function, &character, 1, "");
                        break;
                }

                case B_INPUT_CELL_VALUE: {
                        LLVMValueRef function = LLVMGetNamedFunction(module,
                                "getchar");

                        LLVMValueRef input = LLVMBuildCall(builder, function,
                                NULL, 0, "");

                        LLVMValueRef character = LLVMBuildTrunc(builder, input,
                                LLVMInt8Type(), "");

                        LLVMValueRef offset = LLVMBuildLoad(builder, index,
                                "");

                        LLVMValueRef cell = LLVMBuildGEP(builder, container,
                                &offset, 1, "");

                        LLVMBuildStore(builder, character, cell);
                        break;
                }

                case B_BRANCH_FORWARD: {
                        LLVMBasicBlockRef body = NULL;

                        LLVMValueRef offset = NULL;
                        LLVMValueRef cell = NULL;
                        LLVMValueRef value = NULL;
                        LLVMValueRef predicate = NULL;

                        LLVMValueRef zero = LLVMConstInt(LLVMInt8Type(), 0,
                                B_FALSE);

                        LLVMValueRef main = LLVMGetNamedFunction(module,
                                "main");

                        start = LLVMAppendBasicBlock(main, "start");
                        stack[k++] = start;

                        body = LLVMAppendBasicBlock(main, "body");

                        end = LLVMAppendBasicBlock(main, "end");
                        stack[k++] = end;

                        LLVMBuildBr(builder, start);
                        LLVMPositionBuilderAtEnd(builder, start);

                        offset = LLVMBuildLoad(builder, index, "");
                        cell = LLVMBuildGEP(builder, container, &offset, 1,
                                "");

                        value = LLVMBuildLoad(builder, cell, "");
                        predicate = LLVMBuildICmp(builder, LLVMIntEQ, value,
                                zero, "");

                        LLVMBuildCondBr(builder, predicate, end, body);

                        LLVMPositionBuilderAtEnd(builder, body);
                        break;
                }

                case B_BRANCH_BACKWARD: {
                        end = stack[--k];
                        start = stack[--k];

                        LLVMBuildBr(builder, start);
                        LLVMPositionBuilderAtEnd(builder, end);

                        break;
                }

                case B_TERMINATE:
                default:
                        break;
                }
        }

        LLVMBuildFree(builder, container);
        LLVMBuildRetVoid(builder);

        LLVMDisposeBuilder(builder);

        free(stack);

        return optimize_llvm_module(module);
}

static void execute(struct program const *program)
{
        LLVMExecutionEngineRef engine = NULL;
        char *error = NULL;

        LLVMModuleRef module = build_llvm_module(program);

        fputs("executing:\n", stderr);
        LLVMDumpModule(module);

        fputc('\n', stderr);
        fflush(stderr);

        LLVMVerifyModule(module, LLVMAbortProcessAction, &error);

        LLVMDisposeMessage(error);
        error = NULL;

        LLVMInitializeNativeTarget();

        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeAsmParser();

        if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
                abort();
        }

        if (error != NULL) {
                fprintf(stderr, "error: %s\n", error);
                LLVMDisposeMessage(error);

                abort();
        }

        puts("output:");

        LLVMValueRef main = LLVMGetNamedFunction(module, "main");
        LLVMRunFunction(engine, main, 0, NULL);

        LLVMDisposeModule(module);
}

static void disassamble(struct program const *program)
{
        size_t i = 0;

        if (program == NULL || program->opcodes == NULL) {
                abort();
        }

        printf(",- b -----------------------.\n");

        for (; i != program->number_of_opcodes; ++i) {
                if (program->opcodes[i].instruction == B_TERMINATE) {
                        break;
                }

                printf("| 0x%08zX | %05zd:%02d | %c |\n", i,
                        program->opcodes[i].auxiliary,
                        program->opcodes[i].instruction,
                        program->opcodes[i].instruction);
        }

        printf("\\-------~ ......... ~-------/\n");
}

static void explain_opcode(struct opcode const *opcode)
{
        if (opcode == NULL) {
                abort();
        }

        switch (opcode->instruction) {
        case B_MOVE_POINTER_LEFT:
                printf("| move-pointer-left       |   (%05zd)   |",
                        opcode->auxiliary);
                break;

        case B_MOVE_POINTER_RIGHT:
                printf("| move-pointer-right      |   (%05zd)   |",
                        opcode->auxiliary);
                break;

        case B_INCREMENT_CELL_VALUE:
                printf("| increment-cell-value    |   (%05zd)   |",
                        opcode->auxiliary);
                break;

        case B_DECREMENT_CELL_VALUE:
                printf("| decrement-cell-value    |   (%05zd)   |",
                        opcode->auxiliary);
                break;

        case B_OUTPUT_CELL_VALUE:
                printf("| output-cell-value       |      ~      |");
                break;

        case B_INPUT_CELL_VALUE:
                printf("| input-cell-value        |      ~      |");
                break;

        case B_BRANCH_FORWARD:
                printf("| branch-if-zero          | [x%08zX] |",
                        opcode->auxiliary);
                break;

        case B_BRANCH_BACKWARD:
                printf("| branch-back-if-not-zero | [x%08zX] |",
                        opcode->auxiliary);
                break;

        case B_TERMINATE:
                printf("| terminate-execution ------------------/");

        default:
                break;
        }
}

static void explain(struct program const *program)
{
        size_t i = 0;

        if (program == NULL || program->opcodes == NULL) {
                abort();
        }

        printf(",- b -----------------------------------.\n");
        printf("| (): relative | []: absolute | ~: n/a  |\n");
        printf("|---------------------------------------|\n");

        for (; i != program->number_of_opcodes; ++i) {
                explain_opcode(program->opcodes + i);
                putchar('\n');
        }
}

static void emit_c_code(struct program const *program, char const *filename)
{
        size_t i = 0;

        FILE *file = fopen(filename, "wt");

        if (file == NULL) {
                abort();
        }

        fprintf(file, "/* %s */\n#include <stdio.h>\n\n"
                "static char container[%zd];\n"
                "static char *pointer = container;\n"
                "\n"
                "int main(int count, char **arguments)\n"
                "{\n", B_INPUT_FILENAME, B_CONTAINER_LENGTH);


        for (; i != program->number_of_opcodes; ++i) {
                switch (program->opcodes[i].instruction) {
                case B_MOVE_POINTER_LEFT:
                        fputs("        ", file);
                        fprintf(file, "pointer -= %zd;\n",
                                program->opcodes[i].auxiliary);
                        break;

                case B_MOVE_POINTER_RIGHT:
                        fputs("        ", file);
                        fprintf(file, "pointer += %zd;\n",
                                program->opcodes[i].auxiliary);
                        break;

                case B_INCREMENT_CELL_VALUE:
                        fputs("        ", file);
                        fprintf(file, "*pointer += %zd;\n",
                                program->opcodes[i].auxiliary);
                        break;

                case B_DECREMENT_CELL_VALUE:
                        fputs("        ", file);
                        fprintf(file, "*pointer -= %zd;\n",
                                program->opcodes[i].auxiliary);
                        break;

                case B_OUTPUT_CELL_VALUE:
                        fputs("        ", file);
                        fputs("putchar(*pointer);\n", file);
                        break;

                case B_INPUT_CELL_VALUE:
                        fputs("        ", file);
                        fputs("*pointer = getchar();\n", file);
                        break;

                case B_BRANCH_FORWARD:
                        fprintf(file, "\nl%zd:\n", i);

                        fputs("        ", file);
                        fprintf(file, "if (*pointer == 0) {\n"
                                "                goto l%zd;\n"
                                "        }\n\n",
                                program->opcodes[i].auxiliary);

                        break;

                case B_BRANCH_BACKWARD:
                        fputs("\n        ", file);
                        fprintf(file, "if (*pointer != 0) {\n"
                                "                goto l%zd;\n"
                                "        }\n\n",
                                program->opcodes[i].auxiliary);

                        fprintf(file, "l%zd:\n", i);

                default:
                        break;
                }
        }

        fputs("\n        return 0;\n}\n", file);
        fclose(file);
}

static void emit_llvm_ir(struct program const *program, char const *filename)
{
        char *error = NULL;
        LLVMModuleRef module = build_llvm_module(program);

        if (filename == NULL) {
                abort();
        }

        LLVMPrintModuleToFile(module, filename, &error);

        if (error != NULL) {
                fprintf(stderr, "error: %s\n", error);
                LLVMDisposeMessage(error);

                abort();
        }

        LLVMDisposeModule(module);
}

static inline void free_program(struct program *program)
{
        if (program != NULL) {
                free(program->opcodes);
        }

        free(program);
}

static void display_help_screen(void)
{
        printf("    dP                         oo          .8888b             "
                "      dP\n    88                                     88   \" "
                "                  88\n    88d888b. 88d888b. .d8888b. dP 88d88"
                "8b. 88aaa  dP    dP .d8888b. 88  .dP\n    88'  `88 88'  `88 8"
                "8'  `88 88 88'  `88 88     88    88 88'  `\"\" 88888\"\n    8"
                "8.  .88 88       88.  .88 88 88    88 88     88.  .88 88.  .."
                ". 88  `8b.\n    88Y8888' dP       `88888P8 dP dP    dP dP    "
                " `88888P' `88888P' dP   `YP\n"
                "\n"
                "Authored in 2013.  See README for a list of contributors.\n"
                "Released into the public domain.\n"
                "\n"
                "Usage:\n"
                "        %s [--cdehlruvxz] <input>\n"
                "\n"
                "Options:\n"
                "        --                          read input from stdin\n"
                "        -c [filename=`brainfuck.c`] generate and emit C "
                                                    "code\n"
                "        -d                          print disassembly\n"
                "        -e                          explain source code\n"
                "        -h                          display this help "
                                                    "screen\n"
                "        -l [filename=`brainfuck.l`] generate and emit LLVM "
                                                    "IR\n"
                "        -r                          JIT compile and execute\n"
                "        -u                          disable optimizations\n"
                "        -v                          display version "
                                                    "information\n"
                "        -x                          disable interpretation\n"
                "        -z <length=`30000`>         set tape length\n",
                B_INVOCATION);
}

static void parse_command_line(int count, char **arguments)
{
        int i = 1;

        for (; i < count; ++i) {
                switch (arguments[i][0]) {
                case '-':
                        switch(arguments[i][1]) {
                        case '-':
                                B_SHOULD_READ_FROM_STDIN = B_TRUE;
                                break;

                        case 'c':
                                B_SHOULD_EMIT_C_CODE = B_TRUE;

                                if (i + 1 < count) {
                                        if (arguments[i + 1][0] != '\0') {
                                                B_C_CODE_FILENAME =
                                                        arguments[++i];
                                        }
                                }
                                break;

                        case 'd':
                                B_SHOULD_PRINT_BYTECODE_DISASSEMBLY = B_TRUE;
                                break;

                        case 'e':
                                B_SHOULD_EXPLAIN_CODE = B_TRUE;
                                break;

                        case 'h':
                                display_help_screen();
                                break;

                        case 'l':
                                B_SHOULD_EMIT_LLVM_IR = B_TRUE;

                                if (i + 1 < count) {
                                        if (arguments[i + 1][0] != '\0') {
                                                B_LLVM_IR_FILENAME =
                                                        arguments[++i];
                                        }
                                }
                                break;

                        case 'r':
                                B_SHOULD_COMPILE_AND_EXECUTE = B_TRUE;
                                break;

                        case 'u':
                                B_SHOULD_OPTIMIZE_CODE = B_FALSE;
                                break;

                        case 'v':
                                printf("%s (brainfuck) %s (%s)\n",
                                        B_INVOCATION, B_VERSION_STRING,
                                        B_BUILD_FEATURES);
                                break;

                        case 'x':
                                B_SHOULD_INTERPRET_CODE = B_FALSE;
                                break;

                        case 'z':
                                if (i + 1 >= count) {
                                        printf("%s: the argument `z` requires "
                                                "a numerical parameter\n",
                                                B_INVOCATION);
                                        abort();
                                }

                                B_CONTAINER_LENGTH = atoi(arguments[++i]);

                                if (B_CONTAINER_LENGTH == 0) {
                                        printf("%s: the tape length cannot be "
                                                "alphanumerical or zero\n",
                                                B_INVOCATION);
                                        abort();
                                }

                                break;

                        default:
                                printf("%s: unknown option `%c`\n",
                                        B_INVOCATION, arguments[i][1]);
                        }

                        break;

                default:
                        if (B_INPUT_FILENAME != NULL) {
                                printf("%s: warning, overriding previously "
                                        "specified filename\n",
                                        B_INVOCATION);
                        }

                        B_INPUT_FILENAME = arguments[i];
                }
        }
}

void respond_to_signal(int signal_identifier)
{
        (void) signal_identifier;

        printf("%s: aborting\n", B_INVOCATION);
        exit(EXIT_FAILURE);
}

int main(int count, char **arguments)
{
        char *source_code = NULL;
        struct program *program = NULL;

        signal(SIGABRT, respond_to_signal);
        signal(SIGINT, respond_to_signal);

        B_INVOCATION = arguments[0];

        if (count == 1) {
                printf("%s: no input files\n", B_INVOCATION);
                abort();
        }

        parse_command_line(count, arguments);

        if (B_SHOULD_READ_FROM_STDIN == B_TRUE || B_INPUT_FILENAME == NULL) {
                source_code = read_stdin();
        } else {
                source_code = read_file(B_INPUT_FILENAME);
        }

        source_code = sanitize(&source_code);

        if (B_SHOULD_OPTIMIZE_CODE == B_TRUE) {
                program = run_length_encode(source_code);
        }

        program = link_branches(program);

        if (B_SHOULD_PRINT_BYTECODE_DISASSEMBLY == B_TRUE) {
                disassamble(program);
        }

        if (B_SHOULD_EXPLAIN_CODE == B_TRUE) {
                explain(program);
        }

        if (B_SHOULD_EMIT_C_CODE == B_TRUE) {
                emit_c_code(program, B_C_CODE_FILENAME);
        }

        if (B_SHOULD_EMIT_LLVM_IR == B_TRUE) {
                emit_llvm_ir(program, B_LLVM_IR_FILENAME);
        }

        if (B_SHOULD_COMPILE_AND_EXECUTE == B_TRUE) {
                execute(program);
        }

        if (B_SHOULD_INTERPRET_CODE == B_TRUE) {
                interpret(program);
        }

        free_program(program);
        free(source_code);

        return 0;
}

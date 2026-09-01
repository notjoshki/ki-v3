#ifndef COMPILER_H
#define COMPILER_H

#include <stdio.h>
#include <stdbool.h>

#define COMP_SOURCE 0x01
#define COMP_OBJECT 0x02
#define COMP_UNOPTIMIZED 0x04
#define COMP_IR 0x08
#define COMP_EXPLICIT_IR 0x10
#define COMP_DEBUGINFO 0x20
#define COMP_OUTFILE_SPECIFIED 0x40
#define COMP_STATS 0x80
#define COMP_DOCUMENT 0x100
#define COMP_FREESTANDING 0x200
#define COMP_COMPILE_ASM 0x400
#define COMP_DOC_EXPORTED 0x800
#define COMP_SOURCE_LIBS 0x1000
#define COMP_RO_STRINGS 0x2000

typedef struct {
    char *assembler_path;
    char *linker_path;
    char *assemble_flags;
    char *linkage_flags;
    size_t flags;
} Compiler_Options;

typedef struct {
    char *input_path;
    char *output_path;
    Compiler_Options options;
} Compiler;

static inline Compiler_Options create_compiler_options(char *assembler_path, char *linker_path, size_t flags) {
    return (Compiler_Options){ .assembler_path = assembler_path, .linker_path = linker_path, .assemble_flags = "\0", .linkage_flags = "\0", .flags = flags };
}

static inline Compiler create_compiler(char *infile, char *outfile, Compiler_Options options) {
    return (Compiler){ .input_path = infile, .output_path = outfile, .options = options };
}

bool compile(Compiler *compiler);

#endif

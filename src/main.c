#include "compiler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define TAG "0.1.1-alpha"

#define VERSION "ki-v3 build " TAG "\n"

static void help(const char *context) {
    printf("Usage: %s <command> [options...] <file>\n"
           "Commands:\n"
           "    build                Produce an executable\n"
           "    document             Produce a markdown file\n"
           "    ir                   Produce an IR file\n"
           "    object               Produce an object file\n"
           "    source               Produce an assembly file\n"
           "Options:\n"
           "    --help               Show this information\n"
           "    --version            Show the compiler version\n"
           "    -as-flags <\"...\">    Specify flags to pass during assembling\n"
           "    -doc-exported        Show only exported symbols in documentation\n"
           "    -freestanding        Don't use the standard library or initialize the heap\n"
           "    -g                   Build with debugging information\n"
           "    -ir-explicit         Generate IR in explicit form\n"
           "    -ld <path>           Specify the linker to use in compilation\n"
           "    -ld-flags <\"...\">    Specify flags to pass during linkage\n"
           "    -o <name>            Specify the output filename\n"
           "    -unopt               Disable optimization\n"
           "    -source-libs         Show library externs in assembly source code\n"
           , context);
}

static bool parse_option_argument(const int argc, const size_t index, const char *option, const char *argument) {
    if (index + 1 < (size_t)argc)
        return true;

    log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
        "Missing <%s> to option '%s'\n", argument, option);
    return false;
}

static bool option_is_valid_with_command_and_other_options(const bool condition, const char *option) {
    if (!condition)
        log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
            "Invalid combination of command or options with option '%s'\n", option);

    return condition;
}

static bool parse_command_line(const int argc, char **argv, Compiler *compiler) {
    if (argc == 1) {
        help(argv[0]);
        return EXIT_SUCCESS;
    }

    const char *command = argv[1];

    if (strcmp(command, "--help") == 0) {
        help(argv[0]);
        return EXIT_SUCCESS;
    } else if (strcmp(command, "--version") == 0) {
        printf(VERSION);
        return EXIT_SUCCESS;
    } else if (strcmp(command, "source") == 0)
        compiler->options.flags |= COMP_SOURCE | COMP_COMPILE_ASM;
    else if (strcmp(command, "document") == 0)
        compiler->options.flags |= COMP_DOCUMENT;
    else if (strcmp(command, "ir") == 0)
        compiler->options.flags |= COMP_IR;
    else if (strcmp(command, "object") == 0)
        compiler->options.flags |= COMP_OBJECT | COMP_COMPILE_ASM;
    else if (strcmp(command, "build") == 0)
        compiler->options.flags |= COMP_COMPILE_ASM;
    else {
        log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
            "No such command '%s'\n", command);
        return false;
    }

    for (int i = 2; i < argc - 1; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            help(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(arg, "--version") == 0) {
            printf(VERSION);
            return EXIT_SUCCESS;
        } else if (strcmp(arg, "-as-flags") == 0) {
            if (!option_is_valid_with_command_and_other_options(
                    (compiler->options.flags & COMP_COMPILE_ASM) || (compiler->options.flags & COMP_OBJECT), arg) ||
                !parse_option_argument(argc, i, "-as-flags", "<\"...\">"))
                continue;

            compiler->options.assemble_flags = argv[++i];
        } else if (strcmp(arg, "-doc-exported") == 0) {
            if (option_is_valid_with_command_and_other_options(compiler->options.flags & COMP_DOCUMENT, arg))
                compiler->options.flags |= COMP_DOC_EXPORTED;
        } else if (strcmp(arg, "-freestanding") == 0) {
            if (option_is_valid_with_command_and_other_options(compiler->options.flags & COMP_COMPILE_ASM, arg))
                compiler->options.flags |= COMP_FREESTANDING;
        } else if (strcmp(arg, "-ir-explicit") == 0) {
            if (option_is_valid_with_command_and_other_options(compiler->options.flags & COMP_IR, arg))
                compiler->options.flags |= COMP_EXPLICIT_IR;
        } else if (strcmp(arg, "-g") == 0) {
            if (option_is_valid_with_command_and_other_options(
                    !(compiler->options.flags & COMP_SOURCE) && !(compiler->options.flags & COMP_IR), arg))
                compiler->options.flags |= COMP_DEBUGINFO;
        } else if (strcmp(arg, "-ld") == 0) {
            if (option_is_valid_with_command_and_other_options(
                    !(compiler->options.flags & COMP_SOURCE) && !(compiler->options.flags & COMP_IR), arg) ||
                !parse_option_argument(argc, i, "-ld", "<path>"))
                compiler->options.linker_path = argv[++i];
        } else if (strcmp(arg, "-ld-flags") == 0) {
            if (!option_is_valid_with_command_and_other_options(
                    !(compiler->options.flags & COMP_SOURCE) && !(compiler->options.flags & COMP_IR), arg) ||
                !parse_option_argument(argc, i, "-ld-flags", "<\"...\">"))
                continue;

            compiler->options.linkage_flags = argv[++i];
        } else if (strcmp(arg, "-o") == 0 && parse_option_argument(argc, i, "-o", "<name>")) {
            compiler->output_path = argv[++i];
            compiler->options.flags |= COMP_OUTFILE_SPECIFIED;
        } else if (strcmp(arg, "-source-libs") == 0) {
            if (option_is_valid_with_command_and_other_options(
                    (compiler->options.flags & COMP_SOURCE) || (compiler->options.flags & COMP_IR), "-source-libs"))
                compiler->options.flags |= COMP_SOURCE_LIBS;
        } else if (strcmp(arg, "-unopt") == 0)
            compiler->options.flags |= COMP_UNOPTIMIZED;
        else {
            log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
                "Unknown option '%s'\n", arg);
            return false;
        }
    }

    compiler->input_path = argv[argc - 1];
    return true;
}

int main(const int argc, char **argv) {
    Compiler compiler = create_compiler(NULL, "a.out", create_compiler_options("nasm", "ld", 0));

    if (!parse_command_line(argc, argv, &compiler))
        return EXIT_FAILURE;

    return compile(&compiler) ? EXIT_SUCCESS : EXIT_FAILURE;
}

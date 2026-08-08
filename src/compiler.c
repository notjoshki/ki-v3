#include "compiler.h"
#include "parser.h"
#include "ast.h"
#include "utilities.h"
#include "context.h"
#include "logger.h"
#include "resolver.h"
#include "hir.h"
#include "lir.h"
#include "ir_representation.h"
#include "backend.h"
#include "optimizer.h"
#include "documentor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

static bool write_output_code(const char *path, const char *code, const char *outfile) {
    FILE *file = fopen(outfile, "w");

    if (file == NULL) {
        log(ERROR_CRITICAL, path, LOG_NOLN, LOG_NOCOL,
            "Failed to write output to '%s'\n", outfile);
        return false;
    }

    fputs(code, file);
    fclose(file);
    return true;
}

static bool compile_from_resolved_root(const Compiler *compiler, Context *context, char *path, size_t path_length,
        AST *root, const bool is_entry, char **out_output_file, char *remove_outfile_path) {
    HIR hir = { .type = HIR_NOP };

    if (get_error_count() == 0)
        hir = ast_to_hir(context, root);

    LIR lir = { .count = 0 };

    if (get_error_count() == 0) {
        lir = hir_to_lir(context, &hir);

        if (!(compiler->options.flags & COMP_UNOPTIMIZED))
            optimize_lir(&lir); 
    }

    if (get_error_count() > 0) {
        if (hir.type != HIR_NOP)
            delete_hir(&hir);

        if (lir.count > 0)
            delete_lir(&lir);

        *out_output_file = NULL;
        return false;
    }

    Symbol *entry = NULL;
    bool status = true;
    
    if (is_entry) {
        entry = find_symbol(context, SYMBOL_FUNCTION, context->entrypoint_function, context->entrypoint_function_length, 
            FIND_IN_ANY_SCOPE, root->module_uid);

        if (entry == NULL && !(compiler->options.flags & COMP_IR) && !(compiler->options.flags & COMP_SOURCE)) {
            log(ERROR_CRITICAL, path, LOG_NOLN, LOG_NOCOL, "Missing entrypoint function\n");
            status = false;
        }
    }

    char *code;
    // remove_outfile_path will either be NULL to mean no, or not NULL to be the module name only.
    (void)path_length;
    char *outfile_path = remove_outfile_path == NULL ? path : remove_outfile_path;
    
    if (compiler->options.flags & COMP_IR) {
        code = lir_to_string(&lir, compiler->options.flags & COMP_EXPLICIT_IR);
        *out_output_file = change_file_extension(outfile_path, strlen(outfile_path), "kir");
    } else {
        code = emit_assembly(context, &lir, entry, !(compiler->options.flags & COMP_FREESTANDING));
        *out_output_file = change_file_extension(outfile_path, strlen(outfile_path), "asm");
    }

    const bool write_status = write_output_code(outfile_path, code, *out_output_file);

    if (!write_status || !status) {
        status = false;
        free(*out_output_file);
        *out_output_file = NULL;
    } else
        status = status && write_status;

    free(code);
    delete_hir(&hir);
    delete_lir(&lir);
    return status;
}

static void import_builtin_libraries(AST *unresolved_main_root) {
    const char *basic_path = KI_LIB_DIRECTORY "/basic.ki";
    AST *basic = new_ast(AST_IMPORT, ast_location(unresolved_main_root), unresolved_main_root->uid);
    basic->import.path = copy_string(basic_path, strlen(basic_path));
    basic->import.path_length = strlen(basic_path);
    basic->import.from_groups = create_list(sizeof(AST *));
    basic->import.from_identifiers = create_list(sizeof(AST *));
    basic->import.as_name = NULL;
    basic->import.use_all_symbols = true;
    push_item(&unresolved_main_root->root.nodes, (AST *)basic);
}

static bool compile_main_module(const Compiler *compiler, Context *context, char *path, size_t path_length, char **out_output_file, AST **out_root) {
    char *directory;
    size_t directory_len;
    char *name = parse_module_path_utility(path, path_length, &directory, &directory_len, true);

    Parser *parser;
    AST *root = initialize_root(context, name, strlen(name), path, path_length, directory, directory_len, false, &parser);

    if (!(compiler->options.flags & COMP_FREESTANDING))
        import_builtin_libraries(root);

    parse_root(root, parser);
    free(directory);

    Module *module = get_module(context, 0); // main is always 0.
    module->root = root; // This is done in the resolver for imported modules, but not main.

    if (get_error_count() == 0)
        resolve_ast(context, root);

    *out_root = root;
    bool status = compile_from_resolved_root(compiler, context, path, path_length, root, true, out_output_file,
        (compiler->options.flags & COMP_OUTFILE_SPECIFIED) ? compiler->output_path : name);
    free(name);
    return status;
}

static void cleanup(Context *context, char **output_files, AST **roots, const size_t count, const bool free_modules) {
    if (context != NULL) { 
        delete_context(context, free_modules);
        context = NULL;
    }

    if (output_files != NULL) {
        for (size_t i = 0; i < count; i++)
            free(output_files[i]);

        free(output_files);
    }

    if (roots != NULL)
        free(roots);
}

static bool compile_modules(Compiler *compiler, Context *context, char *main_path, size_t main_path_length, 
        char ***out_output_files, AST ***out_roots, size_t *out_count) {
    *out_output_files = NULL;
    *out_roots = NULL;

    char *main_outfile;
    AST *main_root = NULL;

    if (!compile_main_module(compiler, context, main_path, main_path_length, &main_outfile, &main_root))
        return false;

    AST **roots = malloc((context->module_count + 1) * sizeof(AST *));
    roots[0] = main_root;
    char **files = malloc((context->module_count + 1) * sizeof(char *));
    files[0] = main_outfile;
    *out_count = context->module_count;

    // Module 0 is main, already done.
    for (size_t i = 1; i < context->module_count; i++) {
        const Module *module = get_module(context, i);
        files[i] = module->name;
        roots[i] = module->root;

        if (!compile_from_resolved_root(compiler, context, module->path, module->path_length, module->root, false, &files[i], module->name)) {
            cleanup(NULL, files, roots, i, false);
            return false;
        }
    }

    *out_roots = roots;
    *out_output_files = files;
    return true;
}

static bool assemble_file(const Compiler *compiler, const char *file, char **out_object) {
    const size_t file_len = strlen(file);
    *out_object = change_file_extension(file, file_len, "o");

    char *command = malloc(strlen(compiler->options.assembler_path) + file_len + strlen(*out_object) + 24);
    sprintf(command, "%s -felf64 %s-o %s %s", compiler->options.assembler_path, compiler->options.flags & COMP_DEBUGINFO ? "-g " : "\0",
        *out_object, file);

    const int status = system(command);
    free(command);
    return status == 0;
}

static void remove_files(char **files, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (remove(files[i]) != 0)
            log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL, "Failed to remove '%s'\n", files[i]);
    }
}

static bool assemble_files(const Compiler *compiler, char **files, const size_t file_count, char ***out_objects) {
    char **objs = malloc(file_count * sizeof(char *));

    for (size_t i = 0; i < file_count; i++) {
        if (!assemble_file(compiler, files[i], &objs[i])) {
            remove_files(files, file_count);
            cleanup(NULL, objs, NULL, i + 1, false);
            *out_objects = NULL;
            return false;
        }

    }

    if (!(compiler->options.flags & COMP_DEBUGINFO))
        remove_files(files, file_count);

    *out_objects = objs;
    return true;
}

static bool link_files(const Compiler *compiler, char **files, const size_t file_count) {
    char *str = calloc(1, sizeof(char));
    size_t str_len = 0;

    for (size_t i = 0; i < file_count; i++) {
        const size_t len = strlen(files[i]);
        str = realloc(str, str_len + len + 2);
        strcat(str, " ");
        strcat(str, files[i]);
        str_len += len + 1;
    }

    char *command = malloc(strlen(compiler->options.linker_path) + strlen(compiler->output_path) + str_len + 12);
    sprintf(command, "%s -o %s %s", compiler->options.linker_path, compiler->output_path, str);
    free(str);

    const int status = system(command);
    free(command);
    return status == 0;
}

static bool compile_only_main_to_source(Compiler *compiler, Context *context) {
    char *outfile = NULL;
    AST *root = NULL;
    bool status = compile_main_module(compiler, context, compiler->input_path, strlen(compiler->input_path), &outfile, &root);

    if (status)
        free(outfile);

    cleanup(context, NULL, NULL, 0, true);
    return status;
}

static bool compile_only_main_to_markdown(Compiler *compiler, Context *context) {
    char *directory;
    size_t directory_len;
    char *name = parse_module_path_utility(compiler->input_path, strlen(compiler->input_path), &directory, &directory_len, true);
    const size_t name_len = strlen(name);

    Parser *parser;
    AST *root = initialize_root(context, name, name_len, compiler->input_path, strlen(compiler->input_path), 
        directory, directory_len, true, &parser);
    free(directory);

    if (!(compiler->options.flags & COMP_FREESTANDING))
        import_builtin_libraries(root);

    parse_root(root, parser);

    Module *module = get_module(context, 0); // main is always 0.
    module->root = root; // This is done in the resolver for imported modules, but not main.

    if (get_error_count() == 0)
        resolve_ast(context, root);

    if (get_error_count() > 0) {
        cleanup(context, NULL, NULL, 0, true);
        return false;
    }

    char *code = generate_documentation(context, root, compiler->options.flags & COMP_DOC_EXPORTED);
    char *md_file = change_file_extension(name, name_len, "md");
    free(name);

    const bool status = write_output_code(compiler->input_path, code, md_file);
    free(md_file);
    free(code);
    cleanup(context, NULL, NULL, 0, true);
    return status;
}

bool compile(Compiler *compiler) {
    Context context = create_context("main");

    if ((compiler->options.flags & COMP_IR) || (compiler->options.flags & COMP_SOURCE))
        return compile_only_main_to_source(compiler, &context);
    else if (compiler->options.flags & COMP_DOCUMENT)
        return compile_only_main_to_markdown(compiler, &context);

    char **output_files = NULL;
    size_t output_file_count;
    AST **roots = NULL;

    if (!compile_modules(compiler, &context, compiler->input_path, strlen(compiler->input_path), &output_files, &roots, &output_file_count)) {
        log(ERROR_CRITICAL, compiler->input_path, LOG_NOLN, LOG_NOCOL,  "Failed to compile modules\n");
        cleanup(&context, NULL, NULL, 0, true);
        return false;
    }

    char **object_files = NULL;
    
    if (!assemble_files(compiler, output_files, output_file_count, &object_files)) {
        log(ERROR_CRITICAL, compiler->input_path, LOG_NOLN, LOG_NOCOL,  "Failed to assemble files\n");
        cleanup(&context, output_files, roots, output_file_count, true);
        return false;
    }

    if (!(compiler->options.flags & COMP_OBJECT)) {
        if (!link_files(compiler, object_files, output_file_count))
            log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL, "Failed to link files\n");

        remove_files(object_files, output_file_count);
    }

    cleanup(NULL, object_files, NULL, output_file_count, false);
    cleanup(&context, output_files, roots, output_file_count, true);
    return true;
}
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <stddef.h>
#include <limits.h>
#include <glob.h>
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <zlib.h>
#include <fcntl.h>
#include "interpreter.h"
#include "lexer.h"
#include "program.h"

static int resolve_target_line(Token tok) {
    if (tok.type == TOKEN_NUMBER) return tok.int_val;
    if (tok.type == TOKEN_IDENTIFIER) {
        Statement *s = find_label(tok.text);
        if (s) return s->line_number;
    }
    return -1;
}
#include "graphics.h"
#include "audio.h"

volatile sig_atomic_t stop_running = 0;
/* Set by STOP and Ctrl+C: the program pauses with "Break in N" and CONT can resume it. */
volatile sig_atomic_t break_requested = 0;
/* Line number given to statements typed in direct mode (GW-BASIC uses 65535). */
#define DIRECT_LINE_NUMBER 65535

#define MAX_VARIABLES 8192
static Variable vars[MAX_VARIABLES];
static int var_count = 0;

#define MAX_NAMED_CONSTANTS 256
typedef struct {
    char name[64];
    Num value;
    int is_wide; /* LONG/DOUBLE-valued constants print with full precision */
} NamedConstant;
static NamedConstant named_constants[MAX_NAMED_CONSTANTS];
static int named_constant_count = 0;
/* Bumped when a constant is added or the table is cleared, so the per-token
 * lookup cache (find_named_constant_tok) knows to look again. */
static unsigned int constant_generation = 1;

#define MAX_USER_TYPES 64
#define MAX_TYPE_FIELDS 64
#define MAX_RECORD_INSTANCES 128
typedef struct {
    char name[32];
    char type_name[32];
    int num_dims;
    int dims[3];
    int lower_bounds[3];
    size_t fixed_string_length;
    int is_fixed_string;
    Statement *declaration_stmt;
} TypeField;

typedef struct {
    char name[32];
    TypeField fields[MAX_TYPE_FIELDS];
    int field_count;
    Statement *declaration_stmt;
} UserType;

static UserType user_types[MAX_USER_TYPES];
static int user_type_count = 0;
typedef struct {
    int root_variable_index;
    const struct ProcedureDefTag *scope; /* declaring procedure, NULL at module level */
    int type_index;
    int num_dims;
    int dims[3];
    int lower_bounds[3];
    int element_count;
} UserTypeInstance;

static UserTypeInstance user_type_instances[MAX_RECORD_INSTANCES];
static int user_type_instance_count = 0;
#define VAR_HASH_CAPACITY 16384
#define VAR_HASH_MASK (VAR_HASH_CAPACITY - 1)
/* Open-addressed table; load stays at or below 50% (MAX_VARIABLES). */
static int var_hash_table[VAR_HASH_CAPACITY];
static int var_hash_initialized = 0;

typedef struct {
    Token *tokens;
    int pos;
    Statement *statement;
} TokenStream;

static int print_col = 0;
static int print_row = 0;
static int internal_argc = 0;
static char **internal_argv = NULL;
static char internal_command_line[1024] = "";
static char qb64_start_dir[4096] = ""; // _STARTDIR$: where basika started
static struct timespec qb64_start_time; // _UPTIME counts from here
static int fixed_string_declarations_present = 0;

static unsigned char basika_memory[65536];

static int last_expression_is_double = 0;

static char default_type_map[26] = {
    '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!',
    '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!'
};
static unsigned int default_type_generation = 1;

static FILE *file_handles[16] = {NULL};
typedef struct {
    int var_idx;
    int array_idx;
    int offset;
    int len;
} FieldBinding;

typedef struct {
    char *buffer;
    int size;
    int binding_count;
    FieldBinding bindings[64];
} FileFieldState;

static FileFieldState file_field_state[16];
/* QBasic file modes: RANDOM files address LEN-byte records (default 128) and
 * BINARY files address bytes; LOC reports the last record or byte used. */
enum { FILE_MODE_INPUT = 'I', FILE_MODE_OUTPUT = 'O', FILE_MODE_APPEND = 'A',
       FILE_MODE_RANDOM = 'R', FILE_MODE_BINARY = 'B' };
static int file_mode[16];
static int file_record_length[16];
static long file_last_record[16];
static int get_field_binding_for_var(int idx, int array_idx, int *out_fnum, FieldBinding *out_binding);

static unsigned int rnd_seed = 1;
static double last_rnd_value = 0.0;
static int on_error_goto_line = 0;
static int runtime_error_occurred = 0;
static int last_runtime_error_code = 0;
static int last_runtime_error_line = 0;
static int current_executing_line = 0;
static int current_source_line_number = 0;
static int current_has_explicit_line_number = 0;
static char last_runtime_error_msg[256] = "";
static Statement *error_stmt = NULL;
static const char *error_ptr = NULL;
static const char *error_next_ptr = NULL;

typedef struct {
    unsigned char op;
    int left;
    int right;
    Token token;
} CompiledExpressionNode;

enum {
    EXPR_NUMBER,
    EXPR_VARIABLE,
    EXPR_NEGATE,
    EXPR_NOT,
    EXPR_ADD,
    EXPR_SUBTRACT,
    EXPR_MULTIPLY,
    EXPR_DIVIDE,
    EXPR_MOD,
    EXPR_IDIV,
    EXPR_POWER,
    EXPR_EQUAL,
    EXPR_NOT_EQUAL,
    EXPR_LESS,
    EXPR_LESS_EQUAL,
    EXPR_GREATER,
    EXPR_GREATER_EQUAL,
    EXPR_AND,
    EXPR_OR,
    EXPR_XOR
};

#define MAX_COMPILED_EXPRESSION_NODES 128

typedef struct {
    char name[32];
    char param_name[32];
    char expression[256];
    CompiledExpressionNode compiled_nodes[MAX_COMPILED_EXPRESSION_NODES];
    int compiled_node_count;
    int compiled_root;
    int has_compiled_expression;
} UserFunction;

static UserFunction user_functions[64];
static int user_function_count = 0;

#define MAX_PROCEDURES 128
#define MAX_PROC_PARAMS 16

typedef struct {
    char name[32];
    int is_string;
    int is_array;
    int user_type_index;
} ProcParamDef;

typedef struct ProcedureDefTag {
    char name[32];
    int is_function; // 0 for SUB, 1 for FUNCTION
    int is_static;
    int param_count;
    ProcParamDef params[MAX_PROC_PARAMS];
    Statement *header_stmt;
    Statement *start_stmt;
    Statement *end_stmt;
} ProcedureDef;

static ProcedureDef registered_procs[MAX_PROCEDURES];
static int proc_count = 0;
static unsigned int proc_generation = 1; /* bumped on each scan_procedures */

#define MAX_CALL_FRAMES 64
#define FRAME_LOCAL_CHUNK 128
#define MAX_FRAME_LOCALS 8192

typedef struct {
    ProcedureDef *proc;
    Statement *return_stmt;
    int return_token_idx;
    /* Depths of the main program's loop and block stacks when a SUB it runs
     * inline was called; the SUB's own entries are dropped when it returns. */
    int for_base, while_base, do_base, block_if_base, select_base;
    
    /* Locals are stored in 128-entry chunks allocated on first use, so large
     * local record arrays fit while pointers to existing locals stay valid. */
    Variable *local_chunks[MAX_FRAME_LOCALS / FRAME_LOCAL_CHUNK];
    int local_var_count;
    /* Per-call cache of variable lookups. Clearing all MAX_VARIABLES entries on
     * every call is costly, so the indexes that were filled are recorded and
     * only those are cleared (all of them if the list overflowed). */
    Variable *resolved_variables[MAX_VARIABLES];
    int resolved_touched[256];
    int resolved_touched_count;
    
    Variable *byref_caller_variable[MAX_PROC_PARAMS];
    int byref_caller_var_idx[MAX_PROC_PARAMS];
    int byref_caller_frame[MAX_PROC_PARAMS];
    
    char shared_var_names[32][32];
    int shared_count;

    double func_return_numeric;
    BasicString *func_return_string;
    int return_depth; /* call_stack_depth to restore when this call returns */
} CallFrame;

static CallFrame call_stack[MAX_CALL_FRAMES];
/* Calls whose arguments are still being evaluated. Their frames sit just
 * above the stack, so a call made while evaluating an argument (F(1, G(2)))
 * takes the next slot instead of overwriting the half-built frame. */
static int frames_being_built = 0;

static void report_runtime_error(RuntimeError code);

/* Local variable `index` of a frame, allocating its chunk on first use. */
static Variable *frame_local(CallFrame *frame, int index) {
    static Variable fallback;
    int chunk = index / FRAME_LOCAL_CHUNK;
    if (index < 0 || chunk >= MAX_FRAME_LOCALS / FRAME_LOCAL_CHUNK) return &fallback;
    if (!frame->local_chunks[chunk]) {
        frame->local_chunks[chunk] = calloc(FRAME_LOCAL_CHUNK, sizeof(Variable));
        if (!frame->local_chunks[chunk]) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return &fallback;
        }
    }
    return &frame->local_chunks[chunk][index % FRAME_LOCAL_CHUNK];
}
static int call_stack_depth = 0;

static int find_variable_raw_name(const char *name);
static void report_runtime_error(RuntimeError code);
static Variable *get_variable_ptr_for_frame(int idx, int frame_index);

static ProcedureDef *find_procedure(const char *name) {
    if (!name || !name[0]) return NULL;
    char norm[64];
    size_t i;
    for (i = 0; i < 63 && name[i]; i++) norm[i] = (char)toupper((unsigned char)name[i]);
    norm[i] = '\0';
    for (int p = 0; p < proc_count; p++) {
        if (strcasecmp(registered_procs[p].name, norm) == 0) return &registered_procs[p];
    }
    // As in QB64, a FUNCTION Name& (or Name$, ...) can be named without its
    // suffix; a different suffix is a different name.
    if (strchr("$%&!#", norm[i - 1])) return NULL;
    for (int p = 0; p < proc_count; p++) {
        const char *proc_name = registered_procs[p].name;
        if (strncasecmp(proc_name, norm, i) != 0 || !proc_name[i]) continue;
        if (strspn(proc_name + i, "$%&!#~") == strlen(proc_name + i)) return &registered_procs[p];
    }
    return NULL;
}

/* find_procedure_tok - Cached find_procedure for hot expression paths; the
 * result is reused until scan_procedures rebuilds the procedure table */
static ProcedureDef *find_procedure_tok(Token *token) {
    if (token->proc_generation != proc_generation) {
        token->proc_cache = find_procedure(token->text);
        token->proc_generation = proc_generation;
    }
    return (ProcedureDef *)token->proc_cache;
}

static int qb64_function_id(const char *name);

/* A user FUNCTION or a QB64 built-in such as _RESIZEWIDTH: a value, never a
 * by-reference variable. */
static int qb64_file_function_id(const char *name);
static int qb64_string_function_id(const char *name);
static int qb64_argument_bit_width(const Token *tokens, int pos, int function_id);
static int qb64_rotates_variable(const Token *tokens, int pos, int function_id);
/* The bit width _ROL and _ROR turn through, from the declared type of the
 * variable the value came from when that is known, else the value's kind;
 * and whether that variable is _UNSIGNED. */
static int qb64_bit_width_hint = 0;
static int qb64_bit_width_unsigned = 0;

static int is_function_name(const char *name) {
    if (name[0] == '_' && (qb64_function_id(name) || qb64_file_function_id(name) ||
                           qb64_string_function_id(name))) return 1;
    ProcedureDef *proc = find_procedure(name);
    return proc && proc->is_function;
}

static ProcedureDef *find_procedure_by_header(Statement *header_stmt) {
    if (!header_stmt) return NULL;
    for (int p = 0; p < proc_count; p++) {
        if (registered_procs[p].header_stmt == header_stmt) return &registered_procs[p];
    }
    return NULL;
}

/* Type suffixes: $ STRING, % INTEGER, & LONG, ~& _UNSIGNED LONG, ! SINGLE, # DOUBLE. */
static int is_type_suffix_char(char c) {
    return c == '$' || c == '%' || c == '!' || c == '#' || c == '&';
}

/* Length of name without a trailing numeric suffix (%, !, #, & or ~&). */
static size_t type_suffix_length(const char *name, size_t len);

static size_t numeric_base_length(const char *name, size_t len) {
    if (len == 0 || name[len - 1] == '$') return len;
    return len - type_suffix_length(name, len);
}

/* Primitive types: name in DIM/TYPE/parameters, variable suffix, record
 * size in bytes, and integer range. */
typedef struct {
    const char *name;
    const char *suffix;
    int bytes;
    int is_integer;
    int is_unsigned;
    double min_value, max_value;
} PrimitiveType;

static const PrimitiveType primitive_types[] = {
    {"STRING", "$", 0, 0, 0, 0, 0},
    {"_BYTE", "%%", 1, 1, 0, -128, 127},
    {"_UNSIGNED _BYTE", "~%%", 1, 1, 1, 0, 255},
    {"INTEGER", "%", 2, 1, 0, -32768, 32767},
    {"_UNSIGNED INTEGER", "~%", 2, 1, 1, 0, 65535},
    {"LONG", "&", 4, 1, 0, -2147483648.0, 2147483647.0},
    {"_UNSIGNED LONG", "~&", 4, 1, 1, 0, 4294967295.0},
    {"_INTEGER64", "&&", 8, 1, 0, -9223372036854775808.0, 9223372036854775807.0},
    {"_UNSIGNED _INTEGER64", "~&&", 8, 1, 1, 0, 18446744073709551615.0},
    {"SINGLE", "!", 4, 0, 0, 0, 0},
    {"DOUBLE", "#", 8, 0, 0, 0, 0},
};

static const PrimitiveType *find_primitive_type(const char *type_name) {
    if (!type_name) return NULL;
    for (size_t i = 0; i < sizeof(primitive_types) / sizeof(primitive_types[0]); i++) {
        if (strcasecmp(type_name, primitive_types[i].name) == 0) return &primitive_types[i];
    }
    return NULL;
}

static const char *primitive_type_suffix(const char *type_name) {
    const PrimitiveType *type = find_primitive_type(type_name);
    return type ? type->suffix : NULL;
}

/* Length of the type suffix ending a variable name ("~&&", "%%", "!", ...). */
static size_t type_suffix_length(const char *name, size_t len) {
    if (len == 0) return 0;
    char last = name[len - 1];
    if (last == '!' || last == '#' || last == '$') return 1;
    if (last != '%' && last != '&') return 0;
    size_t n = (len >= 2 && name[len - 2] == last) ? 2 : 1;
    if (len > n && name[len - n - 1] == '~') n++;
    return n;
}

/* The primitive type named by a variable's suffix, or NULL. */
static const PrimitiveType *primitive_type_of_name(const char *name) {
    size_t len = strlen(name);
    size_t n = type_suffix_length(name, len);
    if (n == 0) return NULL;
    for (size_t i = 0; i < sizeof(primitive_types) / sizeof(primitive_types[0]); i++) {
        if (strcmp(name + len - n, primitive_types[i].suffix) == 0) return &primitive_types[i];
    }
    return NULL;
}

/* Two-word type names: "_UNSIGNED" followed by INTEGER, LONG, _BYTE or
 * _INTEGER64. Returns the combined name or NULL. */
static const char *unsigned_type_name(const char *first, const char *second) {
    if (strcasecmp(first, "_UNSIGNED") != 0 || !second) return NULL;
    if (strcasecmp(second, "INTEGER") == 0) return "_UNSIGNED INTEGER";
    if (strcasecmp(second, "LONG") == 0) return "_UNSIGNED LONG";
    if (strcasecmp(second, "_BYTE") == 0) return "_UNSIGNED _BYTE";
    if (strcasecmp(second, "_INTEGER64") == 0) return "_UNSIGNED _INTEGER64";
    return NULL;
}

static int proc_name_match(const char *name1, const char *name2) {
    if (!name1 || !name2) return 0;
    size_t len1 = numeric_base_length(name1, strlen(name1));
    size_t len2 = numeric_base_length(name2, strlen(name2));
    if (len1 != len2) return 0;
    return strncasecmp(name1, name2, len1) == 0;
}

static int has_record_prefix(const char *name, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return strncasecmp(name, prefix, prefix_length) == 0 &&
        (name[prefix_length] == '.' || name[prefix_length] == '@');
}

/* Names declared with DIM ... AS type (or typed parameters) resolve to the
 * matching suffixed variable within their scope. A NULL scope is module level;
 * shared entries also apply inside every procedure. */
#define MAX_DECLARED_TYPES 512
typedef struct {
    char name[64];
    char suffix[4];
    const ProcedureDef *scope;
    int shared;
} DeclaredType;
static DeclaredType declared_types[MAX_DECLARED_TYPES];
static int declared_type_count = 0;

/* Variables declared with DIM SHARED are visible in every procedure. */
#define MAX_GLOBAL_SHARED 256
typedef struct {
    char full[64];
    char base[64];
} GlobalSharedName;
static GlobalSharedName global_shared_names[MAX_GLOBAL_SHARED];
static int global_shared_count = 0;

static int global_shared_match(const char *name) {
    for (int s = 0; s < global_shared_count; s++) {
        if (proc_name_match(global_shared_names[s].full, name) ||
            has_record_prefix(name, global_shared_names[s].base)) return 1;
    }
    return 0;
}

static const ProcedureDef *current_declaration_scope(void) {
    return call_stack_depth > 0 ? call_stack[call_stack_depth - 1].proc : NULL;
}

/* Returns the declared suffix for an unsuffixed name visible in the current scope. */
static const char *find_declared_suffix(const char *name) {
    if (declared_type_count == 0) return NULL;
    const ProcedureDef *scope = current_declaration_scope();
    const char *shared_match = NULL;
    // Entries are uppercase; compare the first letter before calling strcasecmp.
    int first = (unsigned char)name[0];
    if (first >= 'a' && first <= 'z') first -= 'a' - 'A';
    for (int i = 0; i < declared_type_count; i++) {
        DeclaredType *entry = &declared_types[i];
        if ((unsigned char)entry->name[0] != first || strcasecmp(entry->name, name) != 0) continue;
        if (entry->scope == scope) return entry->suffix;
        if (entry->shared && !shared_match) shared_match = entry->suffix;
    }
    return shared_match;
}

static unsigned int default_type_generation;

/* Bumped whenever anything that changes how a name resolves to a variable,
 * CONST or procedure changes (DEFtype/DIM-AS declarations, CONST statements,
 * procedure and user-type scans, CLEAR), so that compiled expressions can
 * cache each CEXPR_VARIABLE name's resolution in the instruction itself. */
static unsigned int expression_name_generation = 1;

static void invalidate_expression_name_cache(void) {
    expression_name_generation++;
    if (expression_name_generation == 0) expression_name_generation = 1;
}

static int declare_variable_type(const char *name, const char *suffix,
                                 const ProcedureDef *scope, int shared) {
    char base[64];
    size_t len = 0;
    while (name[len] && len < sizeof(base) - 1 && !is_type_suffix_char(name[len]) && name[len] != '~') {
        base[len] = (char)toupper((unsigned char)name[len]);
        len++;
    }
    base[len] = '\0';
    if (len == 0) return 0;
    for (int i = 0; i < declared_type_count; i++) {
        DeclaredType *entry = &declared_types[i];
        if (entry->scope == scope && strcmp(entry->name, base) == 0) {
            if (strcmp(entry->suffix, suffix) != 0) return 0;
            if (shared) entry->shared = 1;
            return 1;
        }
    }
    if (declared_type_count >= MAX_DECLARED_TYPES) return 0;
    DeclaredType *entry = &declared_types[declared_type_count++];
    snprintf(entry->name, sizeof(entry->name), "%s", base);
    snprintf(entry->suffix, sizeof(entry->suffix), "%s", suffix);
    entry->scope = scope;
    entry->shared = shared;
    /* Cached token lookups must re-resolve against the new declaration. */
    default_type_generation++;
    if (default_type_generation == 0) default_type_generation = 1;
    invalidate_expression_name_cache();
    return 1;
}

static void clear_procedure_declared_types(void) {
    int kept = 0;
    for (int i = 0; i < declared_type_count; i++) {
        if (declared_types[i].scope == NULL) declared_types[kept++] = declared_types[i];
    }
    declared_type_count = kept;
    default_type_generation++;
    if (default_type_generation == 0) default_type_generation = 1;
    invalidate_expression_name_cache();
}

/* Locals of a STATIC procedure, and names listed in a STATIC statement,
 * live here rather than in the call frame so they keep their values between
 * calls. Both lists are cleared when the program is run again. */
#define MAX_STATIC_LOCALS 1024
#define MAX_STATIC_NAMES 256
typedef struct {
    const ProcedureDef *proc;
    Variable var;
} StaticLocal;
typedef struct {
    const ProcedureDef *proc;
    char name[64];
} StaticName;
static StaticLocal static_locals[MAX_STATIC_LOCALS];
static int static_local_count = 0;
static StaticName static_names[MAX_STATIC_NAMES];
static int static_name_count = 0;

static void add_static_name(const ProcedureDef *proc, const char *name) {
    for (int i = 0; i < static_name_count; i++) {
        if (static_names[i].proc == proc && strcmp(static_names[i].name, name) == 0) return;
    }
    if (static_name_count >= MAX_STATIC_NAMES) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return;
    }
    static_names[static_name_count].proc = proc;
    snprintf(static_names[static_name_count].name, sizeof(static_names[0].name), "%s", name);
    static_name_count++;
}

static int is_static_local(const ProcedureDef *proc, const char *name) {
    if (proc->is_static) return 1;
    for (int i = 0; i < static_name_count; i++) {
        if (static_names[i].proc != proc) continue;
        const char *listed = static_names[i].name;
        size_t root_length = numeric_base_length(listed, strlen(listed));
        /* Arrays and record fields share the listed name as their root. */
        if (proc_name_match(listed, name) ||
            (strncasecmp(name, listed, root_length) == 0 &&
             (name[root_length] == '.' || name[root_length] == '@'))) return 1;
    }
    return 0;
}

static Variable *static_local_variable(const ProcedureDef *proc, const char *name) {
    for (int i = 0; i < static_local_count; i++) {
        if (static_locals[i].proc == proc && strcasecmp(static_locals[i].var.name, name) == 0) {
            return &static_locals[i].var;
        }
    }
    if (static_local_count >= MAX_STATIC_LOCALS) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return &vars[0];
    }
    StaticLocal *slot = &static_locals[static_local_count++];
    memset(slot, 0, sizeof(*slot));
    slot->proc = proc;
    snprintf(slot->var.name, sizeof(slot->var.name), "%s", name);
    return &slot->var;
}

static void basic_string_destroy(BasicString *value);

static void release_variable_storage(Variable *variable) {
    if (variable->s_value) basic_string_destroy(variable->s_value);
    if (variable->s_array) {
        for (int i = 0; i < variable->array_size; i++) basic_string_destroy(variable->s_array[i]);
        free(variable->s_array);
    }
    free(variable->array);
    variable->s_value = NULL;
    variable->s_array = NULL;
    variable->array = NULL;
}

static void clear_static_locals(void) {
    for (int i = 0; i < static_local_count; i++) release_variable_storage(&static_locals[i].var);
    static_local_count = 0;
    static_name_count = 0;
}

static void note_resolved_variable(CallFrame *frame, int idx) {
    if (frame->resolved_touched_count < (int)(sizeof(frame->resolved_touched) / sizeof(frame->resolved_touched[0]))) {
        frame->resolved_touched[frame->resolved_touched_count] = idx;
    }
    frame->resolved_touched_count++;
}

static void clear_resolved_variables(CallFrame *frame) {
    int capacity = (int)(sizeof(frame->resolved_touched) / sizeof(frame->resolved_touched[0]));
    if (frame->resolved_touched_count > capacity) {
        memset(frame->resolved_variables, 0, sizeof(frame->resolved_variables));
    } else {
        for (int i = 0; i < frame->resolved_touched_count; i++) {
            frame->resolved_variables[frame->resolved_touched[i]] = NULL;
        }
    }
    frame->resolved_touched_count = 0;
}

static Variable *get_variable_ptr_for_frame(int idx, int frame_index) {
    if (idx < 0 || idx >= MAX_VARIABLES) return &vars[0];
    if (frame_index < 0) return &vars[idx];
    if (frame_index >= call_stack_depth) return &vars[idx];

    CallFrame *frame = &call_stack[frame_index];
    if (frame->resolved_variables[idx]) return frame->resolved_variables[idx];
    note_resolved_variable(frame, idx);
    Variable **resolved = &frame->resolved_variables[idx];
    const char *name = vars[idx].name;

    for (int p = 0; p < frame->proc->param_count; p++) {
        ProcParamDef *parameter = &frame->proc->params[p];
        if (proc_name_match(parameter->name, name)) {
            if (frame->byref_caller_variable[p]) {
                return *resolved = frame->byref_caller_variable[p];
            }
            int caller_index = frame->byref_caller_var_idx[p];
            if (caller_index >= 0) {
                return *resolved = get_variable_ptr_for_frame(
                    caller_index, frame->byref_caller_frame[p]);
            }
            for (int l = 0; l < frame->local_var_count; l++) {
                if (proc_name_match((*frame_local(frame, l)).name, name)) {
                    return *resolved = &(*frame_local(frame, l));
                }
            }
            if (frame->local_var_count >= MAX_FRAME_LOCALS) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return &vars[0];
            }
            int local_index = frame->local_var_count++;
            memset(&(*frame_local(frame, local_index)), 0, sizeof(Variable));
            snprintf((*frame_local(frame, local_index)).name,
                     sizeof((*frame_local(frame, local_index)).name), "%s", name);
            return *resolved = &(*frame_local(frame, local_index));
        }

        if (parameter->user_type_index >= 0 && has_record_prefix(name, parameter->name)) {
            int caller_index = frame->byref_caller_var_idx[p];
            if (caller_index >= 0) {
                const char *caller_root = frame->byref_caller_variable[p]
                    ? frame->byref_caller_variable[p]->name : vars[caller_index].name;
                char member_name[128];
                int written = snprintf(member_name, sizeof(member_name), "%s%s",
                                       caller_root, name + strlen(parameter->name));
                if (written < 0 || (size_t)written >= sizeof(member_name)) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return &vars[0];
                }
                int member_index = find_variable_raw_name(member_name);
                return *resolved = get_variable_ptr_for_frame(
                    member_index, frame->byref_caller_frame[p]);
            }
            if (frame->local_var_count >= MAX_FRAME_LOCALS) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return &vars[0];
            }
            int local_index = frame->local_var_count++;
            memset(&(*frame_local(frame, local_index)), 0, sizeof(Variable));
            snprintf((*frame_local(frame, local_index)).name,
                     sizeof((*frame_local(frame, local_index)).name), "%s", name);
            return *resolved = &(*frame_local(frame, local_index));
        }

        if (parameter->user_type_index >= 0 &&
            frame->byref_caller_var_idx[p] >= 0 &&
            frame->byref_caller_variable[p] &&
            has_record_prefix(name, frame->byref_caller_variable[p]->name)) {
            return *resolved = get_variable_ptr_for_frame(
                idx, frame->byref_caller_frame[p]);
        }
    }

    for (int s = 0; s < frame->shared_count; s++) {
        const char *shared = frame->shared_var_names[s];
        // Record fields are named "ROOT.FIELD" or "ROOT@n.FIELD", without the
        // type suffix the SHARED name was given.
        size_t root_length = numeric_base_length(shared, strlen(shared));
        if (proc_name_match(shared, name) ||
            (strncasecmp(name, shared, root_length) == 0 &&
             (name[root_length] == '.' || name[root_length] == '@'))) {
            return *resolved = &vars[idx];
        }
    }
    if (global_shared_count > 0 && global_shared_match(name)) {
        return *resolved = &vars[idx];
    }

    if (frame->proc->is_function && proc_name_match(frame->proc->name, name)) {
        for (int l = 0; l < frame->local_var_count; l++) {
            if (proc_name_match((*frame_local(frame, l)).name, name)) {
                return *resolved = &(*frame_local(frame, l));
            }
        }
        if (frame->local_var_count >= MAX_FRAME_LOCALS) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return &vars[0];
        }
        int local_index = frame->local_var_count++;
        memset(&(*frame_local(frame, local_index)), 0, sizeof(Variable));
        snprintf((*frame_local(frame, local_index)).name,
                 sizeof((*frame_local(frame, local_index)).name), "%s", name);
        return *resolved = &(*frame_local(frame, local_index));
    }

    if (is_static_local(frame->proc, name)) {
        return *resolved = static_local_variable(frame->proc, name);
    }

    for (int l = 0; l < frame->local_var_count; l++) {
        if (strcasecmp((*frame_local(frame, l)).name, name) == 0) {
            return *resolved = &(*frame_local(frame, l));
        }
    }
    if (frame->local_var_count >= MAX_FRAME_LOCALS) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return &vars[0];
    }
    int local_index = frame->local_var_count++;
    memset(&(*frame_local(frame, local_index)), 0, sizeof(Variable));
    snprintf((*frame_local(frame, local_index)).name,
             sizeof((*frame_local(frame, local_index)).name), "%s", name);
    return *resolved = &(*frame_local(frame, local_index));
}

static Variable *get_variable_ptr(int idx) {
    if (idx < 0 || idx >= MAX_VARIABLES) return &vars[0];
    if (call_stack_depth == 0) return &vars[idx];
    return get_variable_ptr_for_frame(idx, call_stack_depth - 1);
}

static void reset_variable_hash_table(void) {
    for (int i = 0; i < VAR_HASH_CAPACITY; i++) var_hash_table[i] = -1;
    var_hash_initialized = 1;
}

void set_args(int argc, char **argv) {
    // Initialize hash table
    reset_variable_hash_table();
    if (qb64_start_time.tv_sec == 0) clock_gettime(CLOCK_MONOTONIC, &qb64_start_time);
    if (!qb64_start_dir[0] && !getcwd(qb64_start_dir, sizeof(qb64_start_dir))) qb64_start_dir[0] = '\0';
    internal_argc = argc;
    internal_argv = argv;
    internal_command_line[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) strncat(internal_command_line, " ", sizeof(internal_command_line) - 1);
        strncat(internal_command_line, argv[i], sizeof(internal_command_line) - 1);
    }
}

int find_variable(const char *name);
static int is_string_var(const char *name);
static int is_string_identifier_tok(Token *token);
static int parse_array_index(const char **input, int var_idx);
static int parse_array_index_tok(TokenStream *ts, int var_idx);
static void set_string_variable(int idx, int array_idx, const char *value);
static int resolve_token_variable(Token *token);
static Num evaluate_num_tok(TokenStream *ts);
static inline int evaluate_num_tok_cached(TokenStream *ts, Num *result);
static Num evaluate_num_text(const char **input);
static int round_to_int(double value);
static int evaluate_int_text(const char **input);
static int evaluate_int_tok(TokenStream *ts);
static int parse_string_expression_tok_heap(TokenStream *ts, BasicString *out);
static const char *skip_whitespace_fast(const char *p);
void interpret_line_at_ptr(const char **ptr_addr, int is_direct, int *last_line_num);
static void interpret_statement_text(Statement *stmt, const char **ptr);
double evaluate_expression(const char **input);
double evaluate_expression_tok(TokenStream *ts);
static int evaluate_function_call_string(ProcedureDef *proc, TokenStream *ts, BasicString *out);
static int evaluate_function_call_string_text(ProcedureDef *proc, const char **input, BasicString *out);

// Helper to skip tokens until a matching end token is found
// Returns the statement *after* the end token, and the token position within that statement.
// If not found, returns NULL for statement.
static Statement* skip_to_matching_token(Statement *start_stmt, int start_ts_pos, TokenType start_type, TokenType end_type, int *out_ts_pos) {
    Statement *current_stmt = start_stmt;
    int current_ts_pos = start_ts_pos;
    int nest_depth = 1;

    while (nest_depth > 0 && current_stmt) {
        while (current_ts_pos < current_stmt->token_count) {
            Token t = current_stmt->tokens[current_ts_pos++];
            if (t.type == start_type) nest_depth++;
            else if (t.type == end_type) nest_depth--;
            if (nest_depth == 0) {
                // Found the matching end token. Now find the statement *after* it.
                if (current_ts_pos < current_stmt->token_count) {
                    // There are more tokens on this line after the end token
                    *out_ts_pos = current_ts_pos;
                    return current_stmt;
                } else {
                    // End token was the last on this line, move to next statement
                    current_stmt = current_stmt->next;
                    *out_ts_pos = 0; // Start from beginning of next statement
                    return current_stmt;
                }
            }
        }
        // Reached end of current statement, move to next
        current_stmt = current_stmt->next;
        if (current_stmt) {
            current_ts_pos = 0; // Start from beginning of next statement
        }
    }
    return NULL; // Matching end token not found
}

// Skips a FOR loop's body to the NEXT that closes it. Each name in a NEXT
// closes one loop (NEXT j, i closes two), and a NEXT without one closes one.
// Returns the statement and token position just after the closing name, which
// may be a comma before further names, or NULL if there is no such NEXT.
static Statement* skip_for_block(Statement *start_stmt, int start_ts_pos, int *out_ts_pos) {
    Statement *current_stmt = start_stmt;
    int current_ts_pos = start_ts_pos;
    int nest_depth = 1;

    while (nest_depth > 0 && current_stmt) {
        while (current_ts_pos < current_stmt->token_count) {
            Token t = current_stmt->tokens[current_ts_pos++];
            if (t.type == TOKEN_EXIT) {
                // The FOR in "EXIT FOR" does not start a nested loop.
                if (current_ts_pos < current_stmt->token_count && current_stmt->tokens[current_ts_pos].type == TOKEN_FOR) {
                    current_ts_pos++;
                }
                continue;
            }
            if (t.type == TOKEN_FOR) {
                nest_depth++;
            } else if (t.type == TOKEN_NEXT) {
                if (current_ts_pos < current_stmt->token_count && current_stmt->tokens[current_ts_pos].type == TOKEN_IDENTIFIER) {
                    while (1) {
                        nest_depth--;
                        current_ts_pos++; // the name
                        if (nest_depth == 0 || current_ts_pos + 1 >= current_stmt->token_count ||
                            current_stmt->tokens[current_ts_pos].type != TOKEN_COMMA ||
                            current_stmt->tokens[current_ts_pos + 1].type != TOKEN_IDENTIFIER) break;
                        current_ts_pos++; // the comma
                    }
                } else {
                    nest_depth--; // NEXT without variable matches innermost FOR
                }
            }
            if (nest_depth == 0) {
                if (current_ts_pos < current_stmt->token_count) {
                    *out_ts_pos = current_ts_pos;
                    return current_stmt;
                } else {
                    current_stmt = current_stmt->next;
                    *out_ts_pos = 0;
                    return current_stmt;
                }
            }
        }
        current_stmt = current_stmt->next;
        if (current_stmt) {
            current_ts_pos = 0;
        }
    }
    return NULL;
}

// Helper to skip tokens until a matching LOOP token for a DO block is found.
// Behaves like skip_to_matching_token(TOKEN_DO, TOKEN_LOOP), except it ignores
// the DO keyword that follows EXIT (i.e. "EXIT DO"), which must not be treated
// as the start of a nested DO block.
static Statement* skip_do_block(Statement *start_stmt, int start_ts_pos, int *out_ts_pos) {
    Statement *current_stmt = start_stmt;
    int current_ts_pos = start_ts_pos;
    int nest_depth = 1;

    while (nest_depth > 0 && current_stmt) {
        while (current_ts_pos < current_stmt->token_count) {
            Token t = current_stmt->tokens[current_ts_pos++];
            if (t.type == TOKEN_EXIT) {
                if (current_ts_pos < current_stmt->token_count && current_stmt->tokens[current_ts_pos].type == TOKEN_DO) {
                    current_ts_pos++; // Skip the DO in "EXIT DO"; not a nested loop start
                }
                continue;
            }
            if (t.type == TOKEN_DO) nest_depth++;
            else if (t.type == TOKEN_LOOP) nest_depth--;
            if (nest_depth == 0) {
                // Skip a LOOP WHILE / LOOP UNTIL condition: execution resumes
                // after the whole LOOP statement.
                if (current_ts_pos < current_stmt->token_count &&
                    (current_stmt->tokens[current_ts_pos].type == TOKEN_WHILE ||
                     current_stmt->tokens[current_ts_pos].type == TOKEN_UNTIL)) {
                    while (current_ts_pos < current_stmt->token_count &&
                           current_stmt->tokens[current_ts_pos].type != TOKEN_COLON) {
                        current_ts_pos++;
                    }
                }
                if (current_ts_pos < current_stmt->token_count) {
                    *out_ts_pos = current_ts_pos;
                    return current_stmt;
                } else {
                    current_stmt = current_stmt->next;
                    *out_ts_pos = 0;
                    return current_stmt;
                }
            }
        }
        current_stmt = current_stmt->next;
        if (current_stmt) {
            current_ts_pos = 0;
        }
    }
    return NULL;
}

/* The name an OPTION _EXPLICIT check found undeclared, for its message. */
static char undefined_variable_name[64];

static const char* get_error_message(RuntimeError code) {
    static char variable_message[96];
    switch (code) {
        case ERR_VARIABLE_NOT_DEFINED:
            snprintf(variable_message, sizeof(variable_message), "Variable not defined: %s", undefined_variable_name);
            return variable_message;
        case ERR_NEXT_WITHOUT_FOR: return "NEXT without FOR";
        case ERR_SYNTAX_ERROR: return "Syntax error";
        case ERR_RETURN_WITHOUT_GOSUB: return "RETURN without GOSUB";
        case ERR_OUT_OF_DATA: return "Out of DATA";
        case ERR_ILLEGAL_FUNCTION_CALL: return "Illegal function call";
        case ERR_OVERFLOW: return "Overflow";
        case ERR_OUT_OF_MEMORY: return "Out of memory";
        case ERR_UNDEFINED_LINE_NUMBER: return "Undefined line number";
        case ERR_SUBSCRIPT_OUT_OF_RANGE: return "Subscript out of range";
        case ERR_DUPLICATE_DEFINITION: return "Duplicate Definition";
        case ERR_DIVISION_BY_ZERO: return "Division by zero";
        case ERR_TYPE_MISMATCH: return "Type mismatch";
        case ERR_OUT_OF_STRING_SPACE: return "Out of string space";
        case ERR_CANT_CONTINUE: return "Can't continue";
        case ERR_CANT_RESUME: return "Can't RESUME";
        case ERR_RESUME_WITHOUT_ERROR: return "RESUME without error";
        case ERR_FOR_WITHOUT_NEXT: return "FOR without NEXT";
        case ERR_WHILE_WITHOUT_WEND: return "WHILE without WEND";
        case ERR_WEND_WITHOUT_WHILE: return "WEND without WHILE";
        case ERR_DO_WITHOUT_LOOP: return "DO without LOOP";
        case ERR_LOOP_WITHOUT_DO: return "LOOP without DO";
        case ERR_FIELD_OVERFLOW: return "FIELD overflow";
        case ERR_INTERNAL_ERROR: return "Internal error";
        case ERR_BAD_FILE_NUMBER: return "Bad file number";
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_BAD_FILE_MODE: return "Bad file mode";
        case ERR_FILE_ALREADY_OPEN: return "File already open";
        case ERR_DEVICE_IO_ERROR: return "Device I/O error";
        case ERR_FILE_ALREADY_EXISTS: return "File already exists";
        case ERR_BAD_RECORD_LENGTH: return "Bad record length";
        case ERR_DISK_FULL: return "Disk full";
        case ERR_INPUT_PAST_END: return "Input past end";
        case ERR_BAD_RECORD_NUMBER: return "Bad record number";
        case ERR_BAD_FILE_NAME: return "Bad file name";
        case ERR_TOO_MANY_FILES: return "Too many files";
        case ERR_PERMISSION_DENIED: return "Permission Denied";
        case ERR_DISK_NOT_READY: return "Disk not ready";
        case ERR_PATH_FILE_ACCESS_ERROR: return "Path/File access error";
        case ERR_PATH_NOT_FOUND: return "Path not found";
        default: return "Unprintable error";
    }
}

static void report_runtime_error(RuntimeError code) {
    const char *msg = get_error_message(code);
    last_runtime_error_code = (int)code;
    last_runtime_error_line = current_executing_line;
    
    if (msg) {
        strncpy(last_runtime_error_msg, msg, sizeof(last_runtime_error_msg) - 1);
        last_runtime_error_msg[sizeof(last_runtime_error_msg) - 1] = '\0';
    } else {
        last_runtime_error_msg[0] = '\0';
    }
    
    if (on_error_goto_line == 0) {
        char full_msg[300];
        if (msg) {
            int display_line = current_has_explicit_line_number || current_source_line_number <= 0
                ? current_executing_line
                : current_source_line_number;
            if (current_executing_line == DIRECT_LINE_NUMBER) {
                snprintf(full_msg, sizeof(full_msg), "%s\n", msg);
            } else {
                snprintf(full_msg, sizeof(full_msg), "%s in %d\n", msg, display_line);
            }
            basic_output(full_msg);
            // In graphics mode the message is drawn in the window, which may be
            // hidden (--headless) or closed a moment later; echo it to stderr.
            if (graphics_is_active()) {
                fputs(full_msg, stderr);
                fflush(stderr);
            }
        }
    }
    runtime_error_occurred = 1;
    stop_running = 1;
}

static double basika_rnd_next(void) {
    // Standard 31-bit LCG for cross-platform consistency.
    // This ensures RND results are the same on Windows, macOS, and Linux.
    rnd_seed = (rnd_seed * 1103515245 + 12345) & 0x7fffffff;
    last_rnd_value = (double)rnd_seed / 2147483648.0;
    return last_rnd_value;
}
static int option_base = 0;
static int option_base_set = 0;
static int arrays_dimensioned = 0;

static Statement *data_stmt = NULL;
static const char *data_ptr = NULL;
static int parse_string_expression(const char **input, char *out, int out_size);
static int parse_string_expression_heap(const char **input, BasicString *out);
static int is_string_var(const char *name);
static int parse_array_index(const char **input, int var_idx);
static int parse_array_index_tok(TokenStream *ts, int var_idx);
static void set_string_variable(int idx, int array_idx, const char *value);
static inline int set_numeric_variable_num(int idx, int array_idx, Num n);
static Num parse_number_text(const char *text);
double evaluate_expression(const char **input);
double evaluate_expression_tok(TokenStream *ts);

static void clear_data_pointer(void) {
    data_stmt = NULL;
    data_ptr = NULL;
}

/* The text after the DATA keyword in stmt's line, or NULL. As in QBasic,
 * DATA may follow other statements on its line (after a colon or ELSE) and
 * runs to the end of the line. */
static const char *data_items_in(Statement *stmt) {
    if (!stmt) return NULL;
    const char *p = stmt->raw_command;
    while (1) {
        Token t = get_next_token(&p);
        if (t.type == TOKEN_DATA) return p;
        if (t.type == TOKEN_EOF || t.type == TOKEN_REM) return NULL;
    }
}

static int is_data_statement(Statement *stmt) {
    return data_items_in(stmt) != NULL;
}

static Statement *find_next_data_statement(Statement *start) {
    Statement *s = start;
    while (s) {
        if (is_data_statement(s)) return s;
        s = s->next;
    }
    return NULL;
}

static void reset_data_pointer(void) {
    Statement *head = get_head();
    data_stmt = find_next_data_statement(head);
    if (data_stmt) {
        data_ptr = data_items_in(data_stmt);
    } else {
        data_ptr = NULL;
    }
}

static int advance_data_statement(void) {
    if (!data_stmt) return 0;
    data_stmt = find_next_data_statement(data_stmt->next);
    if (!data_stmt) {
        data_ptr = NULL;
        return 0;
    }
    data_ptr = data_items_in(data_stmt);
    return 1;
}

static int get_stdin_char(void) {
    fd_set fds;
    struct timeval tv = {0, 0};
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ready <= 0) return 0;
    int c = getchar();
    if (c == EOF || c == '\r' || c == '\n') return 0;
    return c;
}

static void restore_data_to_statement(Statement *stmt);

static void restore_data_to_line(int line) {
    if (line <= 0) {
        reset_data_pointer();
        return;
    }
    restore_data_to_statement(find_line(line));
}

/* Point READ at the first DATA item at or after stmt. */
static void restore_data_to_statement(Statement *stmt) {
    if (!stmt) {
        reset_data_pointer();
        return;
    }
    data_stmt = stmt;
    data_ptr = data_items_in(data_stmt);
    if (!data_ptr) {
        if (!advance_data_statement()) {
            clear_data_pointer();
        }
    }
}

static int get_next_data_item(char *out, int out_size) {
    while (1) {
        if (!data_stmt) reset_data_pointer();
        if (!data_stmt) return 0;
        while (isspace((unsigned char)*data_ptr)) data_ptr++;
        if (*data_ptr == '\0') {
            if (!advance_data_statement()) return 0;
            continue;
        }
        if (*data_ptr == '"') {
            parse_string_expression(&data_ptr, out, out_size);
        } else {
            // Unquoted items are taken as written, as in QBasic, so numbers
            // keep every digit and words are strings.
            int length = 0;
            while (*data_ptr && *data_ptr != ',' && *data_ptr != ':') {
                if (length < out_size - 1) out[length++] = *data_ptr;
                data_ptr++;
            }
            while (length > 0 && isspace((unsigned char)out[length - 1])) length--;
            out[length] = '\0';
        }
        while (isspace((unsigned char)*data_ptr)) data_ptr++;
        if (*data_ptr == ',') {
            data_ptr++;
        } else if (*data_ptr == ':') {
            data_ptr++;
            advance_data_statement();
        }
        return 1;
    }
}

static int parse_read_variable(const char **input, int *var_idx, int *array_idx, int *is_string) {
    Token var = get_next_token(input);
    if (var.type != TOKEN_IDENTIFIER) return 0;
    *is_string = is_string_var(var.text);
    *var_idx = find_variable(var.text);
    *array_idx = parse_array_index(input, *var_idx);
    return 1;
}

static int read_next_data_into_variable(const char **input) {
    int var_idx, array_idx, is_string;
    if (!parse_read_variable(input, &var_idx, &array_idx, &is_string)) return 0;
    char item[256] = "";
    if (!get_next_data_item(item, sizeof(item))) {
        report_runtime_error(ERR_OUT_OF_DATA);
        return 0;
    }
    if (is_string) {
        set_string_variable(var_idx, array_idx, item);
    } else {
        set_numeric_variable_num(var_idx, array_idx, parse_number_text(item));
    }
    return 1;
}

/* ---- Numeric values -------------------------------------------------------
 * Expressions produce a Num: an exact 64-bit integer for the integer kinds or
 * a double for SINGLE and DOUBLE. Arithmetic follows QBasic: the result has
 * the larger operand kind, and an INTEGER, LONG or _INTEGER64 result that
 * does not fit is an Overflow error. _UNSIGNED _INTEGER64 arithmetic wraps,
 * like every _UNSIGNED type does on assignment. */

static inline Num num_i(int64_t value, int kind) {
    Num n;
    n.i = value;
    n.kind = kind;
    return n;
}

static inline Num num_f(double value, int kind) {
    Num n;
    n.d = value;
    n.kind = kind;
    return n;
}

static inline Num num_d(double value) { return num_f(value, NUM_DOUBLE); }

/* A TOKEN_NUMBER literal; tokens built without a kind are DOUBLE. */
static inline Num num_from_token(const Token *token) {
    if (token->num_kind && token->num_kind < NUM_SINGLE) return num_i(token->int64_val, token->num_kind);
    return num_f(token->double_val, token->num_kind ? token->num_kind : NUM_DOUBLE);
}

static inline int num_is_int(Num n) { return n.kind < NUM_SINGLE; }

static inline double num_to_double(Num n) {
    if (n.kind >= NUM_SINGLE) return n.d;
    if (n.kind == NUM_UINT64) return (double)(uint64_t)n.i;
    return (double)n.i;
}

static inline __int128 num_int128(Num n) {
    return n.kind == NUM_UINT64 ? (__int128)(uint64_t)n.i : (__int128)n.i;
}

/* The kind of a float result: DOUBLE when either side is DOUBLE or wider
 * than INTEGER, so 1& / 3 keeps LONG's precision. */
static inline int num_float_kind(Num a, Num b) {
    return (a.kind == NUM_SINGLE || a.kind == NUM_INTEGER) &&
           (b.kind == NUM_SINGLE || b.kind == NUM_INTEGER) ? NUM_SINGLE : NUM_DOUBLE;
}

static __attribute__((noinline)) Num num_overflow(int kind) {
    report_runtime_error(ERR_OVERFLOW);
    return num_i(0, kind);
}

static __attribute__((noinline)) double num_float_overflow(void) {
    report_runtime_error(ERR_OVERFLOW);
    return 0;
}

/* A floating-point result too large for a DOUBLE is an Overflow, as in
 * QBasic, rather than an infinity. */
static inline __attribute__((always_inline)) double num_checked_float(double value) {
    return __builtin_expect(isinf(value), 0) ? num_float_overflow() : value;
}

/* An INTEGER or LONG result, checked against its range. */
static inline __attribute__((always_inline)) Num num_small_result(int64_t value, int kind) {
    if (kind == NUM_INTEGER ? (value < -32768 || value > 32767)
                            : (value < INT32_MIN || value > INT32_MAX)) {
        return num_overflow(kind);
    }
    return num_i(value, kind);
}

#define NUM_HOT static inline __attribute__((always_inline))
#define NUM_COLD static __attribute__((noinline))

/* +, - and * on the wide kinds; the inline versions below handle floats and
 * INTEGER/LONG directly. */
NUM_COLD Num num_arith_slow(int op, Num a, Num b) {
    int kind = a.kind > b.kind ? a.kind : b.kind;
    if (kind <= NUM_LONG) {
        int64_t r = op == '+' ? a.i + b.i : op == '-' ? a.i - b.i : a.i * b.i;
        return num_small_result(r, kind);
    }
    // _INTEGER64 wraps around like QB64's C++ int64_t, so the usual 64-bit
    // random number generators (seed * 25214903917 + 11) work.
    if (kind == NUM_INT64 || kind == NUM_UINT64) {
        uint64_t x = (uint64_t)a.i, y = (uint64_t)b.i;
        return num_i((int64_t)(op == '+' ? x + y : op == '-' ? x - y : x * y), kind);
    }
    double x = num_to_double(a), y = num_to_double(b);
    return num_f(num_checked_float(op == '+' ? x + y : op == '-' ? x - y : x * y), kind);
}

NUM_HOT Num num_add(Num a, Num b) {
    int kind = a.kind > b.kind ? a.kind : b.kind;
    if (kind >= NUM_SINGLE) return num_f(num_checked_float(num_to_double(a) + num_to_double(b)), kind);
    if (kind <= NUM_LONG) return num_small_result(a.i + b.i, kind);
    return num_arith_slow('+', a, b);
}

NUM_HOT Num num_subtract(Num a, Num b) {
    int kind = a.kind > b.kind ? a.kind : b.kind;
    if (kind >= NUM_SINGLE) return num_f(num_checked_float(num_to_double(a) - num_to_double(b)), kind);
    if (kind <= NUM_LONG) return num_small_result(a.i - b.i, kind);
    return num_arith_slow('-', a, b);
}

NUM_HOT Num num_multiply(Num a, Num b) {
    int kind = a.kind > b.kind ? a.kind : b.kind;
    if (kind >= NUM_SINGLE) return num_f(num_checked_float(num_to_double(a) * num_to_double(b)), kind);
    if (kind <= NUM_LONG) return num_small_result(a.i * b.i, kind);
    return num_arith_slow('*', a, b);
}

static Num num_negate(Num a) {
    if (a.kind <= NUM_LONG) return num_small_result(-a.i, a.kind);
    if (a.kind == NUM_INT64 || a.kind == NUM_UINT64) return num_i((int64_t)(0 - (uint64_t)a.i), a.kind);
    return num_f(-a.d, a.kind);
}

/* Division as in QBasic: dividing by zero is an error, and / always gives
 * a floating-point result. */
static Num num_divide(Num a, Num b) {
    double right = num_to_double(b);
    if (right == 0.0) {
        report_runtime_error(ERR_DIVISION_BY_ZERO);
        return num_i(0, NUM_INTEGER);
    }
    return num_f(num_checked_float(num_to_double(a) / right), num_float_kind(a, b));
}

/* x ^ y: a negative base needs a whole exponent and 0 ^ -n divides by
 * zero, as in QBasic. */
static Num num_power(Num a, Num b) {
    double base = num_to_double(a), exponent = num_to_double(b);
    if (base < 0 && exponent != floor(exponent)) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return num_i(0, NUM_INTEGER);
    }
    if (base == 0 && exponent < 0) {
        report_runtime_error(ERR_DIVISION_BY_ZERO);
        return num_i(0, NUM_INTEGER);
    }
    return num_f(num_checked_float(pow(base, exponent)), num_float_kind(a, b));
}

/* \, MOD and the logical operators work on whole numbers: an integer stays as
 * it is and a float is rounded half to even. Returns the operand's kind, or 0
 * (after reporting Overflow) when a float is out of _INTEGER64 range. */
static int num_whole_operand(Num n, int64_t *out) {
    if (n.kind < NUM_SINGLE) {
        *out = n.i;
        return n.kind;
    }
    double r = nearbyint(n.d);
    if (!(r >= -9223372036854775808.0 && r < 9223372036854775808.0)) {
        report_runtime_error(ERR_OVERFLOW);
        return 0;
    }
    *out = (int64_t)r;
    return (*out >= INT32_MIN && *out <= INT32_MAX) ? NUM_LONG : NUM_INT64;
}

static __attribute__((noinline)) Num num_integer_divide_slow(Num a, Num b, int want_remainder) {
    int64_t x, y;
    int ka = num_whole_operand(a, &x);
    int kb = num_whole_operand(b, &y);
    if (!ka || !kb) return num_i(0, NUM_INTEGER);
    int kind = ka > kb ? ka : kb;
    if (y == 0) {
        report_runtime_error(ERR_DIVISION_BY_ZERO);
        return num_i(0, kind);
    }
    if (kind == NUM_UINT64) {
        uint64_t ux = (uint64_t)x, uy = (uint64_t)y;
        return num_i((int64_t)(want_remainder ? ux % uy : ux / uy), kind);
    }
    if (x == INT64_MIN && y == -1) return want_remainder ? num_i(0, kind) : num_overflow(kind);
    int64_t r = want_remainder ? x % y : x / y;
    return kind <= NUM_LONG ? num_small_result(r, kind) : num_i(r, kind);
}

/* \ and MOD: INTEGER/LONG operands, and floats that already hold a small
 * whole number, take the inline path. */
static inline __attribute__((always_inline)) int num_small_whole(Num n, int64_t *out, int *kind) {
    if (n.kind <= NUM_LONG) {
        *out = n.i;
        *kind = n.kind;
        return 1;
    }
    if (n.kind >= NUM_SINGLE && n.d >= -2147483648.0 && n.d <= 2147483647.0) {
        int64_t whole = (int64_t)n.d;
        if ((double)whole != n.d) return 0;
        *out = whole;
        *kind = NUM_LONG;
        return 1;
    }
    return 0;
}

static inline __attribute__((always_inline)) Num num_integer_divide(Num a, Num b, int want_remainder) {
    int64_t x, y;
    int ka, kb;
    if (num_small_whole(a, &x, &ka) && num_small_whole(b, &y, &kb) && y != 0) {
        int kind = ka > kb ? ka : kb;
        return num_small_result(want_remainder ? x % y : x / y, kind);
    }
    return num_integer_divide_slow(a, b, want_remainder);
}

/* Compares an exact integer with a double without rounding the integer. */
static __attribute__((noinline)) int compare_integer_double(__int128 x, double d) {
    if (d != d) return 0;
    if (d >= 18446744073709551616.0) return -1;
    if (d < -9223372036854775808.0) return 1;
    double whole = floor(d);
    __int128 y = (__int128)whole;
    if (x != y) return x < y ? -1 : 1;
    return whole < d ? -1 : 0;
}

/* Returns <0, 0 or >0. Integers compare exactly, also against floats. */
static inline __attribute__((always_inline)) int num_compare(Num a, Num b) {
    if (a.kind >= NUM_SINGLE && b.kind >= NUM_SINGLE) return (a.d > b.d) - (a.d < b.d);
    if (a.kind < NUM_SINGLE && b.kind < NUM_SINGLE) {
        if (a.kind != NUM_UINT64 && b.kind != NUM_UINT64) return (a.i > b.i) - (a.i < b.i);
        __int128 x = num_int128(a), y = num_int128(b);
        return (x > y) - (x < y);
    }
    if (a.kind < NUM_SINGLE) return compare_integer_double(num_int128(a), b.d);
    return -compare_integer_double(num_int128(b), a.d);
}

static inline Num num_truth(int condition) { return num_i(condition ? -1 : 0, NUM_INTEGER); }

/* AND, OR, XOR, EQV, IMP (op '&', '|', '^', '=', '>') and NOT (op '~', b
 * ignored). */
static Num num_logical(int op, Num a, Num b) {
    int64_t x, y = 0;
    int ka = num_whole_operand(a, &x);
    int kb = op == '~' ? ka : num_whole_operand(b, &y);
    if (!ka || !kb) return num_i(0, NUM_INTEGER);
    int kind = ka > kb ? ka : kb;
    switch (op) {
        case '&': return num_i(x & y, kind);
        case '|': return num_i(x | y, kind);
        case '^': return num_i(x ^ y, kind);
        case '=': return num_i(~(x ^ y), kind);
        case '>': return num_i(~x | y, kind);
        default: return num_i(~x, kind);
    }
}

/* Variable types, in the order of primitive_types[] (VarType = index + 1).
 * VT_UNTYPED is an internal name without a suffix. */
enum {
    VT_STRING = 1, VT_BYTE, VT_UBYTE, VT_INTEGER, VT_UINTEGER, VT_LONG, VT_ULONG,
    VT_INT64, VT_UINT64, VT_SINGLE, VT_DOUBLE, VT_UNTYPED
};

/* The expression kind a variable of each VarType reads as. */
static const unsigned char var_type_kind[] = {
    [VT_STRING] = 0, [VT_BYTE] = NUM_LONG, [VT_UBYTE] = NUM_LONG,
    [VT_INTEGER] = NUM_INTEGER, [VT_UINTEGER] = NUM_LONG, [VT_LONG] = NUM_LONG,
    [VT_ULONG] = NUM_INT64, [VT_INT64] = NUM_INT64, [VT_UINT64] = NUM_UINT64,
    [VT_SINGLE] = NUM_SINGLE, [VT_DOUBLE] = NUM_DOUBLE, [VT_UNTYPED] = NUM_SINGLE
};

static inline int var_type_is_integer(int vt) { return vt >= VT_BYTE && vt <= VT_UINT64; }

static int var_type_of_name(const char *name) {
    const PrimitiveType *type = primitive_type_of_name(name);
    if (!type) return VT_UNTYPED;
    return (int)(type - primitive_types) + 1;
}

static inline int var_type(Variable *v) {
    if (!v->var_type) v->var_type = (unsigned char)var_type_of_name(v->name);
    return v->var_type;
}

/* LONG, _INTEGER64 and DOUBLE variables (names ending in & or #) make PRINT
 * show 16 significant digits. */
static const unsigned char var_type_wide[] = {
    [VT_LONG] = 1, [VT_ULONG] = 1, [VT_INT64] = 1, [VT_UINT64] = 1, [VT_DOUBLE] = 1, [VT_UNTYPED] = 0
};

static inline int var_is_wide(Variable *v) { return var_type_wide[var_type(v)]; }

/* Reads a numeric variable or array element (array_idx < 0 for a scalar). */
static inline __attribute__((always_inline)) Num load_num(Variable *v, int array_idx) {
    int vt = var_type(v);
    int in_array = array_idx >= 0;
    if (in_array && (!v->array || array_idx >= v->array_size)) {
        return var_type_is_integer(vt) ? num_i(0, var_type_kind[vt]) : num_f(0, var_type_kind[vt]);
    }
    if (var_type_is_integer(vt)) {
        return num_i(in_array ? v->iarray[array_idx] : v->ivalue, var_type_kind[vt]);
    }
    return num_f(in_array ? v->array[array_idx] : v->value, var_type_kind[vt]);
}

static inline double load_double(Variable *v, int array_idx) {
    return num_to_double(load_num(v, array_idx));
}

/* Converts n to integer VarType vt: floats round half to even (2.5 -> 2,
 * 3.5 -> 4) like QBasic, _UNSIGNED types wrap around like QB64, and a value
 * outside a signed type's range is an Overflow error (returns 0). */
static int num_to_integer_type(Num n, int vt, int64_t *out) {
    const PrimitiveType *type = &primitive_types[vt - 1];
    int bits = type->bytes * 8;
    if (n.kind >= NUM_SINGLE) {
        double r = nearbyint(n.d);
        if (type->is_unsigned) {
            double modulus = ldexp(1.0, bits);
            r = fmod(r, modulus);
            if (r < 0) r += modulus;
            if (!(r >= 0)) r = 0; // NaN
            *out = (int64_t)(uint64_t)r;
            return 1;
        }
        if (!(r >= type->min_value && r <= type->max_value) ||
            (bits == 64 && r >= 9223372036854775808.0)) {
            report_runtime_error(ERR_OVERFLOW);
            return 0;
        }
        *out = (int64_t)r;
        return 1;
    }
    if (type->is_unsigned) {
        uint64_t value = (uint64_t)n.i;
        if (bits < 64) value &= (UINT64_C(1) << bits) - 1;
        *out = (int64_t)value;
        return 1;
    }
    if (n.kind == NUM_UINT64 && n.i < 0) {
        report_runtime_error(ERR_OVERFLOW);
        return 0;
    }
    // As in QB64, a whole number up to 2^32 - 1 stored in a LONG keeps its bit
    // pattern, so 32-bit colors (POINT, _RGB32, _UNSIGNED LONG values) fit:
    // &HFFFFFFFF becomes -1. Floating-point values still Overflow.
    if (bits == 32 && n.i >= (INT64_C(1) << 31) && n.i < (INT64_C(1) << 32)) {
        *out = n.i - (INT64_C(1) << 32);
        return 1;
    }
    if (bits < 64) {
        int64_t limit = INT64_C(1) << (bits - 1);
        if (n.i < -limit || n.i >= limit) {
            report_runtime_error(ERR_OVERFLOW);
            return 0;
        }
    }
    *out = n.i;
    return 1;
}

/* Converts value to integer VarType vt as an expression result (CINT, CLNG). */
static Num num_convert_integer(Num value, int vt) {
    int64_t converted;
    if (!num_to_integer_type(value, vt, &converted)) return num_i(0, var_type_kind[vt]);
    return num_i(converted, var_type_kind[vt]);
}

/* Converts n to the variable's type and stores it. Returns 0 on Overflow. */
static inline __attribute__((always_inline)) int store_num(Variable *v, int array_idx, Num n) {
    int vt = var_type(v);
    int in_array = array_idx >= 0;
    if (in_array && (!v->array || array_idx >= v->array_size)) return 1;
    if (var_type_is_integer(vt)) {
        int64_t value;
        // A value of the variable's own kind always fits.
        if (n.kind == var_type_kind[vt] && (vt == VT_INTEGER || vt == VT_LONG || vt == VT_INT64 ||
                                            vt == VT_UINT64)) {
            value = n.i;
        } else if (!num_to_integer_type(n, vt, &value)) {
            return 0;
        }
        if (in_array) v->iarray[array_idx] = value;
        else v->ivalue = value;
        return 1;
    }
    double value = num_to_double(n);
    if (vt == VT_SINGLE) {
        value = (double)(float)value;
        // Beyond SINGLE's range (about 3.4E+38) is an Overflow.
        if (__builtin_expect(isinf(value), 0) && !isinf(num_to_double(n))) {
            report_runtime_error(ERR_OVERFLOW);
            return 0;
        }
    }
    if (in_array) v->array[array_idx] = value;
    else v->value = value;
    return 1;
}

/* Stores a value without type conversion (graphics GET arrays hold pixel
 * colors in whatever numeric array they are given). */
static void store_double_raw(Variable *v, int array_idx, double value) {
    int in_array = array_idx >= 0;
    if (in_array && (!v->array || array_idx >= v->array_size)) return;
    if (var_type_is_integer(var_type(v))) {
        int64_t whole = (value >= -9223372036854775808.0 && value < 9223372036854775808.0)
            ? (int64_t)value : 0;
        if (in_array) v->iarray[array_idx] = whole;
        else v->ivalue = whole;
    } else if (in_array) {
        v->array[array_idx] = value;
    } else {
        v->value = value;
    }
}

static inline __attribute__((always_inline)) int set_numeric_variable_num(int idx, int array_idx, Num n) {
    return store_num(get_variable_ptr(idx), array_idx, n);
}


/* A FUNCTION or DEF FN whose name has an integer suffix returns that type
 * even when the result was assigned through an unsuffixed reference. */
static Num num_as_name_type(Num value, const char *name) {
    int vt = var_type_of_name(name);
    if (!var_type_is_integer(vt)) return value;
    Variable typed = {0};
    typed.var_type = (unsigned char)vt;
    return store_num(&typed, -1, value) ? load_num(&typed, -1) : num_i(0, var_type_kind[vt]);
}

/* Parses a number typed or read as text (INPUT, READ, VAL): whole numbers
 * are exact integers, anything else a DOUBLE. */
static Num parse_number_text(const char *text) {
    const char *p = text;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] == '&') {
        // &H, &O and &B literals, typed as in program text.
        const char *literal = p;
        Token token = get_next_token(&literal);
        if (token.type == TOKEN_NUMBER) return num_from_token(&token);
    }
    const char *digits = p;
    if (*digits == '+' || *digits == '-') digits++;
    if (isdigit((unsigned char)*digits)) {
        const char *end = digits;
        while (isdigit((unsigned char)*end)) end++;
        const char *rest = end;
        while (*rest == ' ' || *rest == '\t' || *rest == '%' || *rest == '&') rest++;
        if (*rest == '\0' || *rest == ',') {
            errno = 0;
            char *parsed_end = NULL;
            if (*p == '-') {
                long long value = strtoll(p, &parsed_end, 10);
                if (errno != ERANGE) return num_i(value, NUM_INT64);
            } else {
                unsigned long long value = strtoull(p, &parsed_end, 10);
                if (errno != ERANGE) {
                    return value > INT64_MAX ? num_i((int64_t)value, NUM_UINT64)
                                             : num_i((int64_t)value, NUM_INT64);
                }
            }
        }
    }
    return num_d(atof(text));
}

/* Formats an exact integer value. */
static void format_num_integer(Num n, char *out, size_t size) {
    if (n.kind == NUM_UINT64) snprintf(out, size, "%llu", (unsigned long long)(uint64_t)n.i);
    else snprintf(out, size, "%lld", (long long)n.i);
}

static void randomize_seed(const char **input) {
    const char *saved = *input;
    Token next = get_next_token(input);
    unsigned seed;
    if (next.type == TOKEN_TIMER) {
        seed = (unsigned)clock();
    } else if (next.type == TOKEN_NUMBER) {
        seed = (unsigned)next.int_val;
    } else if (next.type == TOKEN_EOF || next.type == TOKEN_COLON) {
        *input = saved;
        seed = (unsigned)time(NULL) ^ (unsigned)clock();
    } else {
        *input = saved;
        seed = (unsigned)evaluate_expression(input);
    }
    rnd_seed = seed;
}

static void parse_def_range(const char **ptr, char type_char) {
    int changed = 0;
    while (1) {
        Token t = get_next_token(ptr);
        if (t.type != TOKEN_IDENTIFIER) break;
        
        char start = toupper((unsigned char)t.text[0]);
        char end = start;
        
        const char *saved = *ptr;
        Token dash = get_next_token(ptr);
        if (dash.type == TOKEN_MINUS) {
            Token t2 = get_next_token(ptr);
            if (t2.type == TOKEN_IDENTIFIER) {
                end = toupper((unsigned char)t2.text[0]);
            } else {
                *ptr = saved;
            }
        } else {
            *ptr = saved;
        }
        
        for (char c = start; c <= end; c++) {
            if (c >= 'A' && c <= 'Z' && default_type_map[c - 'A'] != type_char) {
                default_type_map[c - 'A'] = type_char;
                changed = 1;
            }
        }
        
        saved = *ptr;
        if (get_next_token(ptr).type != TOKEN_COMMA) {
            *ptr = saved;
            break;
        }
    }
    if (changed) {
        default_type_generation++;
        if (default_type_generation == 0) default_type_generation = 1;
        invalidate_expression_name_cache();
    }
}

static int parse_read_list(const char **input) {
    if (!read_next_data_into_variable(input)) return 0;
    while (1) {
        const char *saved = *input;
        Token sep = get_next_token(input);
        if (sep.type == TOKEN_COMMA) {
            if (!read_next_data_into_variable(input)) return 0;
            continue;
        }
        *input = saved;
        break;
    }
    return 1;
}

static int parse_restore(const char **input) {
    const char *saved = *input;
    Token arg = get_next_token(input);
    if (arg.type == TOKEN_NUMBER) {
        restore_data_to_line(arg.int_val);
        return 1;
    }
    if (arg.type == TOKEN_IDENTIFIER) {
        /* RESTORE label (QBasic) */
        Statement *stmt = find_label(arg.text);
        if (!stmt) {
            report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
            return 0;
        }
        restore_data_to_statement(stmt);
        return 1;
    }
    *input = saved;
    reset_data_pointer();
    return 1;
}

static int parse_randomize(const char **input) {
    randomize_seed(input);
    return 1;
}

static unsigned int hash_name(const char *name);
static int find_variable_index(const char *normalized);

/* Name is a FUNCTION Name$ written without its "$" (as QB64 allows). */
static int is_string_function_without_suffix(const char *name) {
    static unsigned int checked_generation = 0;
    static int checked_count = -1, any_string_function = 0;
    if (checked_generation != proc_generation || checked_count != proc_count) {
        any_string_function = 0;
        for (int p = 0; p < proc_count && !any_string_function; p++) {
            size_t len = strlen(registered_procs[p].name);
            any_string_function = registered_procs[p].is_function && len > 0 &&
                registered_procs[p].name[len - 1] == '$';
        }
        checked_generation = proc_generation;
        checked_count = proc_count;
    }
    if (!any_string_function || strchr(name, '.')) return 0;
    ProcedureDef *proc = find_procedure(name);
    if (!proc || !proc->is_function) return 0;
    size_t len = strlen(proc->name);
    return proc->name[len - 1] == '$';
}

static int is_string_var(const char *name) {
    int len = (int)strlen(name);
    if (len == 0) return 0;
    char last = name[len - 1];
    if (last == '$') return 1;
    if (strchr(name, '.') && last != '%' && last != '!' && last != '#' && last != '&') {
        // Variable names are stored uppercase.
        char string_member[64];
        int written = snprintf(string_member, sizeof(string_member), "%s$", name);
        for (int i = 0; string_member[i]; i++) string_member[i] = (char)toupper((unsigned char)string_member[i]);
        if (written > 0 && (size_t)written < sizeof(string_member) &&
            find_variable_index(string_member) >= 0) return 1;
    }
    if (fixed_string_declarations_present && last != '%' && last != '!' && last != '#' && last != '&') {
        // A fixed-length string is stored under the name with the letter's
        // default suffix (S! normally, S& after DEFLNG).
        char normalized[64];
        int letter = toupper((unsigned char)name[0]);
        char default_suffix = letter >= 'A' && letter <= 'Z' ? default_type_map[letter - 'A'] : '!';
        snprintf(normalized, sizeof(normalized), "%s%c", name, default_suffix ? default_suffix : '!');
        for (int i = 0; normalized[i]; i++) normalized[i] = (char)toupper((unsigned char)normalized[i]);
        int idx = find_variable_index(normalized);
        if (idx >= 0 && vars[idx].string_declared) {
            return 1;
        }
    }
    if (last == '%' || last == '!' || last == '#' || last == '&') return 0;
    if (declared_type_count > 0 && !strchr(name, '.')) {
        const char *declared = find_declared_suffix(name);
        if (declared) return declared[0] == '$';
    }
    if (is_string_function_without_suffix(name)) return 1;
    int first = (unsigned char)name[0];
    if (first >= 'a' && first <= 'z') first -= 'a' - 'A'; // ASCII only; toupper is a libc call
    if (first >= 'A' && first <= 'Z') {
        return (default_type_map[first - 'A'] == '$');
    }
    return 0;
}

/* ERR and ERL are read specially rather than as variables. A three-character
 * compare: strcasecmp shows up in profiles of array-heavy loops. */
static int name_is_err_or_erl(const char *name) {
    char c0 = (char)(name[0] | 0x20), c1 = (char)(name[1] | 0x20), c2 = (char)(name[2] | 0x20);
    return c0 == 'e' && c1 == 'r' && (c2 == 'r' || c2 == 'l') && name[3] == '\0';
}

/* The last character of a name, or 0 when it is empty: the suffix tests of
 * the array paths without a strlen call. */
static char name_last_char(const char *name) {
    const char *last = name;
    while (last[1]) last++;
    return *last;
}

// Forward declaration for is_string_token (used in parse_string_expression_tok)
static int is_string_token(const Token *t);

typedef struct BasicStringPoolSlot {
    BasicString value;
    struct BasicStringPoolSlot *next;
} BasicStringPoolSlot;

#define BASIC_STRING_POOL_SIZE 2048
static BasicStringPoolSlot *basic_string_pool_slots = NULL;
static BasicStringPoolSlot *basic_string_pool_free_list = NULL;

static void basic_string_pool_init(void) {
    if (basic_string_pool_slots) return;

    basic_string_pool_slots = calloc(BASIC_STRING_POOL_SIZE, sizeof(*basic_string_pool_slots));
    if (!basic_string_pool_slots) return;

    for (int i = 0; i < BASIC_STRING_POOL_SIZE; i++) {
        basic_string_pool_slots[i].next = (i + 1 < BASIC_STRING_POOL_SIZE)
            ? &basic_string_pool_slots[i + 1]
            : NULL;
    }
    basic_string_pool_free_list = &basic_string_pool_slots[0];
}

static BasicString *basic_string_pool_alloc(void) {
    if (!basic_string_pool_slots) basic_string_pool_init();
    /* Once the pool is in use, fall back to the heap; release frees these. */
    if (!basic_string_pool_free_list) return calloc(1, sizeof(BasicString));

    BasicStringPoolSlot *slot = basic_string_pool_free_list;
    basic_string_pool_free_list = slot->next;
    memset(&slot->value, 0, sizeof(slot->value));
    return &slot->value;
}

static void basic_string_pool_release(BasicString *value) {
    if (!value) return;
    if (!basic_string_pool_slots) {
        free(value);
        return;
    }

    BasicStringPoolSlot *slot = (BasicStringPoolSlot *)((char *)value - offsetof(BasicStringPoolSlot, value));
    if (slot < basic_string_pool_slots || slot >= basic_string_pool_slots + BASIC_STRING_POOL_SIZE) {
        free(value);
        return;
    }

    memset(&slot->value, 0, sizeof(slot->value));
    slot->next = basic_string_pool_free_list;
    basic_string_pool_free_list = slot;
}

static BasicString *basic_string_create(const void *data, size_t length) {
    BasicString *value = basic_string_pool_alloc();
    if (!value) return NULL;
    value->capacity = length + 1;
    value->data = malloc(value->capacity);
    if (!value->data) {
        basic_string_pool_release(value);
        return NULL;
    }
    if (data && length) memcpy(value->data, data, length);
    value->data[length] = '\0';
    value->length = length;
    return value;
}

static int basic_string_reserve(BasicString *value, size_t required) {
    if (!value) return 0;
    if (required <= value->capacity) return 1;
    size_t capacity = value->capacity ? value->capacity : 16;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    unsigned char *data = realloc(value->data, capacity);
    if (!data) return 0;
    value->data = data;
    value->capacity = capacity;
    return 1;
}

int basic_string_assign(BasicString **target, const void *data, size_t length) {
    if (!target) return 0;
    if (!*target) {
        *target = basic_string_create(NULL, 0);
        if (!*target) return 0;
    }
    BasicString *value = *target;
    size_t assigned_length = value->is_fixed ? value->fixed_length : length;
    if (!basic_string_reserve(value, assigned_length + 1)) return 0;
    size_t copy_length = data && length < assigned_length ? length : assigned_length;
    if (data && copy_length) memcpy(value->data, data, copy_length);
    if (assigned_length > copy_length) memset(value->data + copy_length, ' ', assigned_length - copy_length);
    value->data[assigned_length] = '\0';
    value->length = assigned_length;
    return 1;
}

/* Transfers value's heap buffer to *target without copying the payload.
 * value must be a temporary that nobody else references; on success it is
 * left empty so a later release of it becomes a no-op. Fixed-length
 * targets still take a copying assignment so truncation and padding
 * survive. */
static int basic_string_move_assign(BasicString **target, BasicString *value) {
    if (!target || !value) return 0;
    if (!*target) {
        *target = basic_string_create(NULL, 0);
        if (!*target) return 0;
    }
    if (!value->data || (*target)->is_fixed || (*target)->data == value->data) {
        return basic_string_assign(target, value->data, value->length);
    }
    free((*target)->data);
    (*target)->data = value->data;
    (*target)->length = value->length;
    (*target)->capacity = value->capacity;
    value->data = NULL;
    value->length = 0;
    value->capacity = 0;
    return 1;
}

static int assign_string_variable_value(int idx, int array_idx, const void *data, size_t length) {
    Variable *v = get_variable_ptr(idx);
    BasicString **target = array_idx >= 0 ? &v->s_array[array_idx] : &v->s_value;
    if (v->string_declared) {
        if (!*target) *target = basic_string_create(NULL, 0);
        if (!*target) return 0;
        (*target)->is_fixed = 1;
        (*target)->fixed_length = v->string_fixed_length;
    }
    return basic_string_assign(target, data, length);
}

/* Assigns a parsed string temporary to a variable, adopting the value's
 * buffer when the variable has dynamic length so large assignments do not
 * copy the payload a second time. */
static int assign_string_variable_move(int idx, int array_idx, BasicString *value) {
    Variable *v = get_variable_ptr(idx);
    if (v->string_declared) {
        return assign_string_variable_value(idx, array_idx, value->data, value->length);
    }
    BasicString **target = array_idx >= 0 ? &v->s_array[array_idx] : &v->s_value;
    return basic_string_move_assign(target, value);
}

int basic_string_append(BasicString *target, const void *data, size_t length) {
    if (!target || (!data && length) || !basic_string_reserve(target, target->length + length + 1)) return 0;
    if (length) memcpy(target->data + target->length, data, length);
    target->length += length;
    target->data[target->length] = '\0';
    return 1;
}

static int basic_string_append_repeat(BasicString *target, int character, size_t count) {
    if (!target || !basic_string_reserve(target, target->length + count + 1)) return 0;
    if (count) memset(target->data + target->length, character & 0xFF, count);
    target->length += count;
    target->data[target->length] = '\0';
    return 1;
}

static int append_string_variable_value(BasicString *target, int idx, int array_idx) {
    if (idx < 0 || idx >= var_count) return 0;
    int fnum = 0;
    FieldBinding binding;
    if (get_field_binding_for_var(idx, array_idx, &fnum, &binding)) {
        FileFieldState *state = &file_field_state[fnum];
        if (state->buffer && binding.offset >= 0 && binding.offset < state->size) {
            int length = binding.len;
            if (binding.offset + length > state->size) length = state->size - binding.offset;
            if (length > 0) {
                return basic_string_append(target, state->buffer + binding.offset, (size_t)length);
            }
        }
        return 1;
    }
    Variable *v = get_variable_ptr(idx);
    BasicString *value = array_idx >= 0 && v->s_array && array_idx < v->array_size
        ? v->s_array[array_idx] : v->s_value;
    if (!value) return 1;
    return basic_string_append(target, value->data, value->length);
}

static int parse_dynamic_string_expression(const char **input, BasicString *out) {
    int parsed = 0;
    while (1) {
        const char *saved = *input;
        Token token = get_next_token(input);
        int term_parsed = 0;
        if (token.type == TOKEN_STRING) {
            term_parsed = basic_string_append(out, token.text, strlen(token.text));
        } else if (token.type == TOKEN_IDENTIFIER && is_string_var(token.text)) {
            int idx = find_variable(token.text);
            int array_idx = parse_array_index(input, idx);
            term_parsed = append_string_variable_value(out, idx, array_idx);
        } else if (token.type == TOKEN_COMMANDS) {
            term_parsed = basic_string_append(out, internal_command_line, strlen(internal_command_line));
        } else if (token.type == TOKEN_ARGVS) {
            Token open = get_next_token(input);
            int index = evaluate_int_text(input);
            Token close = get_next_token(input);
            if (open.type == TOKEN_LPAREN && close.type == TOKEN_RPAREN &&
                index >= 0 && index < internal_argc && internal_argv) {
                term_parsed = basic_string_append(out, internal_argv[index], strlen(internal_argv[index]));
            } else {
                *input = saved;
            }
        } else if (token.type == TOKEN_STRING_FUNC) {
            Token open = get_next_token(input);
            if (open.type != TOKEN_LPAREN) { *input = saved; break; }
            int count = evaluate_int_text(input);
            if (get_next_token(input).type != TOKEN_COMMA) { *input = saved; break; }
            Token value_token = get_next_token(input);
            int character;
            if (value_token.type == TOKEN_NUMBER) character = (int)value_token.double_val;
            else if (value_token.type == TOKEN_STRING && value_token.text[0]) character = (unsigned char)value_token.text[0];
            else { *input = saved; break; }
            if (get_next_token(input).type != TOKEN_RPAREN || count < 0) { *input = saved; break; }
            term_parsed = basic_string_append_repeat(out, character, (size_t)count);
        }
        if (!term_parsed) {
            *input = saved;
            break;
        }
        parsed = 1;
        const char *separator = *input;
        if (get_next_token(input).type != TOKEN_PLUS) {
            *input = separator;
            break;
        }
    }
    return parsed;
}

static void basic_string_destroy(BasicString *value) {
    if (!value) return;
    if (value->data) {
        free(value->data);
        value->data = NULL;
    }
    basic_string_pool_release(value);
}

/* HEX$ and OCT$: negative values print in two's complement at the width of
 * their type (INTEGER 16 bits, LONG 32 bits, _INTEGER64 64 bits), as in
 * QBasic; floats are rounded first. */
static void format_radix_string(Num value, int hex, char *buf, size_t size) {
    uint64_t bits;
    int kind = value.kind;
    if (!num_is_int(value)) {
        int64_t whole;
        kind = num_whole_operand(value, &whole);
        if (!kind) {
            buf[0] = '\0';
            return;
        }
        if (whole >= -32768 && whole < 0) kind = NUM_INTEGER;
        value = num_i(whole, kind);
    }
    if (value.i < 0 && kind == NUM_INTEGER) bits = (uint16_t)value.i;
    else if (value.i < 0 && kind == NUM_LONG) bits = (uint32_t)value.i;
    else bits = (uint64_t)value.i;
    snprintf(buf, size, hex ? "%llX" : "%llo", (unsigned long long)bits);
}

/* STR$: the number as PRINT shows it, with a leading space when positive. */
static void format_str_function(Num value, char *buf, size_t size);

/* TIMER: wall-clock seconds since local midnight, as in QBasic. */
static double seconds_since_midnight(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    time_t seconds = now.tv_sec;
    struct tm local;
    localtime_r(&seconds, &local);
    return local.tm_hour * 3600.0 + local.tm_min * 60.0 + local.tm_sec + now.tv_usec / 1000000.0;
}

/* A popped frame keeps its locals until the slot is reused, because FUNCTION
 * results are read from it after the call returns. Release them before the
 * slot is cleared so string and array storage is not leaked on every call. */
static void release_call_frame_storage(CallFrame *frame) {
    for (int l = 0; l < frame->local_var_count; l++) release_variable_storage(&(*frame_local(frame, l)));
    frame->local_var_count = 0;
}

/* Prepares a call frame slot for a new call without clearing the large
 * local-variable and lookup-cache arrays wholesale. */
static void reset_call_frame(CallFrame *frame) {
    release_call_frame_storage(frame);
    clear_resolved_variables(frame);
    frame->proc = NULL;
    frame->return_stmt = NULL;
    frame->return_token_idx = 0;
    memset(frame->byref_caller_variable, 0, sizeof(frame->byref_caller_variable));
    memset(frame->byref_caller_var_idx, 0, sizeof(frame->byref_caller_var_idx));
    memset(frame->byref_caller_frame, 0, sizeof(frame->byref_caller_frame));
    frame->shared_count = 0;
    frame->func_return_numeric = 0;
    frame->func_return_string = NULL;
}



static int read_input_chars(int count, int fnum, BasicString *out);

static void basic_string_release(BasicString *value) {
    if (!value) return;
    free(value->data);
    value->data = NULL;
    value->length = 0;
    value->capacity = 0;
}

static int basic_string_deflate(const BasicString *input, BasicString *output) {
    if (!input || !output) return 0;
    uLong compressed_capacity = compressBound((uLong)input->length);
    Bytef *compressed = malloc(compressed_capacity);
    if (!compressed) return 0;
    uLong compressed_len = compressed_capacity;
    int result = compress2(compressed, &compressed_len, input->data,
                           (uLong)input->length, Z_DEFAULT_COMPRESSION);
    if (result != Z_OK || compressed_len > (SIZE_MAX - 1) / 2) {
        free(compressed);
        return 0;
    }
    if (!basic_string_reserve(output, (size_t)compressed_len * 2 + 1)) {
        free(compressed);
        return 0;
    }
    static const char hex[] = "0123456789ABCDEF";
    for (uLong i = 0; i < compressed_len; i++) {
        output->data[i * 2] = hex[compressed[i] >> 4];
        output->data[i * 2 + 1] = hex[compressed[i] & 15];
    }
    output->length = (size_t)compressed_len * 2;
    output->data[output->length] = '\0';
    free(compressed);
    return 1;
}

static int basic_string_inflate(const BasicString *input, BasicString *output) {
    if (!input || !output || input->length == 0 || (input->length % 2) != 0) return 0;
    size_t encoded_len = input->length;
    Bytef *compressed = malloc(encoded_len / 2);
    if (!compressed) return 0;
    for (size_t i = 0; i < encoded_len; i += 2) {
        unsigned int byte = 0;
        if (sscanf((const char *)input->data + i, "%2x", &byte) != 1) {
            free(compressed);
            return 0;
        }
        compressed[i / 2] = (Bytef)byte;
    }
    size_t capacity = encoded_len > 1 ? encoded_len : 16;
    while (1) {
        if (!basic_string_reserve(output, capacity + 1)) {
            free(compressed);
            return 0;
        }
        uLong decompressed_len = (uLong)capacity;
        int result = uncompress(output->data, &decompressed_len, compressed,
                                (uLong)(encoded_len / 2));
        if (result == Z_OK) {
            output->length = (size_t)decompressed_len;
            output->data[output->length] = '\0';
            free(compressed);
            return 1;
        }
        if (result != Z_BUF_ERROR || capacity > SIZE_MAX / 2) {
            free(compressed);
            return 0;
        }
        capacity *= 2;
    }
}

static unsigned int hash_name(const char *name) {
    unsigned int hash = 5381;
    int c;
    while ((c = *name++)) hash = ((hash << 5) + hash) + (unsigned int)c;
    return hash & VAR_HASH_MASK;
}

/* Returns -1 at the first empty slot, since variable entries are never deleted. */
static int find_variable_index(const char *normalized) {
    if (!var_hash_initialized) reset_variable_hash_table();
    unsigned int slot = hash_name(normalized);
    for (int probe = 0; probe < VAR_HASH_CAPACITY; probe++) {
        int idx = var_hash_table[slot];
        if (idx == -1) return -1;
        if (strcmp(vars[idx].name, normalized) == 0) return idx;
        slot = (slot + 1) & VAR_HASH_MASK;
    }
    return -1;
}

static int add_named_constant(const char *name, Num value, int is_wide) {
    if (!name || !name[0]) return 0;
    char normalized[64];
    int len = 0;
    for (int i = 0; name[i] && len < 63; i++) {
        char c = (char)toupper((unsigned char)name[i]);
        if (is_type_suffix_char(c) || c == '~') break;
        normalized[len++] = c;
    }
    normalized[len] = '\0';
    if (len == 0) return 0;
    for (int i = 0; i < named_constant_count; i++) {
        if (strcasecmp(named_constants[i].name, normalized) == 0) {
            named_constants[i].value = value;
            named_constants[i].is_wide = is_wide;
            return 1;
        }
    }
    if (named_constant_count >= MAX_NAMED_CONSTANTS) return 0;
    if (++constant_generation == 0) constant_generation = 1;
    invalidate_expression_name_cache();
    int idx = named_constant_count++;
    strncpy(named_constants[idx].name, normalized, sizeof(named_constants[idx].name) - 1);
    named_constants[idx].name[sizeof(named_constants[idx].name) - 1] = '\0';
    named_constants[idx].value = value;
    named_constants[idx].is_wide = is_wide;
    return 1;
}

static int find_named_constant_slow(const char *name) {
    if (!name || !name[0]) return -1;
    char normalized[64];
    int len = 0;
    for (int i = 0; name[i] && len < 63; i++) {
        char c = (char)toupper((unsigned char)name[i]);
        normalized[len++] = c;
    }
    if (len > 0) {
        char last = normalized[len - 1];
        if (last == '$') len--;
        else len = (int)numeric_base_length(normalized, (size_t)len);
    }
    normalized[len] = '\0';
    for (int i = 0; i < named_constant_count; i++) {
        if (strcasecmp(named_constants[i].name, normalized) == 0) return i;
    }
    return -1;
}

/* Constants are rare, so keep the common no-constant check inlined. */
static inline int find_named_constant(const char *name) {
    return named_constant_count == 0 ? -1 : find_named_constant_slow(name);
}

/* find_named_constant for a program token: the result is kept on the token,
 * since every variable read in an expression asks whether it is a CONST. */
static inline int find_named_constant_tok(Token *token) {
    if (named_constant_count == 0) return -1;
    if (token->const_generation != constant_generation) {
        token->const_cache = find_named_constant_slow(token->text);
        token->const_generation = constant_generation;
    }
    return token->const_cache;
}

/* Called at every variable reference; the cache-hit path is kept inline. */
static inline __attribute__((always_inline)) int resolve_token_variable(Token *token) {
    if (!token || token->type != TOKEN_IDENTIFIER) return -1;

    if (token->var_idx != -1 && token->type_generation == default_type_generation) {
        return token->var_idx;
    }

    token->var_idx = find_variable(token->text);
    token->type_generation = default_type_generation;
    return token->var_idx;
}

/* FOR counter AS type = start TO ...: declares the counter's type (the
 * FreeBASIC and QB64 form) and leaves ts at the "=". Returns 0 on an error. */
static int for_counter_type_tok(TokenStream *ts, Token *counter, int token_count) {
    if (ts->pos >= token_count || ts->tokens[ts->pos].type != TOKEN_AS) return 1;
    ts->pos++;
    if (ts->pos >= token_count || ts->tokens[ts->pos].type != TOKEN_IDENTIFIER) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    const char *type_name = ts->tokens[ts->pos].text;
    const char *combined = ts->pos + 1 < token_count
        ? unsigned_type_name(type_name, ts->tokens[ts->pos + 1].text) : NULL;
    if (combined) {
        type_name = combined;
        ts->pos++;
    }
    ts->pos++;
    const char *suffix = primitive_type_suffix(type_name);
    size_t length = strlen(counter->text);
    if (!suffix || suffix[0] == '$' || (length > 0 && is_type_suffix_char(counter->text[length - 1]))) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    if (!declare_variable_type(counter->text, suffix, current_declaration_scope(), 0)) {
        report_runtime_error(ERR_DUPLICATE_DEFINITION);
        return 0;
    }
    return 1;
}

static void insert_variable_index(const char *normalized, int idx) {
    if (!var_hash_initialized) reset_variable_hash_table();
    unsigned int slot = hash_name(normalized);
    for (int probe = 0; probe < VAR_HASH_CAPACITY; probe++) {
        if (var_hash_table[slot] == -1) {
            var_hash_table[slot] = idx;
            return;
        }
        slot = (slot + 1) & VAR_HASH_MASK;
    }
}

int find_variable(const char *name) {
    char normalized[128];
    int i;
    for (i = 0; i < 127 && name[i]; i++) {
        normalized[i] = (char)toupper((unsigned char)name[i]);
    }
    normalized[i] = '\0';
    int len = i;

    if (len > 0 && strchr(normalized, '.') && !is_type_suffix_char(normalized[len - 1])) {
        static const char *const suffixes[] = {"$", "%", "!", "#", "&", "~&"};
        for (size_t suffix_index = 0; suffix_index < sizeof(suffixes) / sizeof(suffixes[0]); suffix_index++) {
            char member_name[64];
            int written = snprintf(member_name, sizeof(member_name), "%s%s", normalized, suffixes[suffix_index]);
            if (written > 0 && (size_t)written < sizeof(member_name)) {
                int member_idx = find_variable_index(member_name);
                if (member_idx >= 0) return member_idx;
            }
        }
    }

    if (len > 0 && strchr(normalized, '.') == NULL) {
        char last = normalized[len - 1];
        if (!is_type_suffix_char(last)) {
            const char *declared = declared_type_count > 0 ? find_declared_suffix(normalized) : NULL;
            if (declared) {
                if (len + strlen(declared) < sizeof(normalized)) strcat(normalized, declared);
            } else if (normalized[0] >= 'A' && normalized[0] <= 'Z') {
                char suffix = default_type_map[normalized[0] - 'A'];
                if (suffix != '\0') {
                    normalized[len] = suffix;
                    normalized[len+1] = '\0';
                }
            }
        }
    }

    int idx = find_variable_index(normalized);
    if (idx != -1) return idx;

    if (var_count < MAX_VARIABLES) {
        idx = var_count++;
        memset(&vars[idx], 0, sizeof(Variable));
        strncpy(vars[idx].name, normalized, sizeof(vars[idx].name) - 1);
        vars[idx].name[sizeof(vars[idx].name) - 1] = '\0';
        insert_variable_index(normalized, idx);
        return idx;
    }
    return -1;
}

static int find_variable_raw_name(const char *name) {
    char normalized[128];
    size_t length = 0;
    while (length < sizeof(normalized) - 1 && name[length]) {
        normalized[length] = (char)toupper((unsigned char)name[length]);
        length++;
    }
    normalized[length] = '\0';
    int idx = find_variable_index(normalized);
    if (idx >= 0) return idx;
    if (var_count >= MAX_VARIABLES) return -1;
    idx = var_count++;
    memset(&vars[idx], 0, sizeof(vars[idx]));
    snprintf(vars[idx].name, sizeof(vars[idx].name), "%s", normalized);
    insert_variable_index(normalized, idx);
    return idx;
}

static int find_existing_variable_raw_name(const char *name) {
    char normalized[128];
    size_t length = 0;
    while (length < sizeof(normalized) - 1 && name[length]) {
        normalized[length] = (char)toupper((unsigned char)name[length]);
        length++;
    }
    normalized[length] = '\0';
    return find_variable_index(normalized);
}

static UserType *find_user_type(const char *name) {
    for (int i = 0; i < user_type_count; i++) {
        if (strcasecmp(user_types[i].name, name) == 0) return &user_types[i];
    }
    return NULL;
}

static int is_primitive_user_type(const char *name) {
    return primitive_type_suffix(name) != NULL;
}

static void report_type_declaration_error(Statement *stmt) {
    if (stmt) {
        current_executing_line = stmt->line_number;
        current_source_line_number = stmt->source_line_number;
        current_has_explicit_line_number = stmt->has_explicit_line_number;
    }
    report_runtime_error(ERR_SYNTAX_ERROR);
}

static int validate_user_type_graph(int type_index, unsigned char states[MAX_USER_TYPES]) {
    if (states[type_index] == 1) return 0;
    if (states[type_index] == 2) return 1;
    states[type_index] = 1;
    UserType *type = &user_types[type_index];
    for (int field_index = 0; field_index < type->field_count; field_index++) {
        UserType *nested = find_user_type(type->fields[field_index].type_name);
        if (!nested) continue;
        int nested_index = (int)(nested - user_types);
        if (!validate_user_type_graph(nested_index, states)) return 0;
    }
    states[type_index] = 2;
    return 1;
}

/* The tokens of a TYPE block line before a trailing ' or REM comment. */
static int type_line_token_count(const Statement *stmt) {
    int count = stmt->token_count;
    if (count > 0 && (stmt->tokens[count - 1].type == TOKEN_APOSTROPHE ||
                      stmt->tokens[count - 1].type == TOKEN_REM)) {
        count--;
    }
    return count;
}

static int scan_user_types(void) {
    user_type_count = 0;
    user_type_instance_count = 0;
    invalidate_expression_name_cache();
    for (Statement *stmt = get_head(); stmt; stmt = stmt->next) {
        if (stmt->token_count == 0 || stmt->tokens[0].type != TOKEN_TYPE) continue;
        if (stmt->token_count < 2 || stmt->tokens[1].type != TOKEN_IDENTIFIER ||
            type_line_token_count(stmt) > 2 || user_type_count >= MAX_USER_TYPES) {
            report_type_declaration_error(stmt);
            return 0;
        }
        if (find_user_type(stmt->tokens[1].text)) {
            report_type_declaration_error(stmt);
            return 0;
        }
        UserType *type = &user_types[user_type_count++];
        memset(type, 0, sizeof(*type));
        strncpy(type->name, stmt->tokens[1].text, sizeof(type->name) - 1);
        type->declaration_stmt = stmt;
    }

    int current_type = -1;
    Statement *type_start = NULL;
    for (Statement *stmt = get_head(); stmt; stmt = stmt->next) {
        if (stmt->token_count >= 2 && stmt->tokens[0].type == TOKEN_TYPE) {
            current_type = (int)(find_user_type(stmt->tokens[1].text) - user_types);
            type_start = stmt;
            continue;
        }
        if (stmt->token_count >= 2 && stmt->tokens[0].type == TOKEN_END &&
            stmt->tokens[1].type == TOKEN_TYPE) {
            if (current_type < 0 || type_line_token_count(stmt) != 2) {
                report_type_declaration_error(stmt);
                return 0;
            }
            current_type = -1;
            type_start = NULL;
            continue;
        }
        if (current_type < 0) continue;

        int field_tokens = type_line_token_count(stmt);
        if (field_tokens == 0) continue; // a comment line
        int position = 0;
        if (field_tokens < 3 || stmt->tokens[position++].type != TOKEN_IDENTIFIER) {
            report_type_declaration_error(stmt);
            return 0;
        }
        Token *field_token = &stmt->tokens[0];
        int upper_bounds[3] = {0};
        int lower_bounds[3] = {-1, -1, -1};
        int dimension_count = 0;
        if (stmt->tokens[position].type == TOKEN_LPAREN) {
            position++;
            while (dimension_count < 3) {
                if (stmt->tokens[position].type != TOKEN_NUMBER) break;
                int first_bound = stmt->tokens[position++].int_val;
                int upper = first_bound;
                if (stmt->tokens[position].type == TOKEN_TO) {
                    lower_bounds[dimension_count] = first_bound;
                    position++;
                    if (stmt->tokens[position].type != TOKEN_NUMBER) break;
                    upper = stmt->tokens[position++].int_val;
                }
                if (upper < first_bound || upper > 100000) break;
                upper_bounds[dimension_count++] = upper;
                if (stmt->tokens[position].type != TOKEN_COMMA) break;
                position++;
            }
            if (dimension_count == 0 || stmt->tokens[position++].type != TOKEN_RPAREN) {
                report_type_declaration_error(stmt);
                return 0;
            }
        }
        if (stmt->tokens[position++].type != TOKEN_AS ||
            stmt->tokens[position].type != TOKEN_IDENTIFIER) {
            report_type_declaration_error(stmt);
            return 0;
        }
        Token *field_type = &stmt->tokens[position++];
        const char *field_type_name = field_type->text;
        const char *combined = stmt->tokens[position].type == TOKEN_IDENTIFIER
            ? unsigned_type_name(field_type->text, stmt->tokens[position].text) : NULL;
        if (combined) {
            field_type_name = combined;
            position++;
        }
        size_t fixed_string_length = 0;
        int is_fixed_string = 0;
        if (strcasecmp(field_type->text, "STRING") == 0 &&
            stmt->tokens[position].type == TOKEN_STAR) {
            position++;
            if (stmt->tokens[position].type != TOKEN_NUMBER ||
                stmt->tokens[position].int_val < 0) {
                report_type_declaration_error(stmt);
                return 0;
            }
            fixed_string_length = (size_t)stmt->tokens[position++].int_val;
            is_fixed_string = 1;
        }
        if (position != field_tokens) {
            report_type_declaration_error(stmt);
            return 0;
        }

        UserType *type = &user_types[current_type];
        if (type->field_count >= MAX_TYPE_FIELDS) {
            report_type_declaration_error(stmt);
            return 0;
        }
        for (int i = 0; i < type->field_count; i++) {
            if (strcasecmp(type->fields[i].name, field_token->text) == 0) {
                report_type_declaration_error(stmt);
                return 0;
            }
        }
        TypeField *field = &type->fields[type->field_count++];
        strncpy(field->name, field_token->text, sizeof(field->name) - 1);
        strncpy(field->type_name, field_type_name, sizeof(field->type_name) - 1);
        field->num_dims = dimension_count;
        memcpy(field->dims, upper_bounds, sizeof(upper_bounds));
        memcpy(field->lower_bounds, lower_bounds, sizeof(lower_bounds));
        field->fixed_string_length = fixed_string_length;
        field->is_fixed_string = is_fixed_string;
        field->declaration_stmt = stmt;
    }
    if (current_type >= 0) {
        report_type_declaration_error(type_start);
        return 0;
    }
    for (int i = 0; i < user_type_count; i++) {
        for (int field_index = 0; field_index < user_types[i].field_count; field_index++) {
            TypeField *field = &user_types[i].fields[field_index];
            if (!is_primitive_user_type(field->type_name) && !find_user_type(field->type_name)) {
                report_type_declaration_error(field->declaration_stmt);
                return 0;
            }
        }
    }
    unsigned char states[MAX_USER_TYPES] = {0};
    for (int i = 0; i < user_type_count; i++) {
        if (!validate_user_type_graph(i, states)) {
            report_type_declaration_error(user_types[i].declaration_stmt);
            return 0;
        }
    }
    return 1;
}

static int instantiate_user_type(const char *prefix, UserType *type, int depth) {
    if (!type) return 0;
    if (depth >= MAX_USER_TYPES) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    for (int i = 0; i < type->field_count; i++) {
        TypeField *field = &type->fields[i];
        char member_name[128];
        int written = snprintf(member_name, sizeof(member_name), "%s.%s", prefix, field->name);
        if (written < 0 || (size_t)written >= sizeof(member_name)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }

        UserType *nested = find_user_type(field->type_name);
        if (!nested && !is_primitive_user_type(field->type_name)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }
        int element_count = 1;
        int lower_bounds[3] = {0};
        for (int dim = 0; dim < field->num_dims; dim++) {
            lower_bounds[dim] = field->lower_bounds[dim] < 0
                ? option_base : field->lower_bounds[dim];
            int count = field->dims[dim] - lower_bounds[dim] + 1;
            if (count <= 0 || element_count > 100000 / count) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return 0;
            }
            element_count *= count;
        }
        if (nested) {
            if (field->num_dims > 0) {
                int marker_index = find_variable(member_name);
                Variable *marker = get_variable_ptr(marker_index);
                if (!marker->array) {
                    marker->array = calloc((size_t)element_count, sizeof(*marker->array));
                    if (!marker->array) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        return 0;
                    }
                    marker->array_size = element_count;
                    marker->num_dims = field->num_dims;
                    for (int dim = 0; dim < field->num_dims; dim++) {
                        marker->dims[dim] = field->dims[dim];
                        marker->lower_bounds[dim] = lower_bounds[dim];
                    }
                }
            }
            for (int element = 0; element < element_count; element++) {
                char nested_name[128];
                if (field->num_dims == 0) {
                    snprintf(nested_name, sizeof(nested_name), "%s", member_name);
                } else {
                    snprintf(nested_name, sizeof(nested_name), "%s@%d", member_name, element);
                }
                if (!instantiate_user_type(nested_name, nested, depth + 1)) return 0;
            }
        } else {
            if (!is_primitive_user_type(field->type_name)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return 0;
            }
            char variable_name[128];
            size_t field_len = strlen(field->name);
            char suffix_char = field_len > 0 ? field->name[field_len - 1] : '\0';
            const char *suffix = "";
            if (!is_type_suffix_char(suffix_char)) {
                suffix = primitive_type_suffix(field->type_name);
                if (!suffix) suffix = "";
            }
            written = snprintf(variable_name, sizeof(variable_name), "%s%s", member_name, suffix);
            if (written < 0 || (size_t)written >= sizeof(variable_name)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return 0;
            }
            int idx = find_variable(variable_name);
            Variable *variable = get_variable_ptr(idx);
            if (field->num_dims > 0) {
                if (variable->array || variable->s_array) {
                    report_runtime_error(ERR_DUPLICATE_DEFINITION);
                    return 0;
                }
                if (strcasecmp(field->type_name, "STRING") == 0) {
                    variable->s_array = calloc((size_t)element_count, sizeof(*variable->s_array));
                    if (!variable->s_array) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        return 0;
                    }
                } else {
                    variable->array = calloc((size_t)element_count, sizeof(*variable->array));
                    if (!variable->array) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        return 0;
                    }
                }
                variable->array_size = element_count;
                variable->num_dims = field->num_dims;
                for (int dim = 0; dim < field->num_dims; dim++) {
                    variable->dims[dim] = field->dims[dim];
                    variable->lower_bounds[dim] = lower_bounds[dim];
                }
            }
            if (field->is_fixed_string) {
                variable->string_declared = 1;
                variable->string_fixed_length = field->fixed_string_length;
                fixed_string_declarations_present = 1;
            }
        }
    }
    return 1;
}

/* Record instances DIMmed in a procedure belong to it; a procedure sees its
 * own instance of a name first and otherwise the module-level one. */
static UserTypeInstance *find_user_type_instance_in_scope(int root_variable_index,
                                                          const ProcedureDef *scope) {
    for (int i = 0; i < user_type_instance_count; i++) {
        if (user_type_instances[i].root_variable_index == root_variable_index &&
            user_type_instances[i].scope == scope) {
            return &user_type_instances[i];
        }
    }
    return NULL;
}

static UserTypeInstance *find_user_type_instance_for(int root_variable_index,
                                                     const ProcedureDef *scope) {
    UserTypeInstance *instance = scope ? find_user_type_instance_in_scope(root_variable_index, scope) : NULL;
    return instance ? instance : find_user_type_instance_in_scope(root_variable_index, NULL);
}

static int get_user_type_instance_at_frame(int root_variable_index, int frame_index,
                                           UserTypeInstance *instance_out) {
    if (root_variable_index < 0 || root_variable_index >= var_count) return 0;
    if (frame_index < 0 && user_type_instance_count == 0) return 0;
    if (frame_index >= 0 && frame_index < call_stack_depth) {
        CallFrame *frame = &call_stack[frame_index];
        const char *root_name = vars[root_variable_index].name;
        for (int p = 0; p < frame->proc->param_count; p++) {
            ProcParamDef *parameter = &frame->proc->params[p];
            if (parameter->user_type_index < 0 ||
                !proc_name_match(parameter->name, root_name)) continue;
            memset(instance_out, 0, sizeof(*instance_out));
            instance_out->root_variable_index = root_variable_index;
            instance_out->type_index = parameter->user_type_index;
            instance_out->element_count = 1;
            if (parameter->is_array && frame->byref_caller_var_idx[p] >= 0) {
                UserTypeInstance caller_instance;
                if (get_user_type_instance_at_frame(frame->byref_caller_var_idx[p],
                        frame->byref_caller_frame[p], &caller_instance)) {
                    *instance_out = caller_instance;
                    instance_out->root_variable_index = root_variable_index;
                }
            }
            return 1;
        }
    }
    const ProcedureDef *scope = (frame_index >= 0 && frame_index < call_stack_depth)
        ? call_stack[frame_index].proc : NULL;
    UserTypeInstance *instance = find_user_type_instance_for(root_variable_index, scope);
    if (!instance) return 0;
    *instance_out = *instance;
    return 1;
}

static int get_user_type_instance_for_root(int root_variable_index,
                                           UserTypeInstance *instance_out) {
    return get_user_type_instance_at_frame(root_variable_index,
                                           call_stack_depth - 1, instance_out);
}

static int declare_user_type_instance(int root_variable_index, UserType *type,
                                      int num_dims, const int *upper_bounds,
                                      const int *requested_lower_bounds) {
    if (root_variable_index < 0 || !type || num_dims < 0 || num_dims > 3) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    int type_index = (int)(type - user_types);
    int lower_bounds[3] = {0};
    int element_count = 1;
    for (int dim = 0; dim < num_dims; dim++) {
        lower_bounds[dim] = requested_lower_bounds ? requested_lower_bounds[dim] : option_base;
        int dimension_size = upper_bounds[dim] - lower_bounds[dim] + 1;
        if (dimension_size <= 0 || element_count > 100000 / dimension_size) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return 0;
        }
        element_count *= dimension_size;
    }

    const ProcedureDef *scope = current_declaration_scope();
    UserTypeInstance *instance = find_user_type_instance_in_scope(root_variable_index, scope);
    if (instance && scope) {
        /* A procedure's locals are new on every call, so its DIM may declare
         * different bounds each time; a repeat DIM within one call is still
         * caught below because the local array already exists. */
        instance->type_index = type_index;
        instance->num_dims = num_dims;
        instance->element_count = element_count;
        memset(instance->dims, 0, sizeof(instance->dims));
        if (num_dims > 0) memcpy(instance->dims, upper_bounds, (size_t)num_dims * sizeof(*upper_bounds));
        memcpy(instance->lower_bounds, lower_bounds, sizeof(instance->lower_bounds));
    } else if (instance) {
        if (instance->type_index != type_index || instance->num_dims != num_dims ||
            (num_dims > 0 &&
             memcmp(instance->dims, upper_bounds, (size_t)num_dims * sizeof(*upper_bounds)) != 0)) {
            report_runtime_error(ERR_DUPLICATE_DEFINITION);
            return 0;
        }
    } else {
        if (user_type_instance_count >= MAX_RECORD_INSTANCES) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return 0;
        }
        instance = &user_type_instances[user_type_instance_count++];
        memset(instance, 0, sizeof(*instance));
        instance->root_variable_index = root_variable_index;
        instance->scope = scope;
        instance->type_index = type_index;
        instance->num_dims = num_dims;
        instance->element_count = element_count;
        if (num_dims > 0) memcpy(instance->dims, upper_bounds, sizeof(instance->dims));
        memcpy(instance->lower_bounds, lower_bounds, sizeof(instance->lower_bounds));
    }

    Variable *root = get_variable_ptr(root_variable_index);
    if (num_dims > 0 && !root->array) {
        root->array = calloc((size_t)element_count, sizeof(*root->array));
        if (!root->array) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return 0;
        }
        root->array_size = element_count;
        root->num_dims = num_dims;
        for (int dim = 0; dim < num_dims; dim++) {
            root->dims[dim] = upper_bounds[dim];
            root->lower_bounds[dim] = lower_bounds[dim];
        }
    } else if (num_dims == 0 && (root->array || root->s_array)) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    } else if (num_dims > 0 && (root->array_size != element_count ||
               root->num_dims != num_dims)) {
        report_runtime_error(ERR_DUPLICATE_DEFINITION);
        return 0;
    }

    for (int element = 0; element < element_count; element++) {
        char prefix[128];
        if (num_dims == 0) {
            snprintf(prefix, sizeof(prefix), "%s", root->name);
        } else {
            snprintf(prefix, sizeof(prefix), "%s@%d", root->name, element);
        }
        if (!instantiate_user_type(prefix, type, 0)) return 0;
    }
    return 1;
}

static TypeField *find_user_type_field(UserType *type, const char *name) {
    if (!type) return NULL;
    for (int i = 0; i < type->field_count; i++) {
        if (strcasecmp(type->fields[i].name, name) == 0) return &type->fields[i];
    }
    return NULL;
}

static int append_user_type_member_chunk(const char *chunk, UserType **current_type,
                                        char *prefix, size_t prefix_size,
                                        TokenStream *ts, int *member_index,
                                        int *member_array_index) {
    char components[BASIC_TOKEN_TEXT_MAX];
    snprintf(components, sizeof(components), "%s", chunk);
    char *saveptr = NULL;
    char *component = strtok_r(components, ".", &saveptr);
    while (component) {
        TypeField *field = find_user_type_field(*current_type, component);
        if (!field) {
            report_runtime_error(ERR_TYPE_MISMATCH);
            return 0;
        }
        size_t prefix_length = strlen(prefix);
        int written = snprintf(prefix + prefix_length, prefix_size - prefix_length,
                               ".%s", field->name);
        if (written < 0 || (size_t)written >= prefix_size - prefix_length) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }

        char *next_component = strtok_r(NULL, ".", &saveptr);
        UserType *nested = find_user_type(field->type_name);
        if (nested) {
            if (field->num_dims > 0) {
                int marker_index = find_variable(prefix);
                int array_index = parse_array_index_tok(ts, marker_index);
                if (array_index < 0) {
                    report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
                    return 0;
                }
                prefix_length = strlen(prefix);
                written = snprintf(prefix + prefix_length, prefix_size - prefix_length,
                                   "@%d", array_index);
                if (written < 0 || (size_t)written >= prefix_size - prefix_length) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return 0;
                }
                if (next_component) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return 0;
                }
            }
            *current_type = nested;
            component = next_component;
            continue;
        }

        if (next_component) {
            report_runtime_error(ERR_TYPE_MISMATCH);
            return 0;
        }
        size_t field_name_length = strlen(field->name);
        char last_char = field_name_length > 0 ? field->name[field_name_length - 1] : '\0';
        const char *suffix = NULL;
        if (!is_type_suffix_char(last_char)) suffix = primitive_type_suffix(field->type_name);
        if (suffix) {
            prefix_length = strlen(prefix);
            if (prefix_length + strlen(suffix) >= prefix_size) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return 0;
            }
            strcat(prefix, suffix);
        }
        *member_index = find_variable(prefix);
        *member_array_index = field->num_dims > 0
            ? parse_array_index_tok(ts, *member_index) : -1;
        if (field->num_dims > 0 && *member_array_index < 0) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return 0;
        }
        *current_type = NULL;
        return 1;
    }
    return 1;
}

static int resolve_user_type_reference_tok(TokenStream *ts, const Token *base_token,
                                           int *member_index, int *member_array_index) {
    char root_name[BASIC_TOKEN_TEXT_MAX];
    const char *dot = strchr(base_token->text, '.');
    if (!dot) {
        if (ts->tokens[ts->pos].type != TOKEN_LPAREN) return 0;
        int depth = 0;
        int close_position = ts->pos;
        for (; close_position < ts->statement->token_count; close_position++) {
            if (ts->tokens[close_position].type == TOKEN_LPAREN) depth++;
            else if (ts->tokens[close_position].type == TOKEN_RPAREN && --depth == 0) break;
        }
        if (close_position >= ts->statement->token_count ||
            ts->tokens[close_position + 1].type != TOKEN_DOT) return 0;
    }
    size_t root_length = dot ? (size_t)(dot - base_token->text) : strlen(base_token->text);
    if (root_length == 0 || root_length >= sizeof(root_name)) return 0;
    memcpy(root_name, base_token->text, root_length);
    root_name[root_length] = '\0';
    int root_index = find_variable_raw_name(root_name);
    UserTypeInstance instance_storage;
    if (!get_user_type_instance_for_root(root_index, &instance_storage)) return 0;
    UserTypeInstance *instance = &instance_storage;

    UserType *current_type = &user_types[instance->type_index];
    char prefix[128];
    Variable *root = get_variable_ptr(root_index);
    if (instance->num_dims > 0) {
        int root_array_index = parse_array_index_tok(ts, root_index);
        if (root_array_index < 0) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return -1;
        }
        snprintf(prefix, sizeof(prefix), "%s@%d", root->name, root_array_index);
    } else {
        snprintf(prefix, sizeof(prefix), "%s", root->name);
    }

    if (dot && !append_user_type_member_chunk(dot + 1, &current_type, prefix,
            sizeof(prefix), ts, member_index, member_array_index)) return -1;
    while (ts->tokens[ts->pos].type == TOKEN_DOT) {
        if (!current_type) {
            report_runtime_error(ERR_TYPE_MISMATCH);
            return -1;
        }
        ts->pos++;
        if (ts->tokens[ts->pos].type != TOKEN_IDENTIFIER) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return -1;
        }
        Token *member_token = &ts->tokens[ts->pos++];
        if (!append_user_type_member_chunk(member_token->text, &current_type, prefix,
                sizeof(prefix), ts, member_index, member_array_index)) return -1;
    }
    if (current_type) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return -1;
    }
    return 1;
}

static int resolve_user_type_reference_text(const char **input, const Token *base_token,
                                            int *member_index, int *member_array_index) {
    Token tokens[256];
    Statement statement = {0};
    const char *cursor = *input;
    int count = 0;
    while (count < 255) {
        const char *start = cursor;
        Token token = get_next_token(&cursor);
        if (token.type == TOKEN_EOF) break;
        token.start_ptr = start;
        token.var_idx = -1;
        tokens[count++] = token;
    }
    tokens[count] = (Token){.type = TOKEN_EOF, .start_ptr = cursor, .var_idx = -1};
    statement.tokens = tokens;
    statement.token_count = count;
    TokenStream ts = {tokens, 0, &statement};
    int result = resolve_user_type_reference_tok(&ts, base_token,
                                                 member_index, member_array_index);
    if (result != 0) {
        *input = tokens[ts.pos].start_ptr;
    }
    return result;
}

/*
 * DEF FN bodies are immutable after their definition.  Compile the numeric,
 * side-effect-free subset once so hot functions do not repeatedly scan their
 * source text.  More involved expressions deliberately retain the mature raw
 * evaluator below; this keeps compatibility for strings, arrays, built-ins,
 * and nested function calls.
 */
static int compile_expression_node(UserFunction *function, unsigned char op, int left, int right, Token token) {
    if (function->compiled_node_count >= MAX_COMPILED_EXPRESSION_NODES) return -1;
    int index = function->compiled_node_count++;
    function->compiled_nodes[index] = (CompiledExpressionNode){op, left, right, token};
    return index;
}

static int compile_primary(TokenStream *ts, UserFunction *function);

static int compile_unary(TokenStream *ts, UserFunction *function);

/* Same precedence as the evaluator: ^ binds left to right. */
static int compile_power(TokenStream *ts, UserFunction *function) {
    int left = compile_primary(ts, function);
    if (left < 0) return -1;
    while (ts->tokens[ts->pos].type == TOKEN_POWER) {
        ts->pos++;
        TokenType next = ts->tokens[ts->pos].type;
        int right = (next == TOKEN_MINUS || next == TOKEN_PLUS)
            ? compile_unary(ts, function) : compile_primary(ts, function);
        if (right < 0) return -1;
        left = compile_expression_node(function, EXPR_POWER, left, right, (Token){0});
        if (left < 0) return -1;
    }
    return left;
}

static int compile_unary(TokenStream *ts, UserFunction *function) {
    TokenType type = ts->tokens[ts->pos].type;
    if (type == TOKEN_PLUS || type == TOKEN_MINUS) {
        ts->pos++;
        int operand = compile_unary(ts, function);
        if (operand < 0) return -1;
        return type == TOKEN_PLUS ? operand : compile_expression_node(function, EXPR_NEGATE, operand, -1, (Token){0});
    }
    return compile_power(ts, function);
}

static int compile_multiply(TokenStream *ts, UserFunction *function) {
    int left = compile_unary(ts, function);
    if (left < 0) return -1;
    while (ts->tokens[ts->pos].type == TOKEN_STAR || ts->tokens[ts->pos].type == TOKEN_SLASH) {
        unsigned char op = ts->tokens[ts->pos++].type == TOKEN_STAR ? EXPR_MULTIPLY : EXPR_DIVIDE;
        int right = compile_unary(ts, function);
        if (right < 0) return -1;
        left = compile_expression_node(function, op, left, right, (Token){0});
        if (left < 0) return -1;
    }
    return left;
}

static int compile_integer_divide(TokenStream *ts, UserFunction *function) {
    int left = compile_multiply(ts, function);
    if (left < 0) return -1;
    while (ts->tokens[ts->pos].type == TOKEN_IDIV) {
        ts->pos++;
        int right = compile_multiply(ts, function);
        if (right < 0) return -1;
        left = compile_expression_node(function, EXPR_IDIV, left, right, (Token){0});
        if (left < 0) return -1;
    }
    return left;
}

static int compile_term(TokenStream *ts, UserFunction *function) {
    int left = compile_integer_divide(ts, function);
    if (left < 0) return -1;
    while (ts->tokens[ts->pos].type == TOKEN_MOD) {
        ts->pos++;
        int right = compile_integer_divide(ts, function);
        if (right < 0) return -1;
        left = compile_expression_node(function, EXPR_MOD, left, right, (Token){0});
        if (left < 0) return -1;
    }
    return left;
}

static int compile_arithmetic(TokenStream *ts, UserFunction *function) {
    int left = compile_term(ts, function);
    if (left < 0) return -1;
    while (ts->tokens[ts->pos].type == TOKEN_PLUS || ts->tokens[ts->pos].type == TOKEN_MINUS) {
        TokenType type = ts->tokens[ts->pos++].type;
        int right = compile_term(ts, function);
        if (right < 0) return -1;
        left = compile_expression_node(function, type == TOKEN_PLUS ? EXPR_ADD : EXPR_SUBTRACT, left, right, (Token){0});
        if (left < 0) return -1;
    }
    return left;
}

static int compile_relational(TokenStream *ts, UserFunction *function) {
    int left = compile_arithmetic(ts, function);
    if (left < 0) return -1;
    while (1) {
        TokenType type = ts->tokens[ts->pos].type;
        unsigned char op;
        if (type == TOKEN_EQUALS) op = EXPR_EQUAL;
        else if (type == TOKEN_LESS) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_GREATER) { ts->pos++; op = EXPR_NOT_EQUAL; }
            else if (ts->tokens[ts->pos].type == TOKEN_EQUALS) { ts->pos++; op = EXPR_LESS_EQUAL; }
            else op = EXPR_LESS;
            int right = compile_arithmetic(ts, function);
            if (right < 0) return -1;
            left = compile_expression_node(function, op, left, right, (Token){0});
            if (left < 0) return -1;
            continue;
        } else if (type == TOKEN_GREATER) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_EQUALS) { ts->pos++; op = EXPR_GREATER_EQUAL; }
            else op = EXPR_GREATER;
            int right = compile_arithmetic(ts, function);
            if (right < 0) return -1;
            left = compile_expression_node(function, op, left, right, (Token){0});
            if (left < 0) return -1;
            continue;
        } else break;
        ts->pos++;
        int right = compile_arithmetic(ts, function);
        if (right < 0) return -1;
        left = compile_expression_node(function, op, left, right, (Token){0});
        if (left < 0) return -1;
    }
    return left;
}

static int compile_not(TokenStream *ts, UserFunction *function) {
    if (ts->tokens[ts->pos].type == TOKEN_NOT) {
        ts->pos++;
        int operand = compile_not(ts, function);
        return operand < 0 ? -1 : compile_expression_node(function, EXPR_NOT, operand, -1, (Token){0});
    }
    return compile_relational(ts, function);
}

static int compile_and(TokenStream *ts, UserFunction *function) {
    int left = compile_not(ts, function);
    while (left >= 0 && ts->tokens[ts->pos].type == TOKEN_AND) {
        ts->pos++;
        int right = compile_not(ts, function);
        left = right < 0 ? -1 : compile_expression_node(function, EXPR_AND, left, right, (Token){0});
    }
    return left;
}

static int compile_or(TokenStream *ts, UserFunction *function) {
    int left = compile_and(ts, function);
    while (left >= 0 && ts->tokens[ts->pos].type == TOKEN_OR) {
        ts->pos++;
        int right = compile_and(ts, function);
        left = right < 0 ? -1 : compile_expression_node(function, EXPR_OR, left, right, (Token){0});
    }
    return left;
}

static int compile_primary(TokenStream *ts, UserFunction *function) {
    Token token = ts->tokens[ts->pos++];
    if (token.type == TOKEN_NUMBER) return compile_expression_node(function, EXPR_NUMBER, -1, -1, token);
    if (token.type == TOKEN_IDENTIFIER) {
        int const_idx = find_named_constant(token.text);
        if (const_idx >= 0) {
            Num constant = named_constants[const_idx].value;
            Token num_token = (Token){.type = TOKEN_NUMBER, .double_val = num_to_double(constant),
                                       .int64_val = constant.i, .num_kind = constant.kind,
                                       .is_double = named_constants[const_idx].is_wide};
            return compile_expression_node(function, EXPR_NUMBER, -1, -1, num_token);
        }
        size_t length = strlen(token.text);
        if (length && token.text[length - 1] != '$') return compile_expression_node(function, EXPR_VARIABLE, -1, -1, token);
        return -1;
    }
    if (token.type == TOKEN_LPAREN) {
        int result = compile_or(ts, function);
        if (result < 0 || ts->tokens[ts->pos].type != TOKEN_RPAREN) return -1;
        ts->pos++;
        return result;
    }
    return -1;
}

static void compile_user_function_expression(UserFunction *function) {
    Token tokens[256];
    const char *input = function->expression;
    int count = 0;
    while (count < 255) {
        Token token = get_next_token(&input);
        if (token.type == TOKEN_EOF || token.type == TOKEN_COLON) break;
        tokens[count++] = token;
    }
    tokens[count] = (Token){.type = TOKEN_EOF, .var_idx = -1};
    function->compiled_node_count = 0;
    function->compiled_root = -1;
    function->has_compiled_expression = 0;
    TokenStream ts = {tokens, 0, NULL};
    int root = compile_or(&ts, function);
    if (root >= 0 && ts.tokens[ts.pos].type == TOKEN_EOF) {
        function->compiled_root = root;
        function->has_compiled_expression = 1;
    }
}

static Num evaluate_compiled_expression_node(const UserFunction *function, int index) {
    const CompiledExpressionNode *node = &function->compiled_nodes[index];
    if (node->op == EXPR_NUMBER) {
        if (node->token.is_double) last_expression_is_double = 1;
        return num_from_token(&node->token);
    }
    if (node->op == EXPR_VARIABLE) {
        int const_idx = find_named_constant(node->token.text);
        if (const_idx >= 0) {
            if (named_constants[const_idx].is_wide) last_expression_is_double = 1;
            return named_constants[const_idx].value;
        }
        int variable_index = find_variable(node->token.text);
        Variable *variable = variable_index >= 0 ? get_variable_ptr(variable_index) : NULL;
        if (variable && var_is_wide(variable)) last_expression_is_double = 1;
        return variable ? load_num(variable, -1) : num_f(0, NUM_SINGLE);
    }

    Num left = evaluate_compiled_expression_node(function, node->left);
    if (node->op == EXPR_NEGATE) return num_negate(left);
    if (node->op == EXPR_NOT) return num_logical('~', left, left);
    Num right = evaluate_compiled_expression_node(function, node->right);
    switch (node->op) {
        case EXPR_ADD: return num_add(left, right);
        case EXPR_SUBTRACT: return num_subtract(left, right);
        case EXPR_MULTIPLY: return num_multiply(left, right);
        case EXPR_DIVIDE: return num_divide(left, right);
        case EXPR_MOD: return num_integer_divide(left, right, 1);
        case EXPR_IDIV: return num_integer_divide(left, right, 0);
        case EXPR_POWER: return num_power(left, right);
        case EXPR_EQUAL: return num_truth(num_compare(left, right) == 0);
        case EXPR_NOT_EQUAL: return num_truth(num_compare(left, right) != 0);
        case EXPR_LESS: return num_truth(num_compare(left, right) < 0);
        case EXPR_LESS_EQUAL: return num_truth(num_compare(left, right) <= 0);
        case EXPR_GREATER: return num_truth(num_compare(left, right) > 0);
        case EXPR_GREATER_EQUAL: return num_truth(num_compare(left, right) >= 0);
        case EXPR_AND: return num_logical('&', left, right);
        case EXPR_OR: return num_logical('|', left, right);
        case EXPR_XOR: return num_logical('^', left, right);
        default: return num_i(0, NUM_INTEGER);
    }
}

static Num evaluate_num_text(const char **input);

/* DEF FN: the parameter temporarily takes the argument's value. A function
 * whose name has an integer suffix (FNA%) returns that type. */
static Num evaluate_user_function(UserFunction *function, Num argument) {
    int parameter_index = find_variable(function->param_name);
    Variable *parameter = &vars[parameter_index];
    int64_t old_bits = parameter->ivalue;
    store_num(parameter, -1, argument);
    Num result;
    if (function->has_compiled_expression) {
        result = evaluate_compiled_expression_node(function, function->compiled_root);
    } else {
        const char *expression = function->expression;
        result = evaluate_num_text(&expression);
    }
    parameter->ivalue = old_bits;
    return num_as_name_type(result, function->name);
}

static void reset_file_field_state(int fnum) {
    if (fnum < 1 || fnum >= 16) return;
    if (file_field_state[fnum].buffer) {
        free(file_field_state[fnum].buffer);
        file_field_state[fnum].buffer = NULL;
    }
    file_field_state[fnum].size = 0;
    file_field_state[fnum].binding_count = 0;
}

static int find_field_binding(int idx, int array_idx, int *out_fnum, FieldBinding *out_binding) {
    for (int fnum = 15; fnum >= 1; fnum--) {
        FileFieldState *state = &file_field_state[fnum];
        for (int i = state->binding_count - 1; i >= 0; i--) {
            if (state->bindings[i].var_idx == idx && state->bindings[i].array_idx == array_idx) {
                if (out_fnum) *out_fnum = fnum;
                if (out_binding) *out_binding = state->bindings[i];
                return 1;
            }
        }
    }
    return 0;
}

static void write_field_segment(int fnum, const FieldBinding *binding, const char *value, int right_justify) {
    if (fnum < 1 || fnum >= 16 || !binding) return;
    FileFieldState *state = &file_field_state[fnum];
    if (!state->buffer || binding->offset < 0 || binding->len < 0) return;
    if (binding->offset >= state->size) return;

    int seg_len = binding->len;
    if (binding->offset + seg_len > state->size) {
        seg_len = state->size - binding->offset;
    }
    if (seg_len < 0) return;

    char *dest = state->buffer + binding->offset;
    memset(dest, ' ', seg_len);

    if (!value) value = "";
    int src_len = (int)strlen(value);
    if (src_len > seg_len) src_len = seg_len;
    int start = right_justify ? (seg_len - src_len) : 0;
    if (start < 0) start = 0;
    memcpy(dest + start, value + ((right_justify && (int)strlen(value) > seg_len) ? ((int)strlen(value) - seg_len) : 0), src_len);
    state->buffer[state->size] = '\0';
}

static int get_field_binding_for_var(int idx, int array_idx, int *out_fnum, FieldBinding *out_binding) {
    return find_field_binding(idx, array_idx, out_fnum, out_binding);
}

typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY,
    EVENT_TIMER,
    EVENT_STRIG,
    EVENT_PEN
} TrappedEventType;

typedef struct {
    Statement *stmt;
    const char *ptr;
    TrappedEventType event_type;
    int event_index;
} Subroutine;

static Subroutine gosub_call_stack[32];
static int gosub_ptr = 0;

// Event Trapping globals
static int on_key_line[32];
static int key_state[32]; // 0=OFF, 1=ON, 2=STOP
static int key_event_pending[32];
static int key_event_active[32];
/* Number of set slots in key_event_pending. The statement loop polls event
 * traps after every statement, so this count keeps that poll O(1) for
 * programs with no pending key traps, which is the common case. */
static int key_event_pending_count = 0;

/* ON TIMER traps. Slot 0 is the classic unnamed timer; slots 1... hold
 * numbers that _FREETIMER handed out, and TIMER(t) FREE gives them back. */
#define MAX_TIMER_SLOTS 33
typedef struct {
    int used;               /* the slot holds a TIMER(t) trap (always for 0) */
    int state;              /* 0=OFF, 1=ON, 2=STOP */
    int pending;
    int event_active;       /* its handler is running (one GOSUB at a time) */
    double interval;
    double last_trigger_time;
    int handler_line;
} TimerSlot;
static TimerSlot timers[MAX_TIMER_SLOTS];
/* Slots with a handler installed. The statement loop polls the timer traps
 * after every statement, so this count keeps that poll O(1) for programs
 * with no ON TIMER traps, which is the common case. */
static int timer_trap_count = 0;

static int on_strig_line[8];
static int strig_state = 0; // 0=OFF, 1=ON, 2=STOP
static int strig_event_pending[8];
static int strig_event_active[8];
static int strig_button_pressed_since[8];
static int strig_button_currently_pressed[8];

static int on_pen_line = 0;
static int pen_state = 0; // 0=OFF, 1=ON, 2=STOP
static int pen_event_pending = 0;
static int pen_event_active = 0;
/* Number of pending PEN and STRIG events combined, so the statement loop's
 * device-trap poll is O(1) when nothing is pending. */
static int device_event_pending_count = 0;

static double wall_time_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

void basika_trigger_key_event(int key_idx) {
    if (key_idx >= 1 && key_idx < 32) {
        if (key_state[key_idx] == 1 || key_state[key_idx] == 2) {
            if (!key_event_pending[key_idx]) key_event_pending_count++;
            key_event_pending[key_idx] = 1;
        }
    }
}

static int dispatch_pending_timer_event(Statement *exec_stmt, TokenStream *ts, Statement **curr, const char **resume_ptr) {
    if (timer_trap_count == 0) return 0; /* no traps armed: nothing can be pending */
    for (int n = 0; n < MAX_TIMER_SLOTS; n++) {
        TimerSlot *t = &timers[n];
        /* STOP (2) holds a pending event until TIMER ON, as in QBasic. */
        if (!t->pending || t->state != 1 || t->event_active || t->handler_line == 0) continue;

        Statement *target = find_line(t->handler_line);
        if (!target) {
            report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
            t->pending = 0;
            t->state = 0;
            return 1;
        }
        if (gosub_ptr >= 32) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            t->pending = 0;
            return 1;
        }

        gosub_call_stack[gosub_ptr].stmt = exec_stmt;
        if (ts->pos < exec_stmt->token_count) {
            gosub_call_stack[gosub_ptr].ptr = exec_stmt->tokens[ts->pos].start_ptr;
        } else {
            gosub_call_stack[gosub_ptr].ptr = exec_stmt->raw_command + strlen(exec_stmt->raw_command);
        }
        gosub_call_stack[gosub_ptr].event_type = EVENT_TIMER;
        gosub_call_stack[gosub_ptr].event_index = n;
        gosub_ptr++;

        t->pending = 0;
        t->event_active = 1;
        *curr = target;
        *resume_ptr = NULL;
        return 1;
    }
    return 0;
}

static int dispatch_pending_key_event(Statement *exec_stmt, TokenStream *ts, Statement **curr, const char **resume_ptr) {
    if (key_event_pending_count == 0) return 0; /* nothing pending */
    for (int k = 1; k < 32; k++) {
        if (!key_event_pending[k]) continue;
        if ((key_state[k] != 1 && key_state[k] != 2) || key_event_active[k] || on_key_line[k] == 0) continue;

        Statement *target = find_line(on_key_line[k]);
        if (!target) {
            report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
            key_event_pending[k] = 0;
            key_event_pending_count--;
            return 1;
        }
        if (gosub_ptr >= 32) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            key_event_pending[k] = 0;
            key_event_pending_count--;
            return 1;
        }

        gosub_call_stack[gosub_ptr].stmt = exec_stmt;
        if (ts->pos < exec_stmt->token_count) {
            gosub_call_stack[gosub_ptr].ptr = exec_stmt->tokens[ts->pos].start_ptr;
        } else {
            gosub_call_stack[gosub_ptr].ptr = exec_stmt->raw_command + strlen(exec_stmt->raw_command);
        }
        gosub_call_stack[gosub_ptr].event_type = EVENT_KEY;
        gosub_call_stack[gosub_ptr].event_index = k;
        gosub_ptr++;

        key_event_pending[k] = 0;
        key_event_pending_count--;
        key_event_active[k] = 1;
        *curr = target;
        *resume_ptr = NULL;
        return 1;
    }
    return 0;
}

/* Jumps to an ON STRIG or ON PEN handler as a GOSUB once its event is pending. */
static int enter_event_handler(Statement *exec_stmt, TokenStream *ts, Statement **curr,
                               const char **resume_ptr, int line, int event_type, int index) {
    Statement *target = find_line(line);
    if (!target) {
        report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
        return 1;
    }
    if (gosub_ptr >= 32) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return 1;
    }
    gosub_call_stack[gosub_ptr].stmt = exec_stmt;
    gosub_call_stack[gosub_ptr].ptr = ts->pos < exec_stmt->token_count
        ? exec_stmt->tokens[ts->pos].start_ptr
        : exec_stmt->raw_command + strlen(exec_stmt->raw_command);
    gosub_call_stack[gosub_ptr].event_type = event_type;
    gosub_call_stack[gosub_ptr].event_index = index;
    gosub_ptr++;
    *curr = target;
    *resume_ptr = NULL;
    return 1;
}

static int dispatch_pending_device_event(Statement *exec_stmt, TokenStream *ts, Statement **curr, const char **resume_ptr) {
    if (device_event_pending_count == 0) return 0; /* nothing pending */
    if (pen_event_pending && pen_state == 1 && !pen_event_active && on_pen_line) {
        pen_event_pending = 0;
        device_event_pending_count--;
        pen_event_active = 1;
        return enter_event_handler(exec_stmt, ts, curr, resume_ptr, on_pen_line, EVENT_PEN, 0);
    }
    for (int s = 0; s < 8; s += 2) {
        if (!strig_event_pending[s] || strig_state != 1 || strig_event_active[s] || !on_strig_line[s]) continue;
        strig_event_pending[s] = 0;
        device_event_pending_count--;
        strig_event_active[s] = 1;
        return enter_event_handler(exec_stmt, ts, curr, resume_ptr, on_strig_line[s], EVENT_STRIG, s);
    }
    return 0;
}

/* ON TIMER[(t,)] seconds GOSUB line: arms the classic unnamed timer (no
 * number) or timer t, whose number _FREETIMER handed out. Reports an error
 * and returns 0 for a bad number or interval. */
static int arm_timer(int slot, double interval, int target_line) {
    if (interval <= 0.0 || slot < 0 || slot >= MAX_TIMER_SLOTS) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return 0;
    }
    TimerSlot *t = &timers[slot];
    t->used = 1;
    t->interval = interval;
    timer_trap_count += (target_line != 0) - (t->handler_line != 0);
    t->handler_line = target_line;
    t->pending = 0;
    t->event_active = 0;
    t->last_trigger_time = wall_time_seconds();
    return 1;
}

/* TIMER [(t)] ON|OFF|STOP|FREE: runs, holds or releases a timer trap. */
static void set_timer_state(int slot, Token mode) {
    TimerSlot *t = (slot >= 0 && slot < MAX_TIMER_SLOTS) ? &timers[slot] : NULL;
    if (!t || (slot > 0 && !t->used)) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }
    if (mode.type == TOKEN_ON) {
        t->state = 1;
        t->last_trigger_time = wall_time_seconds();
    } else if (mode.type == TOKEN_OFF) {
        t->state = 0;
        t->pending = 0;
        t->event_active = 0;
    } else if (mode.type == TOKEN_STOP) {
        t->state = 2;
    } else if (mode.type == TOKEN_IDENTIFIER && strcasecmp(mode.text, "FREE") == 0) {
        // TIMER(t) FREE drops the trap and hands the number back.
        t->state = 0;
        t->pending = 0;
        t->event_active = 0;
        t->interval = 0.0;
        timer_trap_count -= (t->handler_line != 0);
        t->handler_line = 0;
        if (slot > 0) t->used = 0;
    } else {
        report_runtime_error(ERR_SYNTAX_ERROR);
    }
}

/* _FREETIMER: hands out free timer numbers (1..., 32) for ON TIMER(t, n)
 * GOSUB, or 0 when they are all taken. The number stays allocated until
 * TIMER(t) FREE hands it back. */
static int alloc_freetimer(void) {
    for (int n = 1; n < MAX_TIMER_SLOTS; n++) {
        if (!timers[n].used) {
            timers[n].used = 1;
            return n;
        }
    }
    return 0;
}

/* SHELL command$ with QB64's _HIDE (hide the output) and _DONTWAIT (return
 * at once) options. */
static void run_shell_command(const char *cmd, int hide, int dont_wait) {
    char full[288];
    snprintf(full, sizeof(full), "%s%s%s", cmd,
             hide ? " > /dev/null 2>&1" : "", dont_wait ? " &" : "");
    fflush(stdout);
    system(full);
}

/* Runs after every statement; with no traps armed the whole check is one
 * load and branch, so the loop lives in an out-of-line slow path. */
static void queue_timer_events_slow(void) {
    for (int n = 0; n < MAX_TIMER_SLOTS; n++) {
        TimerSlot *t = &timers[n];
        if (t->state == 0 || t->handler_line == 0 || t->interval <= 0.0 || t->pending || t->event_active) continue;

        if (!find_line(t->handler_line)) {
            report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
            t->state = 0;
            t->pending = 0;
            return;
        }

        double now = wall_time_seconds();
        if (now - t->last_trigger_time >= t->interval) {
            t->pending = 1;
            t->last_trigger_time = now;
        }
    }
}

static inline __attribute__((always_inline)) void maybe_queue_timer_event(void) {
    if (timer_trap_count == 0) return; /* no traps armed: nothing to queue */
    queue_timer_events_slow();
}

void basika_trigger_pen_event(void) {
    if (pen_state == 1 || pen_state == 2) {
        if (!pen_event_pending) device_event_pending_count++;
        pen_event_pending = 1;
    }
}

void basika_trigger_strig_event(int strig_idx, int pressed) {
    if (strig_idx >= 0 && strig_idx < 8) {
        if (pressed) {
            strig_button_pressed_since[strig_idx] = 1;
            strig_button_currently_pressed[strig_idx] = 1;
            if (strig_state == 1 || strig_state == 2) {
                if (!strig_event_pending[strig_idx]) device_event_pending_count++;
                strig_event_pending[strig_idx] = 1;
            }
        } else {
            strig_button_currently_pressed[strig_idx] = 0;
        }
    }
}

typedef struct {
    int var_idx;
    Num end_val;
    Num step_val;
    Statement *start_stmt;
    int start_ts_pos;
    int block_depth, select_depth; // block IF / SELECT CASE depth at the FOR
} ForLoop;

static ForLoop for_stack[16];
static int for_ptr = 0;

/* A FOR loop repeats while the counter has not passed the limit; a STEP of
 * 0 counts as upward, so it loops until EXIT FOR. */
static inline __attribute__((always_inline)) int for_loop_condition(Num value, Num end, Num step) {
    int compare = num_compare(value, end);
    return (step.kind < NUM_SINGLE ? step.i >= 0 : step.d >= 0) ? compare <= 0 : compare >= 0;
}

/* FOR: assigns the start value and records the limit and step. An integer
 * counter counts in whole steps up to a rounded limit, as in QBasic. Returns
 * 1 when the body runs, 0 when it is skipped and -1 on an error. */
static int for_loop_begin(ForLoop *loop, int idx, Num start, Num end, Num step) {
    Variable *v = get_variable_ptr(idx);
    if (runtime_error_occurred || !store_num(v, -1, start)) return -1;
    if (var_type_is_integer(var_type(v))) {
        int64_t whole;
        int kind;
        if (!num_is_int(end)) {
            if (!(kind = num_whole_operand(end, &whole))) return -1;
            end = num_i(whole, kind);
        }
        if (!num_is_int(step)) {
            if (!(kind = num_whole_operand(step, &whole))) return -1;
            step = num_i(whole, kind);
        }
    } else {
        // A float counter steps in floating point; convert once, not per NEXT.
        end = num_f(num_to_double(end), end.kind >= NUM_SINGLE ? end.kind : NUM_DOUBLE);
        step = num_f(num_to_double(step), step.kind >= NUM_SINGLE ? step.kind : NUM_SINGLE);
    }
    loop->var_idx = idx;
    loop->end_val = end;
    loop->step_val = step;
    return for_loop_condition(load_num(v, -1), end, step);
}

/* NEXT: steps the counter (an INTEGER counter stepping past 32767 is an
 * Overflow, as in QBasic). Returns 1 when the body runs again. */
static inline __attribute__((always_inline)) int for_loop_next(ForLoop *loop) {
    Variable *v = get_variable_ptr(loop->var_idx);
    Num next = num_add(load_num(v, -1), loop->step_val);
    if (runtime_error_occurred || !store_num(v, -1, next)) return 0;
    /* An integer counter of the value's own kind stored the stepped value
     * exactly as computed, so the reload would read it back unchanged. */
    int vt = var_type(v);
    Num value = (var_type_is_integer(vt) && next.kind == var_type_kind[vt])
        ? next : load_num(v, -1);
    return for_loop_condition(value, loop->end_val, loop->step_val);
}

static inline __attribute__((always_inline)) int run_next_list(TokenStream *ts, const Statement *stmt, ForLoop **repeat);
static int apply_resize_metacommand(const char *text, int check_only);

/* After skipping a loop that runs zero times to its NEXT (stmt, *pos): when
 * that NEXT names more loops (NEXT j, i), steps them. Returns 1 with *repeat
 * set if one goes round again, 0 to carry on at *pos, -1 on error. */
static int finish_skipped_next(Statement *stmt, int *pos, ForLoop **repeat) {
    if (*pos + 1 >= stmt->token_count || stmt->tokens[*pos].type != TOKEN_COMMA ||
        stmt->tokens[*pos + 1].type != TOKEN_IDENTIFIER) {
        return 0;
    }
    TokenStream rest = {stmt->tokens, *pos + 1, stmt};
    int result = run_next_list(&rest, stmt, repeat);
    *pos = rest.pos;
    return result;
}

/* NEXT [var[, var...]] with ts at the first name: each name steps its loop
 * (no name: the innermost one), and NEXT j, i goes on to i once j is done.
 * Returns 1 with *repeat set when a loop goes round again (loops inside it
 * are dropped), 0 when every loop named has ended (ts after the list), and
 * -1 on NEXT without FOR. */
static inline __attribute__((always_inline)) int run_next_list(TokenStream *ts, const Statement *stmt, ForLoop **repeat) {
    while (1) {
        int f = for_ptr - 1;
        if (ts->pos < stmt->token_count && ts->tokens[ts->pos].type == TOKEN_IDENTIFIER) {
            int var_idx = resolve_token_variable(&ts->tokens[ts->pos++]);
            for (f = for_ptr - 1; f >= 0 && for_stack[f].var_idx != var_idx; f--) {}
        }
        if (f < 0) {
            report_runtime_error(ERR_NEXT_WITHOUT_FOR);
            return -1;
        }
        if (for_loop_next(&for_stack[f])) {
            for_ptr = f + 1;
            *repeat = &for_stack[f];
            return 1;
        }
        for_ptr = f; // the loop ends
        if (runtime_error_occurred) return -1;
        if (ts->pos + 1 < stmt->token_count && ts->tokens[ts->pos].type == TOKEN_COMMA &&
            ts->tokens[ts->pos + 1].type == TOKEN_IDENTIFIER) {
            ts->pos++;
            continue;
        }
        return 0;
    }
}

typedef struct {
    Statement *stmt;
    const char *ptr;
} WhileLoop;
static WhileLoop while_stack[16];
static int while_ptr = 0;

typedef struct {
    Statement *stmt;
    const char *ptr;
    int block_depth, select_depth; // block IF / SELECT CASE depth at the DO
} DoLoop;
static DoLoop do_stack[16];
static int do_ptr = 0;

#define MAX_BLOCK_IF_DEPTH 128
typedef struct {
    int branch_taken;
    int else_seen;
} BlockIfFrame;

/* The tokens of a statement before a trailing ' or REM comment. */
static inline __attribute__((always_inline)) int tokens_before_comment(const Statement *stmt) {
    int count = stmt->token_count;
    if (count > 0 && (stmt->tokens[count - 1].type == TOKEN_APOSTROPHE ||
                      stmt->tokens[count - 1].type == TOKEN_REM)) {
        count--;
    }
    return count;
}

static inline __attribute__((always_inline)) int is_block_if_header(const Statement *stmt) {
    if (!stmt || stmt->token_count < 2 || stmt->tokens[0].type != TOKEN_IF) {
        return 0;
    }
    int count = tokens_before_comment(stmt);
    return count >= 2 && stmt->tokens[count - 1].type == TOKEN_THEN;
}

static int is_block_elseif_header(const Statement *stmt) {
    if (!stmt || stmt->token_count < 2 || stmt->tokens[0].type != TOKEN_ELSEIF) {
        return 0;
    }
    int count = tokens_before_comment(stmt);
    return count >= 2 && stmt->tokens[count - 1].type == TOKEN_THEN;
}

static int is_block_else(const Statement *stmt) {
    return stmt && stmt->token_count >= 1 && stmt->tokens[0].type == TOKEN_ELSE &&
           tokens_before_comment(stmt) == 1;
}

static int is_end_if(const Statement *stmt) {
    return stmt && stmt->token_count >= 2 &&
        stmt->tokens[0].type == TOKEN_END && stmt->tokens[1].type == TOKEN_IF;
}

static Statement *find_next_block_if_clause(Statement *start, Statement *limit) {
    int depth = 1;
    for (Statement *stmt = start ? start->next : NULL;
         stmt && stmt != limit;
         stmt = stmt->next) {
        if (is_end_if(stmt)) {
            if (--depth == 0) return stmt;
        } else if (is_block_if_header(stmt)) {
            depth++;
        } else if (depth == 1 &&
                   (is_block_else(stmt) || is_block_elseif_header(stmt))) {
            return stmt;
        }
    }
    return NULL;
}

static Statement *find_block_if_end(Statement *start, Statement *limit) {
    int depth = 1;
    for (Statement *stmt = start ? start->next : NULL;
         stmt && stmt != limit;
         stmt = stmt->next) {
        if (is_end_if(stmt)) {
            if (--depth == 0) return stmt;
        } else if (is_block_if_header(stmt)) {
            depth++;
        }
    }
    return NULL;
}

#define MAX_SELECT_CASE_DEPTH 32
typedef struct {
    int branch_taken;
    int is_string;
    double num_val;
    BasicString str_val; // Only used when is_string is true; owns heap data.
    Statement *header;   // The SELECT CASE statement that opened the frame
    int header_pos;
    int gosub_level;
} SelectCaseFrame;

/* Pops the SELECT CASE frames above keep, freeing their string selectors. */
static void release_select_frames(SelectCaseFrame *stack, int *depth, int keep) {
    while (*depth > keep) {
        SelectCaseFrame *frame = &stack[--*depth];
        if (frame->is_string) basic_string_release(&frame->str_val);
    }
}

/* Opens a frame for the SELECT CASE at exec_stmt/pos. Reaching a SELECT that
 * is still open at the same GOSUB level means the program left it without
 * END SELECT (EXIT DO, GOTO), so that block and any opened inside it are
 * dropped first. Returns NULL when the nesting is too deep. */
static SelectCaseFrame *push_select_frame(SelectCaseFrame *stack, int *depth,
                                          Statement *exec_stmt, int pos) {
    for (int i = 0; i < *depth; i++) {
        if (stack[i].header == exec_stmt && stack[i].header_pos == pos &&
            stack[i].gosub_level == gosub_ptr) {
            release_select_frames(stack, depth, i);
            break;
        }
    }
    if (*depth >= MAX_SELECT_CASE_DEPTH) return NULL;
    SelectCaseFrame *frame = &stack[(*depth)++];
    frame->branch_taken = 0;
    frame->is_string = 0;
    frame->str_val = (BasicString){0};
    frame->header = exec_stmt;
    frame->header_pos = pos;
    frame->gosub_level = gosub_ptr;
    return frame;
}

static int is_select_case_header(const Statement *stmt) {
    return stmt && stmt->token_count >= 3 &&
        stmt->tokens[0].type == TOKEN_SELECT && stmt->tokens[1].type == TOKEN_CASE;
}

static int is_case_else(const Statement *stmt) {
    return stmt && stmt->token_count >= 2 &&
        stmt->tokens[0].type == TOKEN_CASE && stmt->tokens[1].type == TOKEN_ELSE &&
        (stmt->token_count == 2 || stmt->tokens[2].type == TOKEN_COLON);
}

static int is_case_clause(const Statement *stmt) {
    return stmt && stmt->token_count >= 2 &&
        stmt->tokens[0].type == TOKEN_CASE && !is_case_else(stmt);
}

static int is_end_select(const Statement *stmt) {
    return stmt && stmt->token_count >= 2 &&
        stmt->tokens[0].type == TOKEN_END && stmt->tokens[1].type == TOKEN_SELECT;
}

// Returns the next CASE/CASE ELSE clause at the same nesting depth, or the
// matching END SELECT statement if no further clause exists before it.
static Statement *find_next_case_clause(Statement *start, Statement *limit) {
    int depth = 1;
    for (Statement *stmt = start ? start->next : NULL;
         stmt && stmt != limit;
         stmt = stmt->next) {
        if (is_end_select(stmt)) {
            if (--depth == 0) return stmt;
        } else if (is_select_case_header(stmt)) {
            depth++;
        } else if (depth == 1 && (is_case_clause(stmt) || is_case_else(stmt))) {
            return stmt;
        }
    }
    return NULL;
}

static Statement *find_select_case_end(Statement *start, Statement *limit) {
    int depth = 1;
    for (Statement *stmt = start ? start->next : NULL;
         stmt && stmt != limit;
         stmt = stmt->next) {
        if (is_end_select(stmt)) {
            if (--depth == 0) return stmt;
        } else if (is_select_case_header(stmt)) {
            depth++;
        }
    }
    return NULL;
}

// Checks a single relational comparison (used by CASE IS) given the matched
// operator token and optional second-character modifier token, following the
// same combination rules used elsewhere for <>, <=, and >=.
static int case_is_matches_numeric(double lhs, TokenType op, TokenType modifier, double rhs) {
    switch (op) {
        case TOKEN_EQUALS: return lhs == rhs;
        case TOKEN_LESS:
            if (modifier == TOKEN_GREATER) return lhs != rhs;
            if (modifier == TOKEN_EQUALS) return lhs <= rhs;
            return lhs < rhs;
        case TOKEN_GREATER:
            if (modifier == TOKEN_EQUALS) return lhs >= rhs;
            return lhs > rhs;
        default: return 0;
    }
}

static int case_is_matches_string(int cmp, TokenType op, TokenType modifier) {
    switch (op) {
        case TOKEN_EQUALS: return cmp == 0;
        case TOKEN_LESS:
            if (modifier == TOKEN_GREATER) return cmp != 0;
            if (modifier == TOKEN_EQUALS) return cmp <= 0;
            return cmp < 0;
        case TOKEN_GREATER:
            if (modifier == TOKEN_EQUALS) return cmp >= 0;
            return cmp > 0;
        default: return 0;
    }
}

// Parses a comparison operator (=, <>, <, >, <=, >=) following CASE IS.
static void parse_case_is_operator(TokenStream *ts, TokenType *op, TokenType *modifier) {
    *op = ts->tokens[ts->pos++].type;
    *modifier = TOKEN_EOF;
    if ((*op == TOKEN_LESS && (ts->tokens[ts->pos].type == TOKEN_GREATER || ts->tokens[ts->pos].type == TOKEN_EQUALS)) ||
        (*op == TOKEN_GREATER && ts->tokens[ts->pos].type == TOKEN_EQUALS)) {
        *modifier = ts->tokens[ts->pos++].type;
    }
}

// Evaluates a CASE clause's comma-separated item list (values, TO ranges, and
// IS comparisons) against the SELECT CASE frame's stored selector value.
// Advances ts->pos to the end of the statement. "IS" is recognized
// contextually here (as an identifier) rather than as a global keyword, so it
// never collides with user variables named IS elsewhere in a program.
static int evaluate_case_clause_tok(TokenStream *ts, Statement *exec_stmt, SelectCaseFrame *frame) {
    int matched = 0;
    while (ts->pos < exec_stmt->token_count && ts->tokens[ts->pos].type != TOKEN_EOF) {
        int item_match = 0;
        int is_is_clause = ts->tokens[ts->pos].type == TOKEN_IDENTIFIER &&
            strcasecmp(ts->tokens[ts->pos].text, "IS") == 0;

        if (is_is_clause) {
            ts->pos++;
            TokenType op, modifier;
            parse_case_is_operator(ts, &op, &modifier);
            if (frame->is_string) {
                BasicString rhs = {0};
                if (!parse_string_expression_tok_heap(ts, &rhs)) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    basic_string_release(&rhs);
                    return 0;
                }
                int cmp = strcmp((const char *)(frame->str_val.data ? frame->str_val.data : (const unsigned char *)""),
                                  (const char *)(rhs.data ? rhs.data : (const unsigned char *)""));
                basic_string_release(&rhs);
                item_match = case_is_matches_string(cmp, op, modifier);
            } else {
                double rhs = evaluate_expression_tok(ts);
                item_match = case_is_matches_numeric(frame->num_val, op, modifier, rhs);
            }
        } else if (frame->is_string) {
            BasicString v1 = {0};
            if (!parse_string_expression_tok_heap(ts, &v1)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                basic_string_release(&v1);
                return 0;
            }
            if (ts->tokens[ts->pos].type == TOKEN_TO) {
                ts->pos++;
                BasicString v2 = {0};
                if (!parse_string_expression_tok_heap(ts, &v2)) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    basic_string_release(&v2);
                    basic_string_release(&v1);
                    return 0;
                }
                const char *selector = (const char *)(frame->str_val.data ? frame->str_val.data : (const unsigned char *)"");
                const char *lo = (const char *)(v1.data ? v1.data : (const unsigned char *)"");
                const char *hi = (const char *)(v2.data ? v2.data : (const unsigned char *)"");
                if (strcmp(lo, hi) > 0) { const char *tmp = lo; lo = hi; hi = tmp; }
                item_match = strcmp(selector, lo) >= 0 && strcmp(selector, hi) <= 0;
                basic_string_release(&v2);
            } else {
                const char *selector = (const char *)(frame->str_val.data ? frame->str_val.data : (const unsigned char *)"");
                const char *val = (const char *)(v1.data ? v1.data : (const unsigned char *)"");
                item_match = strcmp(selector, val) == 0;
            }
            basic_string_release(&v1);
        } else {
            double v1 = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_TO) {
                ts->pos++;
                double v2 = evaluate_expression_tok(ts);
                double lo = v1 < v2 ? v1 : v2;
                double hi = v1 < v2 ? v2 : v1;
                item_match = frame->num_val >= lo && frame->num_val <= hi;
            } else {
                item_match = frame->num_val == v1;
            }
        }

        if (item_match) matched = 1;
        if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
            ts->pos++;
        } else {
            break;
        }
    }
    return matched;
}

// Helper function to convert multi-dimensional indices to linear index
static int calc_linear_index(Variable *var, int *indices, int num_indices) {
    if (var->num_dims == 0 || var->num_dims != num_indices) {
        report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
        return -1;
    }
    
    int linear = 0;
    int multiplier = 1;
    for (int i = var->num_dims - 1; i >= 0; i--) {
        int max_subscript = var->dims[i];
        int min_subscript = var->lower_bounds[i];
        int val = indices[i];
        if (val < min_subscript || val > max_subscript) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return -1;
        }
        int offset = val - min_subscript;
        linear += offset * multiplier;
        
        int dim_size = max_subscript - min_subscript + 1;
        multiplier *= dim_size;
    }
    return linear;
}

static void ensure_array_dimensioned(int idx, int num_dims) {
    Variable *variable = get_variable_ptr(idx);
    if (variable->array || variable->s_array) return; // already dimensioned
    
    // Auto-dimension to 10 for each dimension
    int total_size = 1;
    for (int i = 0; i < num_dims; i++) {
        int dim_size = (option_base == 0) ? 11 : 10;
        total_size *= dim_size;
    }
    
    if (variable->name[strlen(variable->name)-1] == '$') {
        variable->s_array = calloc(total_size, sizeof(char*));
    } else {
        variable->array = malloc(total_size * sizeof(double));
        if (!variable->array) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return;
        }
        for (int i = 0; i < total_size; i++) variable->array[i] = 0;
    }
    variable->array_size = total_size;
    variable->num_dims = num_dims;
    for (int i = 0; i < num_dims; i++) {
        variable->lower_bounds[i] = option_base;
        variable->dims[i] = 10;
    }
    
    arrays_dimensioned = 1;
}

/* Subscripts are rounded to whole numbers (half to even) like CINT, so
 * a(1.5) is a(2) as in QBasic. Out-of-range values map to INT_MIN, which
 * calc_linear_index reports as "Subscript out of range". */
static int array_subscript(Num value) {
    if (num_is_int(value)) {
        if (value.kind == NUM_UINT64 && value.i < 0) return INT_MIN;
        return value.i >= INT_MIN && value.i <= INT_MAX ? (int)value.i : INT_MIN;
    }
    double r = nearbyint(value.d);
    return r >= INT_MIN && r <= INT_MAX ? (int)r : INT_MIN;
}

static int parse_array_index(const char **input, int var_idx) {
    const char *saved = *input;
    Token t = get_next_token(input);
    if (t.type != TOKEN_LPAREN) {
        *input = saved;
        return -1;
    }

    int indices[3] = {0};
    int num_indices = 0;
    indices[0] = array_subscript(evaluate_num_text(input));
    num_indices = 1;

    while (num_indices < 3) {
        const char *sep_saved = *input;
        Token sep = get_next_token(input);
        if (sep.type != TOKEN_COMMA) {
            *input = sep_saved;
            break;
        }
        indices[num_indices++] = array_subscript(evaluate_num_text(input));
    }

    Token close = get_next_token(input);
    if (close.type != TOKEN_RPAREN) {
        *input = saved;
        return -1;
    }

    // Auto-dimension if not already dimensioned
    ensure_array_dimensioned(var_idx, num_indices);

    return calc_linear_index(get_variable_ptr(var_idx), indices, num_indices);
}

static int parse_array_index_tok(TokenStream *ts, int var_idx) {
    if (ts->tokens[ts->pos].type != TOKEN_LPAREN) return -1;
    ts->pos++;

    Token *index_token = &ts->tokens[ts->pos];
    if (ts->tokens[ts->pos + 1].type == TOKEN_RPAREN) {
        int index_value = 0;
        int simple_index = 0;
        if (index_token->type == TOKEN_NUMBER) {
            index_value = array_subscript(num_from_token(index_token));
            if (index_token->is_double) last_expression_is_double = 1;
            simple_index = 1;
        } else if (index_token->type == TOKEN_IDENTIFIER && proc_count == 0 &&
                   user_function_count == 0 && !is_string_identifier_tok(index_token) &&
                   !name_is_err_or_erl(index_token->text)) {
            int index_var_idx = resolve_token_variable(index_token);
            Variable *index_var = index_var_idx >= 0 ? get_variable_ptr(index_var_idx) : NULL;
            if (index_var) {
                if (name_last_char(index_var->name) == '#') {
                    last_expression_is_double = 1;
                }
                index_value = array_subscript(load_num(index_var, -1));
                simple_index = 1;
            }
        }

        if (simple_index) {
            ts->pos += 2;
            ensure_array_dimensioned(var_idx, 1);
            int indices[1] = {index_value};
            return calc_linear_index(get_variable_ptr(var_idx), indices, 1);
        }
    }

    int indices[3] = {0};
    int num_indices = 0;
    indices[0] = array_subscript(evaluate_num_tok(ts));
    num_indices = 1;

    while (num_indices < 3) {
        if (ts->tokens[ts->pos].type != TOKEN_COMMA) break;
        ts->pos++;
        indices[num_indices++] = array_subscript(evaluate_num_tok(ts));
    }

    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;

    ensure_array_dimensioned(var_idx, num_indices);
    return calc_linear_index(get_variable_ptr(var_idx), indices, num_indices);
}

static void get_string_variable_value(int idx, int array_idx, char *dest, int dest_size) {
    const char *src = "";
    int fnum = 0;
    FieldBinding binding;
    if (get_field_binding_for_var(idx, array_idx, &fnum, &binding)) {
        FileFieldState *state = &file_field_state[fnum];
        if (state->buffer && binding.offset >= 0 && binding.offset < state->size) {
            int copy_len = binding.len;
            if (binding.offset + copy_len > state->size) copy_len = state->size - binding.offset;
            if (copy_len < 0) copy_len = 0;
            if (copy_len >= dest_size) copy_len = dest_size - 1;
            memcpy(dest, state->buffer + binding.offset, copy_len);
            dest[copy_len] = '\0';
            return;
        }
    }

    Variable *v = get_variable_ptr(idx);
    if (array_idx >= 0) {
        if (v->s_array && array_idx >= 0 && array_idx < v->array_size && v->s_array[array_idx]) {
            src = (const char *)v->s_array[array_idx]->data;
        }
    } else {
        if (v->s_value) src = (const char *)v->s_value->data;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}


static void basic_string_to_buffer(const BasicString *src, char *out, int out_size) {
    if (!out || out_size <= 0) return;
    size_t len = src ? src->length : 0;
    if (len >= (size_t)out_size) len = (size_t)out_size - 1;
    if (len > 0 && src && src->data) memcpy(out, src->data, len);
    out[len] = '\0';
}

/* MKI$, MKL$, MKS$, MKD$: the little-endian bytes of an INTEGER, LONG,
 * SINGLE or DOUBLE. MKI$ and MKL$ round like CINT and CLNG, and a value
 * outside the type's range is an Overflow. */
static void append_number_bytes(BasicString *out, TokenType type, Num value) {
    unsigned char bytes[8];
    size_t count;
    if (type == TOKEN_MKI || type == TOKEN_MKL) {
        Num whole = num_convert_integer(value, type == TOKEN_MKI ? VT_INTEGER : VT_LONG);
        count = type == TOKEN_MKI ? 2 : 4;
        for (size_t i = 0; i < count; i++) bytes[i] = (unsigned char)((uint64_t)whole.i >> (8 * i));
    } else if (type == TOKEN_MKS) {
        float single = (float)num_to_double(value);
        memcpy(bytes, &single, count = 4);
    } else {
        double number = num_to_double(value);
        memcpy(bytes, &number, count = 8);
    }
    basic_string_append(out, bytes, count);
}

/* ---- QB64 files and folders: _FILEEXISTS, _DIREXISTS, _DIR$, _CWD$,
 * _STARTDIR$, _FULLPATH$, _FILES$, _READFILE$ and _WRITEFILE ----
 * As in QB64 Phoenix Edition: true is -1, and folders come back with a
 * trailing "/" (from _DIR$, _CWD$, _STARTDIR$, _FULLPATH$ and _FILES$). The
 * functions here do the work and return 0 or the error to report; the
 * expression parsers read the arguments and call them. */
enum {
    QB64_FILE_NONE, QB64_FILE_FILEEXISTS, QB64_FILE_DIREXISTS, QB64_FILE_DIR,
    QB64_FILE_CWD, QB64_FILE_STARTDIR, QB64_FILE_FULLPATH, QB64_FILE_FILES,
    QB64_FILE_READFILE,
    // System, no argument: _OS$, _UPTIME, _COMMANDCOUNT, _ENVIRONCOUNT,
    // _ERRORLINE, _ERRORMESSAGE$, the lock keys and _CLIPBOARD$.
    QB64_SYS_OS, QB64_SYS_UPTIME, QB64_SYS_COMMANDCOUNT, QB64_SYS_ENVIRONCOUNT,
    QB64_SYS_ERRORLINE, QB64_SYS_ERRORMESSAGE, QB64_SYS_CAPSLOCK, QB64_SYS_NUMLOCK,
    QB64_SYS_SCROLLLOCK, QB64_SYS_CLIPBOARD, QB64_SYS_TITLE
};

static int qb64_file_function_id(const char *name) {
    static const struct { const char *name; int id; } functions[] = {
        {"_FILEEXISTS", QB64_FILE_FILEEXISTS}, {"_DIREXISTS", QB64_FILE_DIREXISTS},
        {"_DIR$", QB64_FILE_DIR}, {"_CWD$", QB64_FILE_CWD},
        {"_STARTDIR$", QB64_FILE_STARTDIR}, {"_FULLPATH$", QB64_FILE_FULLPATH},
        {"_FILES$", QB64_FILE_FILES}, {"_READFILE$", QB64_FILE_READFILE},
        {"_OS$", QB64_SYS_OS}, {"_UPTIME", QB64_SYS_UPTIME},
        {"_COMMANDCOUNT", QB64_SYS_COMMANDCOUNT}, {"_ENVIRONCOUNT", QB64_SYS_ENVIRONCOUNT},
        {"_ERRORLINE", QB64_SYS_ERRORLINE}, {"_ERRORMESSAGE$", QB64_SYS_ERRORMESSAGE},
        {"_CAPSLOCK", QB64_SYS_CAPSLOCK}, {"_NUMLOCK", QB64_SYS_NUMLOCK},
        {"_SCROLLLOCK", QB64_SYS_SCROLLLOCK}, {"_CLIPBOARD$", QB64_SYS_CLIPBOARD},
        {"_TITLE$", QB64_SYS_TITLE},
    };
    if (name[0] != '_') return QB64_FILE_NONE;
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        if (strcasecmp(name, functions[i].name) == 0) return functions[i].id;
    }
    return QB64_FILE_NONE;
}

/* _FILEEXISTS and _DIREXISTS take a path and give a number; the others give
 * a string. */
static int qb64_file_function_is_string(int id) {
    return id != QB64_FILE_FILEEXISTS && id != QB64_FILE_DIREXISTS &&
           id != QB64_SYS_UPTIME && id != QB64_SYS_COMMANDCOUNT && id != QB64_SYS_ENVIRONCOUNT &&
           id != QB64_SYS_ERRORLINE && id != QB64_SYS_CAPSLOCK && id != QB64_SYS_NUMLOCK &&
           id != QB64_SYS_SCROLLLOCK;
}

/* _CWD$, _STARTDIR$ take no argument; _FILES$ may or may not have one. */
static int qb64_file_function_takes_argument(int id) {
    return id != QB64_FILE_CWD && id != QB64_FILE_STARTDIR && id < QB64_SYS_OS;
}

static void append_folder(BasicString *out, const char *path) {
    size_t n = strlen(path);
    basic_string_append(out, path, n);
    if (n == 0 || path[n - 1] != '/') basic_string_append(out, "/", 1);
}

static int path_is_dir(const char *path) {
    struct stat st;
    return path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int path_is_file(const char *path) {
    struct stat st;
    return path[0] && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

/* The folder for a _DIR$ name, case-insensitively, with the aliases QB64
 * accepts. Anything else is the desktop, as in QB64. */
static void qb64_special_folder(const char *spec, BasicString *out) {
    static const struct { const char *names; const char *home_relative; } folders[] = {
        {"|desktop|", "Desktop"},
        {"|download|downloads|", "Downloads"},
        {"|documents|document|my documents|text|", "Documents"},
        {"|music|my music|audio|sound|sounds|", "Music"},
        {"|pictures|picture|my pictures|image|images|photo|photos|dcim|camera|camera roll|", "Pictures"},
#ifdef __APPLE__
        {"|video|videos|my videos|movie|movies|", "Movies"},
        {"|appdata|data|application data|program data|common program data|", "Library/Application Support"},
        {"|local data|localappdata|local application data|local program data|", "Library/Application Support"},
        {"|fonts|font|user fonts|user font|userfont|userfonts|", "Library/Fonts"},
#else
        {"|video|videos|my videos|movie|movies|", "Videos"},
        {"|appdata|data|application data|program data|common program data|", ".config"},
        {"|local data|localappdata|local application data|local program data|", ".local/share"},
        {"|fonts|font|user fonts|user font|userfont|userfonts|", ".local/share/fonts"},
#endif
        {"|home|user|profile|userprofile|user profile|", ""},
    };
    char key[128];
    size_t n = 0;
    key[n++] = '|';
    for (const char *p = spec; *p && n < sizeof(key) - 2; p++) key[n++] = (char)tolower((unsigned char)*p);
    key[n++] = '|';
    key[n] = '\0';
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/";
    if (strstr("|temp|tmp|", key)) {
        const char *tmp = getenv("TMPDIR");
        append_folder(out, tmp && tmp[0] ? tmp : "/tmp");
        return;
    }
    if (strstr("|program files|programfiles|program files (x86)|program files x86|programfiles x86|"
               "programfilesx86|program files 86|programfiles (x86)|program files(x86)|programfiles(x86)|", key)) {
#ifdef __APPLE__
        append_folder(out, "/Applications");
#else
        append_folder(out, "/usr/local/bin");
#endif
        return;
    }
    const char *relative = "Desktop";
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        if (strstr(folders[i].names, key)) {
            relative = folders[i].home_relative;
            break;
        }
    }
    char path[4096];
    if (relative[0]) snprintf(path, sizeof(path), "%s/%s", home, relative);
    else snprintf(path, sizeof(path), "%s", home);
    append_folder(out, path);
}

/* _FILES$: the pattern's folder and the names in it matching the pattern,
 * one per call; "" when they run out. */
static struct {
    int started;
    char **names;
    size_t count, next;
} qb64_files;

static void qb64_files_clear(void) {
    for (size_t i = 0; i < qb64_files.count; i++) free(qb64_files.names[i]);
    free(qb64_files.names);
    qb64_files.names = NULL;
    qb64_files.count = qb64_files.next = 0;
}

static void qb64_files_start(const char *spec) {
    qb64_files_clear();
    qb64_files.started = 1;
    // "" means everything; a folder on its own means everything in it.
    char pattern[4096];
    if (!spec[0]) snprintf(pattern, sizeof(pattern), "*");
    else if (path_is_dir(spec)) {
        size_t n = strlen(spec);
        snprintf(pattern, sizeof(pattern), "%s%s*", spec, spec[n - 1] == '/' ? "" : "/");
    } else snprintf(pattern, sizeof(pattern), "%s", spec);
    char folder[4096];
    const char *slash = strrchr(pattern, '/');
    const char *match = slash ? slash + 1 : pattern;
    if (slash) snprintf(folder, sizeof(folder), "%.*s", (int)(slash - pattern + 1), pattern);
    else snprintf(folder, sizeof(folder), "./");
    DIR *dir = opendir(folder);
    if (!dir) return;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (fnmatch(match, entry->d_name, 0) != 0) continue;
        // . and .. come back too, as in QB64, but not hidden files for "*".
        int dots = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0;
        if (entry->d_name[0] == '.' && !dots && match[0] != '.') continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s%s", folder, entry->d_name);
        size_t n = strlen(entry->d_name);
        char *name = malloc(n + 2);
        if (!name) break;
        memcpy(name, entry->d_name, n + 1);
        if (path_is_dir(full)) { name[n] = '/'; name[n + 1] = '\0'; }
        if (qb64_files.count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            char **grown = realloc(qb64_files.names, capacity * sizeof(char *));
            if (!grown) { free(name); break; }
            qb64_files.names = grown;
        }
        qb64_files.names[qb64_files.count++] = name;
    }
    closedir(dir);
}

/* Runs a file function. arg is its argument (NULL when there is none);
 * a number goes in *number, a string in *out. Returns 0 or an error. */
static int qb64_file_function(int id, const BasicString *arg, double *number, BasicString *out) {
    const char *path = arg && arg->data ? (const char *)arg->data : "";
    switch (id) {
        case QB64_SYS_OS: {
#if defined(__APPLE__)
            const char *os = "[MACOSX]";
#else
            const char *os = "[LINUX]";
#endif
            basic_string_append(out, os, strlen(os));
#if defined(__aarch64__) || defined(__arm64__)
            basic_string_append(out, "[64BIT][ARM]", 12);
#else
            basic_string_append(out, "[64BIT]", 7);
#endif
            return 0;
        }
        case QB64_SYS_TITLE: {
            const char *title = graphics_window_title();
            basic_string_append(out, title, strlen(title));
            return 0;
        }
        case QB64_SYS_UPTIME: {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            *number = (double)(now.tv_sec - qb64_start_time.tv_sec) +
                      (double)(now.tv_nsec - qb64_start_time.tv_nsec) / 1e9;
            return 0;
        }
        case QB64_SYS_COMMANDCOUNT:
            *number = internal_argc > 1 ? internal_argc - 1 : 0;
            return 0;
        case QB64_SYS_ENVIRONCOUNT: {
            extern char **environ;
            int count = 0;
            while (environ && environ[count]) count++;
            *number = count;
            return 0;
        }
        case QB64_SYS_ERRORLINE:
            *number = last_runtime_error_line;
            return 0;
        case QB64_SYS_ERRORMESSAGE:
            if (last_runtime_error_code) {
                const char *msg = get_error_message((RuntimeError)last_runtime_error_code);
                basic_string_append(out, msg, strlen(msg));
            }
            return 0;
        case QB64_SYS_CAPSLOCK:
        case QB64_SYS_NUMLOCK:
        case QB64_SYS_SCROLLLOCK:
            *number = graphics_lock_state(id == QB64_SYS_CAPSLOCK ? 1 : id == QB64_SYS_NUMLOCK ? 2 : 3);
            return 0;
        case QB64_SYS_CLIPBOARD: {
            char *text = graphics_clipboard_get();
            if (text) {
                basic_string_append(out, text, strlen(text));
                graphics_clipboard_free(text);
            }
            return 0;
        }
        case QB64_FILE_FILEEXISTS:
            *number = path_is_file(path) ? -1 : 0;
            return 0;
        case QB64_FILE_DIREXISTS:
            *number = path_is_dir(path) ? -1 : 0;
            return 0;
        case QB64_FILE_DIR:
            qb64_special_folder(path, out);
            return 0;
        case QB64_FILE_CWD: {
            char cwd[4096];
            if (!getcwd(cwd, sizeof(cwd))) return ERR_INTERNAL_ERROR;
            append_folder(out, cwd);
            return 0;
        }
        case QB64_FILE_STARTDIR:
            append_folder(out, qb64_start_dir[0] ? qb64_start_dir : ".");
            return 0;
        case QB64_FILE_FULLPATH: {
            char full[PATH_MAX];
            if (!path[0] || !realpath(path, full)) return ERR_PATH_NOT_FOUND;
            if (path_is_dir(full)) append_folder(out, full);
            else basic_string_append(out, full, strlen(full));
            return 0;
        }
        case QB64_FILE_FILES:
            if (arg) qb64_files_start(path);
            else if (!qb64_files.started) return ERR_ILLEGAL_FUNCTION_CALL;
            if (qb64_files.next < qb64_files.count) {
                const char *name = qb64_files.names[qb64_files.next++];
                basic_string_append(out, name, strlen(name));
            }
            return 0;
        case QB64_FILE_READFILE: {
            if (!path[0]) return ERR_BAD_FILE_NAME;
            if (path_is_dir(path)) return ERR_PATH_FILE_ACCESS_ERROR;
            FILE *f = fopen(path, "rb");
            if (!f) return errno == ENOENT ? ERR_FILE_NOT_FOUND : ERR_PATH_FILE_ACCESS_ERROR;
            char chunk[65536];
            size_t got;
            while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                if (!basic_string_append(out, chunk, got)) { fclose(f); return ERR_OUT_OF_MEMORY; }
            }
            fclose(f);
            return 0;
        }
    }
    return ERR_SYNTAX_ERROR;
}

/* Calls file function id from tokens, with ts just past its name, reading
 * the "(arg$)" after it if it has one. Errors are reported here. */
static void call_qb64_file_function_tok(TokenStream *ts, int id, double *number, BasicString *out) {
    BasicString arg = {0};
    int have_arg = 0;
    if (qb64_file_function_takes_argument(id) && ts->tokens[ts->pos].type == TOKEN_LPAREN) {
        ts->pos++;
        if (ts->tokens[ts->pos].type != TOKEN_RPAREN) {
            parse_string_expression_tok_heap(ts, &arg);
            have_arg = 1;
        }
        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        else if (!runtime_error_occurred) report_runtime_error(ERR_SYNTAX_ERROR);
    }
    if (!runtime_error_occurred) {
        if (!have_arg && id != QB64_FILE_FILES && qb64_file_function_takes_argument(id)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
        } else {
            int err = qb64_file_function(id, have_arg ? &arg : NULL, number, out);
            if (err) report_runtime_error(err);
        }
    }
    basic_string_release(&arg);
}

/* The same, reading the argument from text, with *input just past the name. */
static void call_qb64_file_function_text(const char **input, int id, double *number, BasicString *out) {
    BasicString arg = {0};
    int have_arg = 0;
    const char *saved = *input;
    if (qb64_file_function_takes_argument(id) && get_next_token(input).type == TOKEN_LPAREN) {
        const char *inner = *input;
        if (get_next_token(input).type != TOKEN_RPAREN) {
            *input = inner;
            parse_string_expression_heap(input, &arg);
            have_arg = 1;
            if (get_next_token(input).type != TOKEN_RPAREN && !runtime_error_occurred) {
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
        }
    } else {
        *input = saved;
    }
    if (!runtime_error_occurred) {
        if (!have_arg && id != QB64_FILE_FILES && qb64_file_function_takes_argument(id)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
        } else {
            int err = qb64_file_function(id, have_arg ? &arg : NULL, number, out);
            if (err) report_runtime_error(err);
        }
    }
    basic_string_release(&arg);
}

static void format_number_plain(Num value, int is_double, char *out, size_t size);
static int is_string_member_reference_text(const char *text);

/* ---- QB64 string functions: _TRIM$, _TOSTR$, _BIN$, _INSTRREV, _STRCMP,
 * _STRICMP and _SHELLHIDE. Each parser reads the arguments into a
 * QB64StringArgs (strings and numbers as they come) and qb64_string_function
 * does the rest. */
enum {
    QB64_STR_NONE, QB64_STR_TRIM, QB64_STR_TOSTR, QB64_STR_BIN,
    QB64_STR_INSTRREV, QB64_STR_STRCMP, QB64_STR_STRICMP,
    QB64_STR_UPRINTWIDTH, QB64_STR_UCHARPOS, QB64_STR_SHELLHIDE
};

static int qb64_string_function_id(const char *name) {
    static const struct { const char *name; int id; } functions[] = {
        {"_TRIM$", QB64_STR_TRIM}, {"_TOSTR$", QB64_STR_TOSTR}, {"_BIN$", QB64_STR_BIN},
        {"_INSTRREV", QB64_STR_INSTRREV}, {"_STRCMP", QB64_STR_STRCMP}, {"_STRICMP", QB64_STR_STRICMP},
        {"_UPRINTWIDTH", QB64_STR_UPRINTWIDTH}, {"_UCHARPOS", QB64_STR_UCHARPOS},
        {"_SHELLHIDE", QB64_STR_SHELLHIDE},
    };
    if (name[0] != '_') return QB64_STR_NONE;
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        if (strcasecmp(name, functions[i].name) == 0) return functions[i].id;
    }
    return QB64_STR_NONE;
}

/* _TRIM$, _TOSTR$ and _BIN$ give a string; the others a number. */
static int qb64_string_function_is_string(int id) {
    return id == QB64_STR_TRIM || id == QB64_STR_TOSTR || id == QB64_STR_BIN;
}

#define QB64_STRING_MAX_ARGS 4
typedef struct {
    int count;
    int is_string[QB64_STRING_MAX_ARGS];
    BasicString text[QB64_STRING_MAX_ARGS];
    Num number[QB64_STRING_MAX_ARGS];
    int number_is_double[QB64_STRING_MAX_ARGS]; // for _TOSTR$, as STR$ shows it
    int is_array[QB64_STRING_MAX_ARGS];         // _UCHARPOS's array argument,
    int array_var[QB64_STRING_MAX_ARGS];        // its variable
    int array_start[QB64_STRING_MAX_ARGS];      // and the first element to fill
} QB64StringArgs;

static void qb64_string_args_release(QB64StringArgs *args) {
    for (int i = 0; i < args->count && i < QB64_STRING_MAX_ARGS; i++) basic_string_release(&args->text[i]);
}

static const char *qb64_arg_text(const QB64StringArgs *args, int i, size_t *length) {
    *length = args->text[i].length;
    return args->text[i].data ? (const char *)args->text[i].data : "";
}

/* -1, 0 or 1 as a and b compare byte by byte, ignoring case if asked. */
static int qb64_compare_strings(const char *a, size_t na, const char *b, size_t nb, int ignore_case) {
    size_t n = na < nb ? na : nb;
    for (size_t i = 0; i < n; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (ignore_case) { x = tolower(x); y = tolower(y); }
        if (x != y) return x < y ? -1 : 1;
    }
    return na < nb ? -1 : na > nb ? 1 : 0;
}

/* Runs a string function on its arguments. A number goes in *number, a
 * string in *out. Returns 0 or the error to report. */
static int qb64_string_function(int id, const QB64StringArgs *args, Num *number, BasicString *out) {
    size_t n0, n1;
    // The number of arguments, and which of them are strings.
    switch (id) {
        case QB64_STR_TRIM: {
            if (args->count != 1 || !args->is_string[0]) return ERR_TYPE_MISMATCH;
            // Spaces only, as LTRIM$(RTRIM$(text$)).
            const char *s = qb64_arg_text(args, 0, &n0);
            size_t start = 0, end = n0;
            while (start < end && s[start] == ' ') start++;
            while (end > start && s[end - 1] == ' ') end--;
            basic_string_append(out, s + start, end - start);
            return 0;
        }
        case QB64_STR_TOSTR: {
            if (args->count < 1 || args->count > 2 || args->is_string[0] ||
                (args->count == 2 && args->is_string[1])) return ERR_TYPE_MISMATCH;
            Num value = args->number[0];
            int is_double = args->number_is_double[0];
            char buf[96];
            if (args->count == 2 && !num_is_int(value)) {
                // At most `digits` significant digits, rounded, trailing
                // zeros dropped; SINGLE allows 1-7 and DOUBLE 1-16.
                int64_t digits;
                if (!num_whole_operand(args->number[1], &digits)) return ERR_OVERFLOW;
                if (digits < 0) return ERR_ILLEGAL_FUNCTION_CALL;
                int most = is_double ? 16 : 7;
                if (digits < 1) digits = 1;
                if (digits > most) digits = most;
                char raw[64];
                snprintf(raw, sizeof(raw), "%.*g", (int)digits, value.d);
                const char *p = raw;
                size_t k = 0;
                if (*p == '-') { buf[k++] = '-'; p++; }
                if (p[0] == '0' && p[1] == '.') p++;
                for (; *p && k + 1 < sizeof(buf); p++) buf[k++] = *p == 'e' ? (is_double ? 'D' : 'E') : *p;
                buf[k] = '\0';
            } else {
                format_number_plain(value, is_double, buf, sizeof(buf));
            }
            basic_string_append(out, buf, strlen(buf));
            return 0;
        }
        case QB64_STR_BIN: {
            if (args->count != 1 || args->is_string[0]) return ERR_TYPE_MISMATCH;
            // Like HEX$: negatives in two's complement at their type's
            // width; floats keep only their whole part, as in QB64.
            Num value = args->number[0];
            int64_t whole;
            int kind = value.kind;
            if (num_is_int(value)) {
                whole = value.i;
            } else {
                double t = trunc(value.d);
                if (!(t >= -9223372036854775808.0 && t < 9223372036854775808.0)) return ERR_OVERFLOW;
                whole = (int64_t)t;
                kind = whole >= INT32_MIN && whole <= INT32_MAX ? NUM_LONG : NUM_INT64;
                if (whole >= -32768 && whole < 0) kind = NUM_INTEGER;
            }
            uint64_t bits = (uint64_t)whole;
            if (whole < 0 && kind == NUM_INTEGER) bits &= 0xFFFF;
            else if (whole < 0 && kind == NUM_LONG) bits &= 0xFFFFFFFFu;
            char buf[65];
            int k = 64;
            buf[k] = '\0';
            do { buf[--k] = (char)('0' + (bits & 1)); bits >>= 1; } while (bits);
            basic_string_append(out, buf + k, (size_t)(64 - k));
            return 0;
        }
        case QB64_STR_INSTRREV: {
            // _INSTRREV([start%,] base$, find$): the last place find$
            // starts at or before start% (the end when start% <= 0), or 0.
            int first = args->count == 3 ? 1 : 0;
            if (args->count < 2 || args->count > 3 || !args->is_string[first] || !args->is_string[first + 1] ||
                (first && args->is_string[0])) return ERR_TYPE_MISMATCH;
            const char *base = qb64_arg_text(args, first, &n0);
            const char *find = qb64_arg_text(args, first + 1, &n1);
            int64_t start = 0;
            if (first && !num_whole_operand(args->number[0], &start)) return ERR_OVERFLOW;
            if (n0 == 0) { *number = num_i(0, NUM_LONG); return 0; }
            if (n1 == 0) { *number = num_i((int64_t)n0, NUM_LONG); return 0; }
            if (n1 > n0) { *number = num_i(0, NUM_LONG); return 0; }
            int64_t from = (int64_t)(n0 - n1) + 1;
            if (start > 0 && start < from) from = start;
            for (int64_t pos = from; pos >= 1; pos--) {
                if (memcmp(base + pos - 1, find, n1) == 0) { *number = num_i(pos, NUM_LONG); return 0; }
            }
            *number = num_i(0, NUM_LONG);
            return 0;
        }
        case QB64_STR_STRCMP:
        case QB64_STR_STRICMP: {
            if (args->count != 2 || !args->is_string[0] || !args->is_string[1]) return ERR_TYPE_MISMATCH;
            const char *a = qb64_arg_text(args, 0, &n0);
            const char *b = qb64_arg_text(args, 1, &n1);
            *number = num_i(qb64_compare_strings(a, n0, b, n1, id == QB64_STR_STRICMP), NUM_INTEGER);
            return 0;
        }
        case QB64_STR_SHELLHIDE: {
            // _SHELLHIDE(command$): runs the command without showing its
            // output and gives its exit code.
            if (args->count != 1 || !args->is_string[0]) return ERR_TYPE_MISMATCH;
            const char *cmd = qb64_arg_text(args, 0, &n0);
            char full[BASIC_STRING_MAX + 32];
            static const char redirect[] = " > /dev/null 2>&1";
            if (n0 > BASIC_STRING_MAX) n0 = BASIC_STRING_MAX;
            memcpy(full, cmd, n0);
            memcpy(full + n0, redirect, sizeof(redirect));
            fflush(stdout);
            int status = system(full);
            int code = status;
            if (status > 0 && WIFEXITED(status)) code = WEXITSTATUS(status);
            *number = num_i(code, NUM_INTEGER);
            return 0;
        }
        case QB64_STR_UPRINTWIDTH:
        case QB64_STR_UCHARPOS: {
            // _UPRINTWIDTH(text$[, encoding][, font]) and
            // _UCHARPOS(text$[, positions&()][, encoding][, font]).
            if (args->count < 1 || !args->is_string[0]) return ERR_TYPE_MISMATCH;
            int first = 1;
            if (id == QB64_STR_UCHARPOS && args->count > 1 && args->is_array[1]) first = 2;
            if (args->count > first + 2) return ERR_SYNTAX_ERROR;
            int64_t encoding = 0, font_handle = -1;
            for (int i = 1; i < args->count; i++) {
                if (args->is_string[i]) return ERR_TYPE_MISMATCH;
                if (args->is_array[i]) continue;
                int64_t *dest = i == first ? &encoding : &font_handle;
                if (!num_whole_operand(args->number[i], dest)) return ERR_OVERFLOW;
            }
            if (!graphics_is_active()) init_graphics();
            const unsigned char *text = (const unsigned char *)qb64_arg_text(args, 0, &n0);
            if (id == QB64_STR_UPRINTWIDTH) {
                int w = graphics_uprintwidth(text, n0, (int)encoding, (int)font_handle);
                if (w < 0) return ERR_ILLEGAL_FUNCTION_CALL;
                *number = num_i(w, NUM_LONG);
                return 0;
            }
            int *positions = NULL;
            int room = 0;
            Variable *array = NULL;
            if (first == 2) {
                array = get_variable_ptr(args->array_var[1]);
                room = array->array_size - args->array_start[1];
                if (!array->iarray || room < 0) return ERR_SUBSCRIPT_OUT_OF_RANGE;
                if ((size_t)room > n0 + 1) room = (int)n0 + 1;
                positions = calloc((size_t)room + 1, sizeof(int));
                if (!positions) return ERR_OUT_OF_MEMORY;
            }
            int count = graphics_ucharpos(text, n0, (int)encoding, (int)font_handle, positions, room);
            if (count < 0) { free(positions); return ERR_ILLEGAL_FUNCTION_CALL; }
            if (positions && count > 0) {
                for (int i = 0; i <= count && i < room; i++)
                    store_num(array, args->array_start[1] + i, num_i(positions[i], NUM_LONG));
            }
            free(positions);
            *number = num_i(count, NUM_LONG);
            return 0;
        }
    }
    return ERR_SYNTAX_ERROR;
}

/* Reads "(arg, arg, ...)" of a string function from tokens, ts just past
 * its name, then runs it. Errors are reported here. */
static void call_qb64_string_function_tok(TokenStream *ts, int id, Num *number, BasicString *out) {
    QB64StringArgs args = {0};
    if (ts->tokens[ts->pos].type != TOKEN_LPAREN) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return;
    }
    ts->pos++;
    while (!runtime_error_occurred) {
        if (args.count >= QB64_STRING_MAX_ARGS) { report_runtime_error(ERR_SYNTAX_ERROR); break; }
        int i = args.count++;
        args.is_string[i] = is_string_token(&ts->tokens[ts->pos]) ||
                            is_string_member_reference_text(ts->tokens[ts->pos].start_ptr);
        if (id == QB64_STR_UCHARPOS && i == 1 && ts->tokens[ts->pos].type == TOKEN_IDENTIFIER &&
            ts->tokens[ts->pos + 1].type == TOKEN_LPAREN && !args.is_string[i]) {
            // An array (name() or name(index)) that receives the positions.
            int var = resolve_token_variable(&ts->tokens[ts->pos]);
            if (var < 0) { report_runtime_error(ERR_SYNTAX_ERROR); break; }
            ts->pos++;
            if (ts->tokens[ts->pos + 1].type == TOKEN_RPAREN) {
                ts->pos += 2;
                args.array_start[i] = 0;
            } else {
                int linear = parse_array_index_tok(ts, var);
                if (linear < 0 || runtime_error_occurred) break;
                args.array_start[i] = linear;
            }
            args.is_array[i] = 1;
            args.array_var[i] = var;
        } else if (args.is_string[i]) {
            parse_string_expression_tok_heap(ts, &args.text[i]);
        } else {
            last_expression_is_double = 0;
            args.number[i] = evaluate_num_tok(ts);
            args.number_is_double[i] = last_expression_is_double;
        }
        if (ts->tokens[ts->pos].type != TOKEN_COMMA) break;
        ts->pos++;
    }
    if (!runtime_error_occurred) {
        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        else report_runtime_error(ERR_SYNTAX_ERROR);
    }
    if (!runtime_error_occurred) {
        int err = qb64_string_function(id, &args, number, out);
        if (err) report_runtime_error(err);
    }
    qb64_string_args_release(&args);
}

/* The same, reading the arguments from text, *input just past the name. */
static void call_qb64_string_function_text(const char **input, int id, Num *number, BasicString *out) {
    QB64StringArgs args = {0};
    if (get_next_token(input).type != TOKEN_LPAREN) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return;
    }
    while (!runtime_error_occurred) {
        if (args.count >= QB64_STRING_MAX_ARGS) { report_runtime_error(ERR_SYNTAX_ERROR); break; }
        int i = args.count++;
        const char *peek = *input;
        Token next = get_next_token(&peek);
        args.is_string[i] = is_string_token(&next) || is_string_member_reference_text(*input);
        if (args.is_string[i]) {
            parse_string_expression_heap(input, &args.text[i]);
        } else {
            last_expression_is_double = 0;
            args.number[i] = evaluate_num_text(input);
            args.number_is_double[i] = last_expression_is_double;
        }
        const char *sep = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_COMMA) continue;
        if (t.type != TOKEN_RPAREN) { *input = sep; report_runtime_error(ERR_SYNTAX_ERROR); }
        break;
    }
    if (!runtime_error_occurred) {
        int err = qb64_string_function(id, &args, number, out);
        if (err) report_runtime_error(err);
    }
    qb64_string_args_release(&args);
}

/* _WRITEFILE file$, contents$: replaces the file with the string's bytes. */
static int qb64_write_file(const BasicString *name, const BasicString *contents) {
    const char *path = name->data ? (const char *)name->data : "";
    if (!path[0]) return ERR_BAD_FILE_NAME;
    FILE *f = fopen(path, "wb");
    if (!f) return errno == ENOENT ? ERR_PATH_NOT_FOUND : ERR_PATH_FILE_ACCESS_ERROR;
    size_t n = contents->length;
    int ok = n == 0 || fwrite(contents->data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : ERR_PATH_FILE_ACCESS_ERROR;
}

static int parse_string_expression_tok_heap(TokenStream *ts, BasicString *out) {
    int parsed = 0;

    while (1) {
        BasicString term = {0};
        Token t = ts->tokens[ts->pos];

        if (t.type == TOKEN_CHR || t.type == TOKEN_TAB) {
            ts->pos++;
            ts->pos++;
            double arg = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (t.type == TOKEN_CHR) {
                char c = (char)(round_to_int(arg) & 0xFF);
                basic_string_append(&term, &c, 1);
            } else {
                int w = round_to_int(arg); if (w < 0) w = 0; if (w >= 255) w = 255;
                char *block = malloc((size_t)(w > 0 ? w : 1));
                if (!block) return 0;
                memset(block, ' ', (size_t)w);
                basic_string_append(&term, block, (size_t)w);
                free(block);
            }
        } else if (t.type == TOKEN_LEFT || t.type == TOKEN_RIGHT || t.type == TOKEN_MID) {
            TokenType ft = t.type;
            ts->pos++;
            ts->pos++;
            BasicString base = {0};
            if (!parse_string_expression_tok_heap(ts, &base)) { basic_string_release(&base); return 0; }
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
            int n1 = evaluate_int_tok(ts);
            int n2 = -1;
            if (ft == TOKEN_MID && ts->tokens[ts->pos].type == TOKEN_COMMA) {
                ts->pos++;
                n2 = evaluate_int_tok(ts);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            int slen = (int)base.length;
            if (ft == TOKEN_LEFT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                if (cnt > 0) basic_string_append(&term, base.data, (size_t)cnt);
            } else if (ft == TOKEN_RIGHT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                if (cnt > 0) basic_string_append(&term, base.data + (slen - cnt > 0 ? slen - cnt : 0), (size_t)cnt);
            } else {
                int start = n1 - 1; if (start < 0) start = 0;
                if (start < slen) {
                    int cnt = (n2 == -1) ? (slen - start) : n2;
                    if (cnt < 0) cnt = 0; if (start + cnt > slen) cnt = slen - start;
                    if (cnt > 0) basic_string_append(&term, base.data + start, (size_t)cnt);
                }
            }
            basic_string_release(&base);
        } else if (t.type == TOKEN_SPACE || t.type == TOKEN_SPC) {
            ts->pos++;
            ts->pos++;
            int n = evaluate_int_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (n < 0) n = 0;
            if (!basic_string_append_repeat(out, ' ', (size_t)n)) return 0;
        } else if (t.type == TOKEN_STRING_FUNC) {
            ts->pos++;
            ts->pos++;
            int n = evaluate_int_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
            char c = ' ';
            BasicString arg_buf = {0};
            if (is_string_token(&ts->tokens[ts->pos])) {
                if (!parse_string_expression_tok_heap(ts, &arg_buf)) { basic_string_release(&arg_buf); return 0; }
                if (arg_buf.length > 0) c = arg_buf.data[0];
            } else {
                c = (char)evaluate_expression_tok(ts);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (n < 0) n = 0;
            int repeated = basic_string_append_repeat(out, c, (size_t)n);
            basic_string_release(&arg_buf);
            if (!repeated) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_DEFLATE || t.type == TOKEN_INFLATE) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) ts->pos++;
            BasicString input = {0};
            if (!parse_string_expression_tok_heap(ts, &input)) { basic_string_release(&input); return 0; }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            BasicString output = {0};
            int ok = t.type == TOKEN_DEFLATE
                ? basic_string_deflate(&input, &output)
                : basic_string_inflate(&input, &output);
            if (ok) basic_string_append(&term, output.data, output.length);
            basic_string_release(&output);
            basic_string_release(&input);
        } else if (t.type == TOKEN_COMMANDS) {
            ts->pos++;
            basic_string_append(&term, internal_command_line, strlen(internal_command_line));
        } else if (t.type == TOKEN_ARGVS) {
            ts->pos++;
            ts->pos++;
            int idx = evaluate_int_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (idx >= 0 && idx < internal_argc && internal_argv) {
                basic_string_append(&term, internal_argv[idx], strlen(internal_argv[idx]));
            }
        } else if (t.type == TOKEN_GETS) {
            ts->pos++;
            ts->pos++;
            int fnum = -1;
            if (ts->tokens[ts->pos].type == TOKEN_HASH) ts->pos++;
            fnum = evaluate_int_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
            int rec = evaluate_int_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
            int len = evaluate_int_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                report_runtime_error(ERR_BAD_FILE_NUMBER);
                return 0;
            }
            long offset = (long)((rec - 1) * len);
            fseek(file_handles[fnum], offset, SEEK_SET);
            char buf[BASIC_STRING_MAX];
            size_t r = fread(buf, 1, len, file_handles[fnum]);
            if (r < (size_t)len) {
                for (size_t i = r; i < (size_t)len; i++) buf[i] = ' ';
            }
            buf[len] = '\0';
            int trim = len - 1;
            while (trim >= 0 && buf[trim] == ' ') { buf[trim] = '\0'; trim--; }
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_INPUTS) {
            // INPUT$(n[, [#]file])
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) ts->pos++;
            int count = evaluate_int_tok(ts);
            int fnum = -1;
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
                ts->pos++;
                if (ts->tokens[ts->pos].type == TOKEN_HASH) ts->pos++;
                fnum = evaluate_int_tok(ts);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (!read_input_chars(count, fnum, &term)) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_INKEY) {
            ts->pos++;
            if (graphics_is_active()) {
                update_graphics();
                int c = get_graphics_char();
                if (c) basic_string_append(&term, (char[]){(char)c, '\0'}, 1);
            } else {
                int c = get_stdin_char();
                if (c) basic_string_append(&term, (char[]){(char)c, '\0'}, 1);
            }
        } else if (t.type == TOKEN_UCASE || t.type == TOKEN_LCASE || t.type == TOKEN_TRIM || t.type == TOKEN_LTRIM || t.type == TOKEN_RTRIM) {
            TokenType ft = t.type;
            ts->pos++;
            ts->pos++;
            BasicString base = {0};
            if (!parse_string_expression_tok_heap(ts, &base)) { basic_string_release(&base); return 0; }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (ft == TOKEN_UCASE) {
                for (int i = 0; i < (int)base.length; i++) base.data[i] = toupper((unsigned char)base.data[i]);
            } else if (ft == TOKEN_LCASE) {
                for (int i = 0; i < (int)base.length; i++) base.data[i] = tolower((unsigned char)base.data[i]);
            } else if (ft == TOKEN_TRIM) {
                int start = 0, end = (int)base.length - 1;
                while (start <= end && isspace((unsigned char)base.data[start])) start++;
                while (end >= start && isspace((unsigned char)base.data[end])) end--;
                int len = (end >= start) ? (end - start + 1) : 0;
                if (len > 0) memmove(base.data, base.data + start, (size_t)len);
                base.data[len] = '\0';
                base.length = (size_t)len;
            } else if (ft == TOKEN_LTRIM) {
                int start = 0;
                while (start < (int)base.length && isspace((unsigned char)base.data[start])) start++;
                if (start > 0) {
                    int len = (int)base.length - start;
                    memmove(base.data, base.data + start, (size_t)len);
                    base.data[len] = '\0';
                    base.length = (size_t)len;
                }
            } else if (ft == TOKEN_RTRIM) {
                int end = (int)base.length - 1;
                while (end >= 0 && isspace((unsigned char)base.data[end])) end--;
                base.data[end + 1] = '\0';
                base.length = (size_t)(end + 1);
            }
            basic_string_append(&term, base.data, base.length);
            basic_string_release(&base);
        } else if (t.type == TOKEN_HEX || t.type == TOKEN_OCT) {
            ts->pos++;
            ts->pos++;
            Num val = evaluate_num_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            char buf[128];
            format_radix_string(val, t.type == TOKEN_HEX, buf, sizeof(buf));
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_TIME || t.type == TOKEN_DATE) {
            ts->pos++;
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            char buf[128];
            if (t.type == TOKEN_TIME) strftime(buf, sizeof(buf), "%H:%M:%S", timeinfo);
            else strftime(buf, sizeof(buf), "%m-%d-%Y", timeinfo);
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_ENVIRON) {
            ts->pos++;
            ts->pos++;
            BasicString arg_val = {0};
            int is_str = is_string_token(&ts->tokens[ts->pos]);
            if (is_str) {
                if (!parse_string_expression_tok_heap(ts, &arg_val)) { basic_string_release(&arg_val); return 0; }
                char *ev = getenv((const char *)arg_val.data);
                if (ev) basic_string_append(&term, ev, strlen(ev));
            } else {
                int idx = evaluate_int_tok(ts);
                extern char **environ;
                if (environ && idx > 0) {
                    int count = 1;
                    for (char **e = environ; *e; e++, count++) {
                        if (count == idx) {
                            basic_string_append(&term, *e, strlen(*e));
                            break;
                        }
                    }
                }
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            basic_string_release(&arg_val);
        } else if (t.type == TOKEN_STR) {
            ts->pos++;
            ts->pos++;
            last_expression_is_double = 0;
            Num val = evaluate_num_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            char buf[128];
            format_str_function(val, buf, sizeof(buf));
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_MKI || t.type == TOKEN_MKL || t.type == TOKEN_MKS || t.type == TOKEN_MKD) {
            ts->pos++;
            ts->pos++;
            Num val = evaluate_num_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            append_number_bytes(&term, t.type, val);
        } else if (t.type == TOKEN_STRING) {
            basic_string_append(&term, t.text, strlen(t.text));
            ts->pos++;
        } else if (t.type == TOKEN_IDENTIFIER && t.text[0] == '_' && qb64_file_function_id(t.text) &&
                   qb64_file_function_is_string(qb64_file_function_id(t.text))) {
            ts->pos++;
            double unused = 0;
            call_qb64_file_function_tok(ts, qb64_file_function_id(t.text), &unused, &term);
            if (runtime_error_occurred) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_IDENTIFIER && t.text[0] == '_' && qb64_string_function_id(t.text) &&
                   qb64_string_function_is_string(qb64_string_function_id(t.text))) {
            ts->pos++;
            Num unused;
            call_qb64_string_function_tok(ts, qb64_string_function_id(t.text), &unused, &term);
            if (runtime_error_occurred) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_IDENTIFIER) {
            ProcedureDef *pfunc = find_procedure(t.text);
            if (pfunc && pfunc->is_function) {
                ts->pos++;
                evaluate_function_call_string(pfunc, ts, &term);
            } else {
                int token_index = ts->pos++;
                int idx = -1;
                int array_idx = -1;
                int is_member = user_type_count > 0
                    ? resolve_user_type_reference_tok(ts, &ts->tokens[token_index],
                                                      &idx, &array_idx)
                    : 0;
                if (is_member < 0) return 0;
                if (is_member && is_string_var(get_variable_ptr(idx)->name)) {
                    append_string_variable_value(out, idx, array_idx);
                } else if (!is_member && is_string_var(t.text)) {
                    ts->pos = token_index;
                    idx = resolve_token_variable(&ts->tokens[ts->pos]);
                    ts->pos++;
                    array_idx = parse_array_index_tok(ts, idx);
                    append_string_variable_value(out, idx, array_idx);
                } else {
                    ts->pos = token_index;
                    if (parsed) break;
                    double val = evaluate_expression_tok(ts);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%g", val);
                    basic_string_append(&term, buf, strlen(buf));
                }
            }
        } else {
            if (parsed) break;
            double val = evaluate_expression_tok(ts);
            char buf[128];
            snprintf(buf, sizeof(buf), "%g", val);
            basic_string_append(&term, buf, strlen(buf));
            parsed = 1;
            if (ts->tokens[ts->pos].type != TOKEN_PLUS) break;
            ts->pos++;
            continue;
        }

        parsed = 1;
        if (!term.length && t.type == TOKEN_STRING && strcmp(t.text, "") == 0) {
            // allow empty strings as valid terms
        }
        if (term.data && !out->data && !out->length && !out->capacity) {
            /* First term built a fresh buffer: adopt it instead of copying. */
            *out = term;
        } else {
            basic_string_append(out, term.data, term.length);
            basic_string_release(&term);
        }

        if (ts->tokens[ts->pos].type != TOKEN_PLUS) {
            break;
        }
        ts->pos++;
    }
    return parsed;
}

static int parse_string_expression_tok(TokenStream *ts, char *out, int out_size) {
    BasicString *heap_out = basic_string_create(NULL, 0);
    if (!heap_out) {
        out[0] = '\0';
        return 0;
    }
    int ok = parse_string_expression_tok_heap(ts, heap_out);
    basic_string_to_buffer(heap_out, out, out_size);
    basic_string_destroy(heap_out);
    return ok;
}

static int parse_string_expression_heap(const char **input, BasicString *out) {
    int parsed = 0;

    while (1) {
        BasicString term = {0};
        const char *saved = *input;
        Token t = get_next_token(input);

        if (t.type == TOKEN_CHR || t.type == TOKEN_TAB) {
            get_next_token(input);
            double arg = evaluate_expression(input);
            get_next_token(input);
            if (t.type == TOKEN_CHR) {
                char c = (char)(round_to_int(arg) & 0xFF);
                basic_string_append(&term, &c, 1);
            } else {
                int w = round_to_int(arg); if (w < 0) w = 0; if (w >= 255) w = 255;
                char *block = malloc((size_t)(w > 0 ? w : 1));
                if (!block) return 0;
                memset(block, ' ', (size_t)w);
                basic_string_append(&term, block, (size_t)w);
                free(block);
            }
        } else if (t.type == TOKEN_LEFT || t.type == TOKEN_RIGHT || t.type == TOKEN_MID) {
            TokenType ft = t.type;
            get_next_token(input);
            BasicString base = {0};
            if (!parse_string_expression_heap(input, &base)) { basic_string_release(&base); return 0; }
            get_next_token(input);
            int n1 = evaluate_int_text(input);
            int n2 = -1;
            if (ft == TOKEN_MID) {
                const char *comma_saved = *input;
                if (get_next_token(input).type == TOKEN_COMMA) n2 = evaluate_int_text(input);
                else *input = comma_saved;
            }
            get_next_token(input);
            int slen = (int)base.length;
            if (ft == TOKEN_LEFT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                if (cnt > 0) basic_string_append(&term, base.data, (size_t)cnt);
            } else if (ft == TOKEN_RIGHT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                if (cnt > 0) basic_string_append(&term, base.data + (slen - cnt > 0 ? slen - cnt : 0), (size_t)cnt);
            } else {
                int start = n1 - 1; if (start < 0) start = 0;
                if (start < slen) {
                    int cnt = (n2 == -1) ? (slen - start) : n2;
                    if (cnt < 0) cnt = 0; if (start + cnt > slen) cnt = slen - start;
                    if (cnt > 0) basic_string_append(&term, base.data + start, (size_t)cnt);
                }
            }
            basic_string_release(&base);
        } else if (t.type == TOKEN_SPACE || t.type == TOKEN_SPC) {
            get_next_token(input);
            int n = evaluate_int_text(input);
            get_next_token(input);
            if (n < 0) n = 0;
            if (!basic_string_append_repeat(out, ' ', (size_t)n)) return 0;
        } else if (t.type == TOKEN_STRING_FUNC) {
            get_next_token(input);
            int n = evaluate_int_text(input);
            get_next_token(input);
            char c = ' ';
            BasicString arg_buf = {0};
            const char *arg_saved = *input;
            Token arg_token = get_next_token(input);
            if (arg_token.type == TOKEN_NUMBER) {
                c = (char)((int)arg_token.double_val & 0xFF);
            } else if (arg_token.type == TOKEN_STRING) {
                if (arg_token.text[0]) c = arg_token.text[0];
            } else {
                *input = arg_saved;
                if (parse_string_expression_heap(input, &arg_buf) && arg_buf.length > 0) {
                    c = arg_buf.data[0];
                }
            }
            if (arg_token.type == TOKEN_EOF) {
                *input = arg_saved;
            } else {
                get_next_token(input);
            }
            if (n < 0) n = 0;
            int repeated = basic_string_append_repeat(out, c, (size_t)n);
            basic_string_release(&arg_buf);
            if (!repeated) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_DEFLATE || t.type == TOKEN_INFLATE) {
            get_next_token(input);
            BasicString source = {0};
            parse_string_expression_heap(input, &source);
            get_next_token(input);
            BasicString output = {0};
            int ok = t.type == TOKEN_DEFLATE
                ? basic_string_deflate(&source, &output)
                : basic_string_inflate(&source, &output);
            if (ok) basic_string_append(&term, output.data, output.length);
            basic_string_release(&output);
            basic_string_release(&source);
        } else if (t.type == TOKEN_COMMANDS) {
            basic_string_append(&term, internal_command_line, strlen(internal_command_line));
        } else if (t.type == TOKEN_ARGVS) {
            get_next_token(input);
            int idx = evaluate_int_text(input);
            get_next_token(input);
            if (idx >= 0 && idx < internal_argc && internal_argv) {
                basic_string_append(&term, internal_argv[idx], strlen(internal_argv[idx]));
            }
        } else if (t.type == TOKEN_GETS) {
            get_next_token(input);
            int fnum = -1;
            const char *saved_num = *input;
            Token hash_tok = get_next_token(input);
            if (hash_tok.type == TOKEN_HASH) {
                fnum = evaluate_int_text(input);
            } else {
                *input = saved_num;
                fnum = evaluate_int_text(input);
            }
            get_next_token(input);
            int rec = evaluate_int_text(input);
            get_next_token(input);
            int len = evaluate_int_text(input);
            get_next_token(input);
            if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                report_runtime_error(ERR_BAD_FILE_NUMBER);
                return 0;
            }
            long offset = (long)((rec - 1) * len);
            fseek(file_handles[fnum], offset, SEEK_SET);
            char buf[BASIC_STRING_MAX];
            size_t r = fread(buf, 1, len, file_handles[fnum]);
            if (r < (size_t)len) {
                for (size_t i = r; i < (size_t)len; i++) buf[i] = ' ';
            }
            buf[len] = '\0';
            int trim = len - 1;
            while (trim >= 0 && buf[trim] == ' ') { buf[trim] = '\0'; trim--; }
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_INPUTS) {
            // INPUT$(n[, [#]file])
            get_next_token(input);
            int count = evaluate_int_text(input);
            int fnum = -1;
            const char *saved = *input;
            if (get_next_token(input).type == TOKEN_COMMA) {
                const char *hash_saved = *input;
                if (get_next_token(input).type != TOKEN_HASH) *input = hash_saved;
                fnum = evaluate_int_text(input);
                saved = *input;
            }
            *input = saved;
            if (get_next_token(input).type != TOKEN_RPAREN) *input = saved;
            if (!read_input_chars(count, fnum, &term)) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_INKEY) {
            if (graphics_is_active()) {
                update_graphics();
                int c = get_graphics_char();
                if (c) basic_string_append(&term, (char[]){(char)c, '\0'}, 1);
            } else {
                int c = get_stdin_char();
                if (c) basic_string_append(&term, (char[]){(char)c, '\0'}, 1);
            }
        } else if (t.type == TOKEN_UCASE || t.type == TOKEN_LCASE || t.type == TOKEN_TRIM || t.type == TOKEN_LTRIM || t.type == TOKEN_RTRIM) {
            TokenType ft = t.type;
            get_next_token(input);
            BasicString base = {0};
            if (!parse_string_expression_heap(input, &base)) { basic_string_release(&base); return 0; }
            get_next_token(input);
            if (ft == TOKEN_UCASE) {
                for (int i = 0; i < (int)base.length; i++) base.data[i] = toupper((unsigned char)base.data[i]);
            } else if (ft == TOKEN_LCASE) {
                for (int i = 0; i < (int)base.length; i++) base.data[i] = tolower((unsigned char)base.data[i]);
            } else if (ft == TOKEN_TRIM) {
                int start = 0, end = (int)base.length - 1;
                while (start <= end && isspace((unsigned char)base.data[start])) start++;
                while (end >= start && isspace((unsigned char)base.data[end])) end--;
                int len = (end >= start) ? (end - start + 1) : 0;
                if (len > 0) memmove(base.data, base.data + start, (size_t)len);
                base.data[len] = '\0';
                base.length = (size_t)len;
            } else if (ft == TOKEN_LTRIM) {
                int start = 0;
                while (start < (int)base.length && isspace((unsigned char)base.data[start])) start++;
                if (start > 0) {
                    int len = (int)base.length - start;
                    memmove(base.data, base.data + start, (size_t)len);
                    base.data[len] = '\0';
                    base.length = (size_t)len;
                }
            } else if (ft == TOKEN_RTRIM) {
                int end = (int)base.length - 1;
                while (end >= 0 && isspace((unsigned char)base.data[end])) end--;
                base.data[end + 1] = '\0';
                base.length = (size_t)(end + 1);
            }
            basic_string_append(&term, base.data, base.length);
            basic_string_release(&base);
        } else if (t.type == TOKEN_HEX || t.type == TOKEN_OCT) {
            get_next_token(input);
            Num val = evaluate_num_text(input);
            get_next_token(input);
            char buf[128];
            format_radix_string(val, t.type == TOKEN_HEX, buf, sizeof(buf));
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_TIME || t.type == TOKEN_DATE) {
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            char buf[128];
            if (t.type == TOKEN_TIME) strftime(buf, sizeof(buf), "%H:%M:%S", timeinfo);
            else strftime(buf, sizeof(buf), "%m-%d-%Y", timeinfo);
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_ENVIRON) {
            get_next_token(input);
            char arg_val[BASIC_STRING_MAX] = "";
            int is_str = parse_string_expression(input, arg_val, sizeof(arg_val));
            if (is_str) {
                char *ev = getenv(arg_val);
                if (ev) basic_string_append(&term, ev, strlen(ev));
            } else {
                int idx = atoi(arg_val);
                extern char **environ;
                if (environ && idx > 0) {
                    int count = 1;
                    for (char **e = environ; *e; e++, count++) {
                        if (count == idx) {
                            basic_string_append(&term, *e, strlen(*e));
                            break;
                        }
                    }
                }
            }
            get_next_token(input);
        } else if (t.type == TOKEN_STR) {
            get_next_token(input);
            last_expression_is_double = 0;
            Num val = evaluate_num_text(input);
            get_next_token(input);
            char buf[128];
            format_str_function(val, buf, sizeof(buf));
            basic_string_append(&term, buf, strlen(buf));
        } else if (t.type == TOKEN_MKI || t.type == TOKEN_MKL || t.type == TOKEN_MKS || t.type == TOKEN_MKD) {
            get_next_token(input);
            Num val = evaluate_num_text(input);
            get_next_token(input);
            append_number_bytes(&term, t.type, val);
        } else if (t.type == TOKEN_STRING) {
            basic_string_append(&term, t.text, strlen(t.text));
        } else if (t.type == TOKEN_IDENTIFIER && t.text[0] == '_' && qb64_file_function_id(t.text) &&
                   qb64_file_function_is_string(qb64_file_function_id(t.text))) {
            double unused = 0;
            call_qb64_file_function_text(input, qb64_file_function_id(t.text), &unused, &term);
            if (runtime_error_occurred) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_IDENTIFIER && t.text[0] == '_' && qb64_string_function_id(t.text) &&
                   qb64_string_function_is_string(qb64_string_function_id(t.text))) {
            Num unused;
            call_qb64_string_function_text(input, qb64_string_function_id(t.text), &unused, &term);
            if (runtime_error_occurred) { basic_string_release(&term); return 0; }
        } else if (t.type == TOKEN_IDENTIFIER) {
            ProcedureDef *pfunc = find_procedure(t.text);
            if (pfunc && pfunc->is_function) {
                evaluate_function_call_string_text(pfunc, input, &term);
            } else {
                int idx = -1;
                int array_idx = -1;
                int is_member = user_type_count > 0
                    ? resolve_user_type_reference_text(input, &t, &idx, &array_idx)
                    : 0;
                if (is_member < 0) return 0;
                if (is_member && is_string_var(get_variable_ptr(idx)->name)) {
                    append_string_variable_value(out, idx, array_idx);
                } else if (!is_member && is_string_var(t.text)) {
                    idx = find_variable(t.text);
                    array_idx = parse_array_index(input, idx);
                    append_string_variable_value(out, idx, array_idx);
                } else {
                    *input = saved;
                    if (parsed) break;
                    double val = evaluate_expression(input);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%g", val);
                    basic_string_append(&term, buf, strlen(buf));
                }
            }
        } else {
            if (parsed) break;
            *input = saved;
            double val = evaluate_expression(input);
            char buf[128];
            snprintf(buf, sizeof(buf), "%g", val);
            basic_string_append(&term, buf, strlen(buf));
            parsed = 1;
        }

        parsed = 1;
        if (term.data && !out->data && !out->length && !out->capacity) {
            /* First term built a fresh buffer: adopt it instead of copying. */
            *out = term;
        } else {
            basic_string_append(out, term.data, term.length);
            basic_string_release(&term);
        }

        const char *separator = *input;
        if (get_next_token(input).type != TOKEN_PLUS) {
            *input = separator;
            break;
        }
    }
    return parsed;
}

static int parse_string_expression(const char **input, char *out, int out_size) {
    BasicString *heap_out = basic_string_create(NULL, 0);
    if (!heap_out) {
        out[0] = '\0';
        return 0;
    }
    int ok = parse_string_expression_heap(input, heap_out);
    basic_string_to_buffer(heap_out, out, out_size);
    basic_string_destroy(heap_out);
    return ok;
}

static void set_string_variable_with_align(int idx, int array_idx, const char *value, int right_justify) {
    int fnum = 0;
    FieldBinding binding;
    if (get_field_binding_for_var(idx, array_idx, &fnum, &binding)) {
        write_field_segment(fnum, &binding, value, right_justify);
        return;
    }

    if (array_idx >= 0) {
        if (!vars[idx].s_array || array_idx < 0 || array_idx >= vars[idx].array_size) return;
        assign_string_variable_value(idx, array_idx, value, strlen(value));
    } else {
        assign_string_variable_value(idx, array_idx, value, strlen(value));
    }
}

static void set_string_variable(int idx, int array_idx, const char *value) {
    set_string_variable_with_align(idx, array_idx, value, 0);
}

static void assign_input_value(int idx, int array_idx, int is_string, const char *value) {
    if (is_string) {
        if (array_idx >= 0 && vars[idx].s_array && array_idx < vars[idx].array_size) {
            assign_string_variable_value(idx, array_idx, value, strlen(value));
        } else {
            assign_string_variable_value(idx, array_idx, value, strlen(value));
        }
    } else {
        set_numeric_variable_num(idx, array_idx, parse_number_text(value));
    }
}

#define INPUT_LINE_MAX 4096
#define MAX_INPUT_FIELDS 16

/* Reads one line for INPUT/LINE INPUT from a file, the window or stdin.
 * Returns 0 at end of input. */
static int read_input_line(int fnum, char *line, size_t size) {
    line[0] = '\0';
    if (fnum != -1) {
        if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
            report_runtime_error(ERR_BAD_FILE_NUMBER);
            return 0;
        }
        if (!fgets(line, (int)size, file_handles[fnum])) {
            report_runtime_error(ERR_INPUT_PAST_END);
            return 0;
        }
    } else if (graphics_is_active()) {
        graphics_readline(line, (int)size);
    } else {
        fflush(stdout);
        if (!fgets(line, (int)size, stdin)) return 0;
    }
    line[strcspn(line, "\r\n")] = '\0';
    return 1;
}

/* INPUT$: appends count characters from a file or, without echo, from the
 * keyboard (Enter is CHR$(13)). Returns 0 on error. */
static int read_input_chars(int count, int fnum, BasicString *out) {
    if (count < 1 || count > 32767) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return 0;
    }
    if (fnum != -1) {
        if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
            report_runtime_error(ERR_BAD_FILE_NUMBER);
            return 0;
        }
        char buf[512];
        while (count > 0) {
            size_t want = count < (int)sizeof(buf) ? (size_t)count : sizeof(buf);
            size_t got = fread(buf, 1, want, file_handles[fnum]);
            if (got == 0) {
                report_runtime_error(ERR_INPUT_PAST_END);
                return 0;
            }
            basic_string_append(out, buf, got);
            count -= (int)got;
        }
        return 1;
    }
    while (count > 0 && !stop_running) {
        int c;
        if (graphics_is_active()) {
            c = get_graphics_char();
            if (!c) {
                graphics_sleep(5);
                continue;
            }
        } else {
            c = getchar();
            if (c == EOF) {
                report_runtime_error(ERR_INPUT_PAST_END);
                return 0;
            }
            if (c == '\n') c = 13;
        }
        char ch = (char)c;
        basic_string_append(out, &ch, 1);
        count--;
    }
    return 1;
}

/* Splits an INPUT reply at commas. A field may be a quoted string that
 * contains commas; spaces around unquoted fields are dropped. Returns the
 * number of fields, or -1 if a quoted field is followed by junk. */
static int split_input_fields(const char *line, char (*fields)[INPUT_LINE_MAX], int max_fields) {
    int count = 0;
    const char *p = line;
    while (1) {
        while (*p == ' ' || *p == '\t') p++;
        if (count >= max_fields) return count + 1;
        char *out = fields[count];
        size_t n = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && n < INPUT_LINE_MAX - 1) out[n++] = *p++;
            if (*p == '"') p++;
            while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != ',') return -1;
        } else {
            while (*p && *p != ',' && n < INPUT_LINE_MAX - 1) out[n++] = *p++;
            while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
        }
        out[n] = '\0';
        count++;
        if (*p != ',') break;
        p++;
    }
    return count;
}

/* True when text is a valid numeric INPUT reply (empty counts as 0). */
static int is_numeric_input(const char *text) {
    if (!*text) return 1;
    char *end = NULL;
    strtod(text, &end);
    while (end && (*end == ' ' || *end == '!' || *end == '#' || *end == '%' || *end == '&')) end++;
    return end && *end == '\0';
}

/* Formats a number the way PRINT does, without the surrounding spaces
 * (used by WRITE and STR$-style output). */
/* Formats a number as QBasic does: no leading zero before the point (.5,
 * -.25) and an uppercase exponent, E for SINGLE and D for DOUBLE (1E+20). */
static void format_number_plain(Num value, int is_double, char *out, size_t size) {
    if (num_is_int(value)) {
        format_num_integer(value, out, size);
        return;
    }
    char digits[64];
    snprintf(digits, sizeof(digits), "%.*g", is_double ? 16 : 7, value.d);
    const char *p = digits;
    size_t n = 0;
    if (*p == '-') {
        if (n + 1 < size) out[n++] = '-';
        p++;
    }
    if (p[0] == '0' && p[1] == '.') p++;
    for (; *p && n + 1 < size; p++) out[n++] = *p == 'e' ? (is_double ? 'D' : 'E') : *p;
    out[n] = '\0';
}

static void format_str_function(Num value, char *buf, size_t size) {
    char digits[64];
    format_number_plain(value, last_expression_is_double, digits, sizeof(digits));
    snprintf(buf, size, "%s%s", digits[0] == '-' ? "" : " ", digits);
}

/* ---- PRINT USING ----
 * The format is literal text with fields in it. Each value fills the next
 * field, the format starts over when the values outnumber its fields, and
 * the text after the last value's field is printed up to the next field.
 *   Strings:  !  first character    &  whole string    \  \  width of the backslashes
 *   Numbers:  #  digit   .  point   ,  (before the point) thousands commas
 *             +  sign first or last   -  last: minus sign   ^^^^ or ^^^^^  exponent
 *             **  fill with *   $$  dollar sign   **$  both
 *   _  prints the next character literally. A number too wide for its field
 *   prints in full after a %.
 * Basika extension: a format with no fields at all (an error in QBasic) may
 * be a C printf format with one conversion, such as "%6.2f" or "%s". */
enum { USING_NUMBER, USING_FIRST_CHAR, USING_WHOLE_STRING, USING_STRING_WIDTH };

typedef struct {
    int kind;
    int width;                      /* USING_STRING_WIDTH */
    int lead_plus, trail_plus, trail_minus;
    int asterisk, dollar, comma;
    int int_positions;              /* characters before the point, for **, $$, # and , */
    int has_point, decimals, carets;
} UsingField;

/* Length of the field starting at f (described in *field), or 0 if f is literal text. */
static int using_field_at(const char *f, UsingField *field) {
    memset(field, 0, sizeof(*field));
    if (*f == '!') { field->kind = USING_FIRST_CHAR; return 1; }
    if (*f == '&') { field->kind = USING_WHOLE_STRING; return 1; }
    if (*f == '\\') {
        const char *q = f + 1;
        while (*q == ' ') q++;
        if (*q != '\\') return 0;
        field->kind = USING_STRING_WIDTH;
        field->width = (int)(q - f) + 1;
        return field->width;
    }
    field->kind = USING_NUMBER;
    const char *q = f;
    if (*q == '+') { field->lead_plus = 1; q++; }
    if (strncmp(q, "**$", 3) == 0) { field->asterisk = field->dollar = 1; field->int_positions = 3; q += 3; }
    else if (strncmp(q, "**", 2) == 0) { field->asterisk = 1; field->int_positions = 2; q += 2; }
    else if (strncmp(q, "$$", 2) == 0) { field->dollar = 1; field->int_positions = 2; q += 2; }
    int digits = 0;
    while (1) {
        if (*q == '#') {
            if (field->has_point) field->decimals++; else field->int_positions++;
            digits++;
        } else if (*q == '.' && !field->has_point && (q[1] == '#' || digits || field->int_positions)) {
            field->has_point = 1;
        } else if (*q == ',' && !field->has_point && (digits || field->asterisk || field->dollar) &&
                   (q[1] == '#' || q[1] == ',' || q[1] == '.')) {
            field->comma = 1;
            field->int_positions++;
        } else {
            break;
        }
        q++;
    }
    if (!digits && !field->asterisk && !field->dollar) return 0;
    int carets = 0;
    while (q[carets] == '^') carets++;
    if (carets >= 4) {
        field->carets = carets > 5 ? 5 : carets;
        q += field->carets;
    }
    if (!field->lead_plus && *q == '+') { field->trail_plus = 1; q++; }
    else if (!field->lead_plus && *q == '-') { field->trail_minus = 1; q++; }
    return (int)(q - f);
}

static int using_has_field(const char *fmt) {
    UsingField field;
    for (const char *p = fmt; *p; p++) {
        if (*p == '_') {
            if (p[1]) p++;
        } else if (using_field_at(p, &field)) {
            return 1;
        }
    }
    return 0;
}

static void using_append(char *out, size_t size, const char *text, size_t length) {
    size_t used = strlen(out);
    if (used + length >= size) length = size > used + 1 ? size - used - 1 : 0;
    memcpy(out + used, text, length);
    out[used + length] = '\0';
}

static void using_append_char(char *out, size_t size, char c, int count) {
    for (int i = 0; i < count; i++) using_append(out, size, &c, 1);
}

/* Appends the literal text from *pos up to the next field. Returns that field's
 * length with *field filled in (leaving *pos at it), or 0 at the end. */
static int using_literals(const char *fmt, size_t *pos, UsingField *field, char *out, size_t size) {
    while (fmt[*pos]) {
        const char *p = fmt + *pos;
        int length = using_field_at(p, field);
        if (length) return length;
        if (*p == '_' && p[1]) p++, (*pos)++;
        using_append(out, size, p, 1);
        (*pos)++;
    }
    return 0;
}

static void using_format_string(const UsingField *field, const char *value, char *out, size_t size) {
    size_t length = strlen(value);
    if (field->kind == USING_FIRST_CHAR) {
        using_append_char(out, size, length ? value[0] : ' ', 1);
    } else if (field->kind == USING_WHOLE_STRING) {
        using_append(out, size, value, length);
    } else {
        size_t shown = length < (size_t)field->width ? length : (size_t)field->width;
        using_append(out, size, value, shown);
        using_append_char(out, size, ' ', field->width - (int)shown);
    }
}

static void using_insert_commas(char *digits, size_t size) {
    size_t length = strlen(digits);
    char grouped[96];
    size_t n = 0;
    for (size_t i = 0; i < length && n + 2 < sizeof(grouped); i++) {
        if (i > 0 && (length - i) % 3 == 0) grouped[n++] = ',';
        grouped[n++] = digits[i];
    }
    grouped[n] = '\0';
    snprintf(digits, size, "%s", grouped);
}

static void using_format_number(const UsingField *field, double value, char *out, size_t size) {
    int width = field->int_positions + field->lead_plus;
    int sign_field = field->lead_plus || field->trail_plus || field->trail_minus;
    char whole[96] = "", fraction[64] = "", exponent[16] = "";
    int negative;

    if (field->carets) {
        /* Without a sign in the field, one position before the point holds it. */
        int digits = field->int_positions - (sign_field ? 0 : 1);
        if (digits < 0) digits = 0;
        int power = 0;
        double magnitude = fabs(value), mantissa = 0;
        char text[96];
        for (int attempt = 0; attempt < 2; attempt++) {
            if (magnitude > 0) {
                if (attempt == 0) power = (int)floor(log10(magnitude)) - (digits > 0 ? digits - 1 : -1);
                mantissa = magnitude / pow(10, power);
            }
            snprintf(text, sizeof(text), "%.*f", field->decimals, mantissa);
            char *point = strchr(text, '.');
            int whole_length = point ? (int)(point - text) : (int)strlen(text);
            if (magnitude == 0 || whole_length <= (digits > 0 ? digits : 0) ||
                (digits == 0 && strncmp(text, "0", 1) == 0)) break;
            power++; // rounding carried into another digit
        }
        negative = value < 0 && strtod(text, NULL) != 0;
        char *point = strchr(text, '.');
        if (point) {
            snprintf(fraction, sizeof(fraction), "%s", point + 1);
            *point = '\0';
        }
        snprintf(whole, sizeof(whole), "%s", digits == 0 && strcmp(text, "0") == 0 ? "" : text);
        snprintf(exponent, sizeof(exponent), "E%c%0*d", power < 0 ? '-' : '+', field->carets - 2, abs(power));
    } else {
        char text[96];
        snprintf(text, sizeof(text), "%.*f", field->decimals, fabs(value));
        negative = value < 0 && strtod(text, NULL) != 0;
        char *point = strchr(text, '.');
        if (point) {
            snprintf(fraction, sizeof(fraction), "%s", point + 1);
            *point = '\0';
        }
        snprintf(whole, sizeof(whole), "%s", text);
        if (field->comma) using_insert_commas(whole, sizeof(whole));
    }

    /* Everything before the point: sign, dollar sign and the whole digits. */
    char before[128];
    char lead_sign = field->lead_plus ? (negative ? '-' : '+') : (!sign_field && negative ? '-' : 0);
    int show_zero_space = field->carets && !sign_field && !negative;
    for (int pass = 0; pass < 2; pass++) {
        snprintf(before, sizeof(before), "%s%s%s%s",
                 show_zero_space ? " " : "", lead_sign ? (char[]){lead_sign, 0} : "",
                 field->dollar ? "$" : "", whole);
        // A lone 0 before the point is the first thing dropped to make room.
        if ((int)strlen(before) <= width || strcmp(whole, "0") != 0) break;
        whole[0] = '\0';
    }
    int length = (int)strlen(before);
    if (length > width) using_append_char(out, size, '%', 1);
    else using_append_char(out, size, field->asterisk ? '*' : ' ', width - length);
    using_append(out, size, before, strlen(before));
    if (field->has_point) {
        using_append_char(out, size, '.', 1);
        using_append(out, size, fraction, strlen(fraction));
    }
    using_append(out, size, exponent, strlen(exponent));
    if (field->trail_plus) using_append_char(out, size, negative ? '-' : '+', 1);
    if (field->trail_minus) using_append_char(out, size, negative ? '-' : ' ', 1);
}

/* The Basika printf extension: the single conversion in fmt ('s' for
 * strings; d i o u x X e E f F g G for numbers), or 0 if fmt is not one. */
static char using_printf_conversion(const char *fmt) {
    char conversion = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') continue;
        if (p[1] == '%') { p++; continue; }
        if (conversion) return 0;
        p++;
        while (*p && strchr("-+ #0", *p)) p++;
        for (int i = 0; i < 2 && isdigit((unsigned char)*p); i++) p++;
        if (*p == '.') {
            p++;
            for (int i = 0; i < 2 && isdigit((unsigned char)*p); i++) p++;
        }
        if (!*p || !strchr("diouxXeEfFgGs", *p)) return 0;
        conversion = *p;
    }
    return conversion;
}

static void using_printf(const char *fmt, char conversion, int is_string, const char *text, double value,
                         char *out, size_t size) {
    char buffer[512];
    if (is_string != (conversion == 's')) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return;
    }
    if (is_string) {
        snprintf(buffer, sizeof(buffer), fmt, text);
    } else if (strchr("diouxX", conversion)) {
        /* Widen the conversion to long long. */
        char wide[300];
        size_t prefix = 0;
        for (const char *p = fmt; *p; p++) {
            if (*p == '%' && p[1] == '%') { p++; continue; }
            if (*p == '%') {
                const char *q = p + 1;
                while (*q && !strchr("diouxXeEfFgGs", *q)) q++;
                prefix = (size_t)(q - fmt);
                break;
            }
        }
        snprintf(wide, sizeof(wide), "%.*sll%s", (int)prefix, fmt, fmt + prefix);
        snprintf(buffer, sizeof(buffer), wide, (long long)llround(value));
    } else {
        snprintf(buffer, sizeof(buffer), fmt, value);
    }
    using_append(out, size, buffer, strlen(buffer));
}

/* Formats one PRINT USING value onto out, after the literal text before its
 * field; *pos tracks the place in fmt across the values of one PRINT. */
static void using_format_value(const char *fmt, size_t *pos, int is_string, const char *text, double value,
                               char *out, size_t size) {
    if (!using_has_field(fmt)) {
        char conversion = using_printf_conversion(fmt);
        if (!conversion) {
            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            return;
        }
        using_printf(fmt, conversion, is_string, text, value, out, size);
        return;
    }
    UsingField field;
    int length = using_literals(fmt, pos, &field, out, size);
    if (!length) {
        *pos = 0;
        length = using_literals(fmt, pos, &field, out, size);
    }
    if (is_string != (field.kind != USING_NUMBER)) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return;
    }
    if (is_string) using_format_string(&field, text, out, size);
    else using_format_number(&field, value, out, size);
    *pos += (size_t)length;
}

/* The literal text after the last value's field, up to the next field. */
static void using_trailing_text(const char *fmt, size_t *pos, char *out, size_t size) {
    UsingField field;
    if (using_has_field(fmt)) using_literals(fmt, pos, &field, out, size);
}

static Variable *get_or_create_frame_variable(CallFrame *frame, int variable_index) {
    if (variable_index < 0 || variable_index >= MAX_VARIABLES) return &vars[0];
    if (frame->resolved_variables[variable_index]) {
        return frame->resolved_variables[variable_index];
    }
    note_resolved_variable(frame, variable_index);
    const char *name = vars[variable_index].name;
    for (int i = 0; i < frame->local_var_count; i++) {
        if (strcasecmp((*frame_local(frame, i)).name, name) == 0) {
            return frame->resolved_variables[variable_index] = &(*frame_local(frame, i));
        }
    }
    if (frame->local_var_count >= MAX_FRAME_LOCALS) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return &vars[0];
    }
    int local_index = frame->local_var_count++;
    memset(&(*frame_local(frame, local_index)), 0, sizeof(Variable));
    snprintf((*frame_local(frame, local_index)).name,
             sizeof((*frame_local(frame, local_index)).name), "%s", name);
    return frame->resolved_variables[variable_index] = &(*frame_local(frame, local_index));
}

static const char *user_type_field_suffix(const TypeField *field) {
    size_t length = strlen(field->name);
    char last = length > 0 ? field->name[length - 1] : '\0';
    if (is_type_suffix_char(last)) return "";
    const char *suffix = primitive_type_suffix(field->type_name);
    return suffix ? suffix : "";
}

static int copy_variable_contents(Variable *destination, const Variable *source) {
    if (destination == source) return 1;
    // COMMON may pass a value to a variable of another numeric type.
    Variable *typed_source = (Variable *)source;
    int convert = var_type(destination) != var_type(typed_source) &&
        var_type(destination) != VT_STRING && var_type(typed_source) != VT_STRING;
    if (convert) store_num(destination, -1, load_num(typed_source, -1));
    else destination->value = source->value;
    destination->string_declared = source->string_declared;
    destination->string_fixed_length = source->string_fixed_length;
    if (source->array) {
        if (!destination->array || destination->array_size != source->array_size) {
            free(destination->array);
            destination->array = malloc((size_t)source->array_size * sizeof(*destination->array));
            if (!destination->array) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return 0;
            }
        }
        if (convert) {
            destination->array_size = source->array_size;
            for (int i = 0; i < source->array_size; i++) {
                store_num(destination, i, load_num(typed_source, i));
            }
        } else {
            memcpy(destination->array, source->array,
                   (size_t)source->array_size * sizeof(*destination->array));
        }
    }
    if (source->s_array) {
        if (!destination->s_array || destination->array_size != source->array_size) {
            if (destination->s_array) {
                for (int i = 0; i < destination->array_size; i++) {
                    basic_string_destroy(destination->s_array[i]);
                }
                free(destination->s_array);
            }
            destination->s_array = calloc((size_t)source->array_size,
                                          sizeof(*destination->s_array));
            if (!destination->s_array) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return 0;
            }
        }
        for (int i = 0; i < source->array_size; i++) {
            if (source->s_array[i]) {
                if (destination->string_declared && !destination->s_array[i]) {
                    destination->s_array[i] = basic_string_create(NULL, 0);
                    if (!destination->s_array[i]) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        return 0;
                    }
                }
                if (destination->string_declared) {
                    destination->s_array[i]->is_fixed = 1;
                    destination->s_array[i]->fixed_length = destination->string_fixed_length;
                }
                if (!basic_string_assign(&destination->s_array[i],
                                         source->s_array[i]->data,
                                         source->s_array[i]->length)) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    return 0;
                }
            }
        }
    }
    destination->array_size = source->array_size;
    destination->num_dims = source->num_dims;
    memcpy(destination->dims, source->dims, sizeof(destination->dims));
    memcpy(destination->lower_bounds, source->lower_bounds,
           sizeof(destination->lower_bounds));
    if (source->s_value) {
        if (destination->string_declared) {
            if (!destination->s_value) {
                destination->s_value = basic_string_create(NULL, 0);
                if (!destination->s_value) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    return 0;
                }
            }
            destination->s_value->is_fixed = 1;
            destination->s_value->fixed_length = destination->string_fixed_length;
        }
        if (!basic_string_assign(&destination->s_value, source->s_value->data,
                                 source->s_value->length)) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return 0;
        }
    } else if (destination->string_declared) {
        if (!destination->s_value) destination->s_value = basic_string_create(NULL, 0);
        if (!destination->s_value) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return 0;
        }
        destination->s_value->is_fixed = 1;
        destination->s_value->fixed_length = destination->string_fixed_length;
        if (!basic_string_assign(&destination->s_value, NULL, 0)) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return 0;
        }
    }
    return 1;
}

static int copy_user_type_value(const char *source_prefix, const char *destination_prefix,
                                UserType *type, int depth, CallFrame *destination_frame) {
    if (!type || depth >= MAX_USER_TYPES) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    for (int i = 0; i < type->field_count; i++) {
        TypeField *field = &type->fields[i];
        char source_member[128];
        char destination_member[128];
        const char *suffix = user_type_field_suffix(field);
        int source_written = snprintf(source_member, sizeof(source_member), "%s.%s%s",
                                      source_prefix, field->name, suffix);
        int destination_written = snprintf(destination_member, sizeof(destination_member),
                                           "%s.%s%s", destination_prefix,
                                           field->name, suffix);
        if (source_written < 0 || (size_t)source_written >= sizeof(source_member) ||
            destination_written < 0 ||
            (size_t)destination_written >= sizeof(destination_member)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }

        UserType *nested = find_user_type(field->type_name);
        if (nested) {
            int lower_bounds[3] = {0};
            int element_count = 1;
            for (int dim = 0; dim < field->num_dims; dim++) {
                lower_bounds[dim] = field->lower_bounds[dim] < 0
                    ? option_base : field->lower_bounds[dim];
                int count = field->dims[dim] - lower_bounds[dim] + 1;
                if (count <= 0 || element_count > 100000 / count) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    return 0;
                }
                element_count *= count;
            }
            for (int element = 0; element < element_count; element++) {
                char source_nested[128];
                char destination_nested[128];
                if (field->num_dims > 0) {
                    source_written = snprintf(source_nested, sizeof(source_nested),
                                              "%s@%d", source_member, element);
                    destination_written = snprintf(destination_nested,
                                                   sizeof(destination_nested),
                                                   "%s@%d", destination_member, element);
                } else {
                    source_written = snprintf(source_nested, sizeof(source_nested),
                                              "%s", source_member);
                    destination_written = snprintf(destination_nested,
                                                   sizeof(destination_nested), "%s",
                                                   destination_member);
                }
                if (source_written < 0 || (size_t)source_written >= sizeof(source_nested) ||
                    destination_written < 0 ||
                    (size_t)destination_written >= sizeof(destination_nested) ||
                    !copy_user_type_value(source_nested, destination_nested, nested,
                                          depth + 1, destination_frame)) return 0;
            }
            continue;
        }

        int source_index = find_variable_raw_name(source_member);
        int destination_index = find_variable_raw_name(destination_member);
        Variable *source = get_variable_ptr(source_index);
        Variable *destination = destination_frame
            ? get_or_create_frame_variable(destination_frame, destination_index)
            : get_variable_ptr(destination_index);
        if (field->is_fixed_string) {
            destination->string_declared = 1;
            destination->string_fixed_length = field->fixed_string_length;
            fixed_string_declarations_present = 1;
        }
        if (field->num_dims > 0 && !destination->array && !destination->s_array) {
            int lower_bounds[3] = {0};
            int count = 1;
            for (int dim = 0; dim < field->num_dims; dim++) {
                lower_bounds[dim] = field->lower_bounds[dim] < 0
                    ? option_base : field->lower_bounds[dim];
                int size = field->dims[dim] - lower_bounds[dim] + 1;
                if (size <= 0 || count > 100000 / size) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    return 0;
                }
                count *= size;
            }
            if (strcasecmp(field->type_name, "STRING") == 0) {
                destination->s_array = calloc((size_t)count,
                                              sizeof(*destination->s_array));
            } else {
                destination->array = calloc((size_t)count, sizeof(*destination->array));
            }
            if ((!destination->array && strcasecmp(field->type_name, "STRING") != 0) ||
                (!destination->s_array && strcasecmp(field->type_name, "STRING") == 0)) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return 0;
            }
            destination->array_size = count;
            destination->num_dims = field->num_dims;
            for (int dim = 0; dim < field->num_dims; dim++) {
                destination->dims[dim] = field->dims[dim];
                destination->lower_bounds[dim] = lower_bounds[dim];
            }
        }
        if (!copy_variable_contents(destination, source)) return 0;
    }
    return 1;
}

static int bind_user_type_parameter_byref(CallFrame *frame, int parameter_index,
                                          ProcParamDef *parameter, int source_index) {
    UserTypeInstance source_instance;
    if (!get_user_type_instance_for_root(source_index, &source_instance) ||
        source_instance.type_index != parameter->user_type_index ||
        parameter->is_array != (source_instance.num_dims > 0)) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    frame->byref_caller_variable[parameter_index] = get_variable_ptr(source_index);
    frame->byref_caller_var_idx[parameter_index] = source_index;
    frame->byref_caller_frame[parameter_index] = call_stack_depth > 0
        ? call_stack_depth - 1 : -1;
    return 1;
}

static int bind_user_type_parameter_value(CallFrame *frame,
                                          ProcParamDef *parameter,
                                          int source_index) {
    UserTypeInstance source_instance;
    if (!get_user_type_instance_for_root(source_index, &source_instance) ||
        source_instance.type_index != parameter->user_type_index ||
        source_instance.num_dims != 0) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    Variable *source_root = get_variable_ptr(source_index);
    if (!copy_user_type_value(source_root->name, parameter->name,
            &user_types[parameter->user_type_index], 0, frame)) return 0;
    int destination_index = find_variable_raw_name(parameter->name);
    get_or_create_frame_variable(frame, destination_index);
    return 1;
}

static int bind_user_type_parameter_value_tok(TokenStream *ts, CallFrame *frame,
                                              ProcParamDef *parameter) {
    if (ts->tokens[ts->pos].type != TOKEN_LPAREN ||
        ts->tokens[ts->pos + 1].type != TOKEN_IDENTIFIER ||
        ts->tokens[ts->pos + 2].type != TOKEN_RPAREN) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    ts->pos++;
    int source_index = find_existing_variable_raw_name(ts->tokens[ts->pos++].text);
    ts->pos++;
    return bind_user_type_parameter_value(frame, parameter, source_index);
}

static int bind_user_type_parameter_value_text(const char **input, CallFrame *frame,
                                               ProcParamDef *parameter) {
    const char *cursor = skip_whitespace_fast(*input);
    if (*cursor++ != '(') {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    cursor = skip_whitespace_fast(cursor);
    char name[128];
    size_t length = 0;
    if (!isalpha((unsigned char)*cursor) && *cursor != '_') {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    while (isalnum((unsigned char)*cursor) || *cursor == '_' || *cursor == '$' ||
           *cursor == '%' || *cursor == '!' || *cursor == '#' || *cursor == '&' || *cursor == '~') {
        if (length + 1 >= sizeof(name)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }
        name[length++] = *cursor++;
    }
    name[length] = '\0';
    cursor = skip_whitespace_fast(cursor);
    if (*cursor++ != ')') {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    int source_index = find_existing_variable_raw_name(name);
    if (!bind_user_type_parameter_value(frame, parameter, source_index)) return 0;
    *input = cursor;
    return 1;
}

static int copy_user_type_instance_value(int destination_index, int destination_array_index,
                                         int source_index, int source_array_index,
                                         CallFrame *destination_frame) {
    UserTypeInstance destination_instance;
    UserTypeInstance source_instance;
    if (!get_user_type_instance_for_root(destination_index, &destination_instance) ||
        !get_user_type_instance_for_root(source_index, &source_instance) ||
        destination_instance.type_index != source_instance.type_index ||
        (destination_instance.num_dims > 0) != (destination_array_index >= 0) ||
        (source_instance.num_dims > 0) != (source_array_index >= 0)) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    Variable *source_root = get_variable_ptr(source_index);
    Variable *destination_root = destination_frame
        ? get_or_create_frame_variable(destination_frame, destination_index)
        : get_variable_ptr(destination_index);
    char source_prefix[128];
    char destination_prefix[128];
    if (source_instance.num_dims > 0) {
        snprintf(source_prefix, sizeof(source_prefix), "%s@%d",
                 source_root->name, source_array_index);
    } else {
        snprintf(source_prefix, sizeof(source_prefix), "%s", source_root->name);
    }
    if (destination_instance.num_dims > 0) {
        snprintf(destination_prefix, sizeof(destination_prefix), "%s@%d",
                 destination_root->name, destination_array_index);
    } else {
        snprintf(destination_prefix, sizeof(destination_prefix), "%s",
                 destination_root->name);
    }
    return copy_user_type_value(source_prefix, destination_prefix,
                                &user_types[destination_instance.type_index],
                                0, destination_frame);
}

static int assign_user_type_value_tok(TokenStream *ts, int destination_index,
                                      int destination_array_index) {
    UserTypeInstance destination_instance;
    if (!get_user_type_instance_for_root(destination_index, &destination_instance)) return 0;
    if (ts->tokens[ts->pos].type != TOKEN_IDENTIFIER) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return -1;
    }
    int source_index = find_existing_variable_raw_name(ts->tokens[ts->pos].text);
    if (source_index < 0) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return -1;
    }
    ts->pos++;
    UserTypeInstance source_instance;
    if (!get_user_type_instance_for_root(source_index, &source_instance)) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return -1;
    }
    int source_array_index = -1;
    if (source_instance.num_dims > 0) {
        source_array_index = parse_array_index_tok(ts, source_index);
    }
    TokenType trailing = ts->tokens[ts->pos].type;
    if (trailing != TOKEN_EOF && trailing != TOKEN_COLON && trailing != TOKEN_ELSE) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return -1;
    }
    if (destination_instance.type_index != source_instance.type_index ||
        ((destination_instance.num_dims > 0) != (destination_array_index >= 0)) ||
        ((source_instance.num_dims > 0) != (source_array_index >= 0))) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return -1;
    }
    return copy_user_type_instance_value(destination_index, destination_array_index,
                                         source_index, source_array_index, NULL) ? 1 : -1;
}

static int user_type_field_element_count(const TypeField *field, int *count_out) {
    int count = 1;
    for (int dim = 0; dim < field->num_dims; dim++) {
        int lower = field->lower_bounds[dim] < 0
            ? option_base : field->lower_bounds[dim];
        int dimension_size = field->dims[dim] - lower + 1;
        if (dimension_size <= 0 || count > BASIC_STRING_MAX / dimension_size) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return 0;
        }
        count *= dimension_size;
    }
    *count_out = count;
    return 1;
}

static int user_type_storage_size(UserType *type, size_t *size_out, int depth) {
    if (!type || depth >= MAX_USER_TYPES) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    size_t total = 0;
    for (int i = 0; i < type->field_count; i++) {
        TypeField *field = &type->fields[i];
        int element_count;
        if (!user_type_field_element_count(field, &element_count)) return 0;
        UserType *nested = find_user_type(field->type_name);
        size_t element_size;
        if (nested) {
            if (!user_type_storage_size(nested, &element_size, depth + 1)) return 0;
        } else if (strcasecmp(field->type_name, "STRING") == 0) {
            if (!field->is_fixed_string || field->fixed_string_length <= 0) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return 0;
            }
            element_size = (size_t)field->fixed_string_length;
        } else if (find_primitive_type(field->type_name)) {
            element_size = (size_t)find_primitive_type(field->type_name)->bytes;
        } else {
            report_runtime_error(ERR_TYPE_MISMATCH);
            return 0;
        }
        if (element_size > BASIC_STRING_MAX / (size_t)element_count ||
            total > BASIC_STRING_MAX - element_size * (size_t)element_count) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return 0;
        }
        total += element_size * (size_t)element_count;
    }
    *size_out = total;
    return 1;
}

static void transfer_little_endian_bytes(unsigned char *bytes, void *value,
                                         size_t size, int writing) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    unsigned char *native_bytes = value;
    for (size_t i = 0; i < size; i++) {
        if (writing) bytes[i] = native_bytes[size - i - 1];
        else native_bytes[size - i - 1] = bytes[i];
    }
#else
    if (writing) memcpy(bytes, value, size);
    else memcpy(value, bytes, size);
#endif
}

/* Packed little-endian record fields: integers by size and signedness,
 * SINGLE as a 4-byte float, DOUBLE as 8 bytes. */
static void encode_record_value(const char *type_name, Num value, unsigned char *out) {
    const PrimitiveType *type = find_primitive_type(type_name);
    if (type && type->is_integer) {
        int64_t whole;
        if (num_is_int(value)) whole = value.i;
        else if (!num_to_integer_type(value, (int)(type - primitive_types) + 1, &whole)) whole = 0;
        uint64_t bits = (uint64_t)whole;
        for (int i = 0; i < type->bytes; i++) out[i] = (unsigned char)(bits >> (8 * i));
    } else if (type && type->bytes == 4) {
        float single = (float)num_to_double(value);
        transfer_little_endian_bytes(out, &single, sizeof(single), 1);
    } else {
        double number = num_to_double(value);
        transfer_little_endian_bytes(out, &number, sizeof(number), 1);
    }
}

static Num decode_record_value(const char *type_name, const unsigned char *in) {
    const PrimitiveType *type = find_primitive_type(type_name);
    if (type && type->is_integer) {
        uint64_t bits = 0;
        for (int i = 0; i < type->bytes; i++) bits |= (uint64_t)in[i] << (8 * i);
        if (type->is_unsigned) return num_i((int64_t)bits, type->bytes == 8 ? NUM_UINT64 : NUM_INT64);
        int shift = 64 - 8 * type->bytes;
        return num_i((int64_t)(bits << shift) >> shift, NUM_INT64); // sign-extend
    }
    if (type && type->bytes == 4) {
        float single;
        transfer_little_endian_bytes((unsigned char *)in, &single, sizeof(single), 0);
        return num_f(single, NUM_SINGLE);
    }
    double value;
    transfer_little_endian_bytes((unsigned char *)in, &value, sizeof(value), 0);
    return num_f(value, NUM_DOUBLE);
}

static int transfer_user_type_fields(UserType *type, const char *prefix,
                                     unsigned char *buffer, size_t *offset,
                                     int writing, int depth) {
    if (!type || depth >= MAX_USER_TYPES) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    for (int i = 0; i < type->field_count; i++) {
        TypeField *field = &type->fields[i];
        char member[128];
        const char *suffix = user_type_field_suffix(field);
        int written = snprintf(member, sizeof(member), "%s.%s%s",
                               prefix, field->name, suffix);
        if (written < 0 || (size_t)written >= sizeof(member)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }
        int element_count;
        if (!user_type_field_element_count(field, &element_count)) return 0;
        UserType *nested = find_user_type(field->type_name);
        size_t element_size = 0;
        if (nested) {
            if (!user_type_storage_size(nested, &element_size, depth + 1)) return 0;
        } else if (strcasecmp(field->type_name, "STRING") == 0) {
            element_size = (size_t)field->fixed_string_length;
        } else {
            const PrimitiveType *primitive = find_primitive_type(field->type_name);
            element_size = primitive ? (size_t)primitive->bytes : sizeof(double);
        }

        for (int element = 0; element < element_count; element++) {
            if (nested) {
                char nested_prefix[128];
                written = field->num_dims > 0
                    ? snprintf(nested_prefix, sizeof(nested_prefix), "%s@%d",
                               member, element)
                    : snprintf(nested_prefix, sizeof(nested_prefix), "%s", member);
                if (written < 0 || (size_t)written >= sizeof(nested_prefix) ||
                    !transfer_user_type_fields(nested, nested_prefix, buffer, offset,
                                               writing, depth + 1)) return 0;
                continue;
            }
            int variable_index = find_variable_raw_name(member);
            Variable *variable = get_variable_ptr(variable_index);
            int array_index = field->num_dims > 0 ? element : -1;
            if (strcasecmp(field->type_name, "STRING") == 0) {
                BasicString *value = array_index >= 0
                    ? (variable->s_array ? variable->s_array[array_index] : NULL)
                    : variable->s_value;
                if (writing) {
                    memset(buffer + *offset, ' ', element_size);
                    if (value && value->data) {
                        size_t copy_length = value->length < element_size
                            ? value->length : element_size;
                        memcpy(buffer + *offset, value->data, copy_length);
                    }
                } else {
                    BasicString **target = array_index >= 0
                        ? &variable->s_array[array_index] : &variable->s_value;
                    if (!*target) {
                        *target = basic_string_create(NULL, 0);
                        if (!*target) {
                            report_runtime_error(ERR_OUT_OF_MEMORY);
                            return 0;
                        }
                    }
                    (*target)->is_fixed = 1;
                    (*target)->fixed_length = element_size;
                    if (!basic_string_assign(target, (const char *)buffer + *offset,
                                             element_size)) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        return 0;
                    }
                }
            } else if (writing) {
                int element = array_index >= 0 && variable->array ? array_index : -1;
                encode_record_value(field->type_name, load_num(variable, element), buffer + *offset);
            } else {
                int element = array_index >= 0 && variable->array ? array_index : -1;
                store_num(variable, element, decode_record_value(field->type_name, buffer + *offset));
            }
            *offset += element_size;
        }
    }
    return 1;
}

static int file_record_offset(int fnum, double record, long *offset);

static int transfer_user_type_record(int file_number, double record_number,
                                     int root_index, int array_index, int writing) {
    if (file_number < 1 || file_number >= 16 || !file_handles[file_number]) {
        report_runtime_error(ERR_BAD_FILE_NUMBER);
        return 0;
    }
    UserTypeInstance instance;
    if (!get_user_type_instance_for_root(root_index, &instance) ||
        ((instance.num_dims > 0) != (array_index >= 0))) {
        report_runtime_error(ERR_TYPE_MISMATCH);
        return 0;
    }
    size_t record_size;
    if (!user_type_storage_size(&user_types[instance.type_index], &record_size, 0) ||
        record_size == 0) return 0;
    // A record must fit the LEN given when a RANDOM file was opened.
    if (file_mode[file_number] != FILE_MODE_BINARY &&
        record_size > (size_t)file_record_length[file_number]) {
        report_runtime_error(ERR_BAD_RECORD_LENGTH);
        return 0;
    }
    long offset_in_file;
    if (!file_record_offset(file_number, record_number, &offset_in_file)) return 0;
    file_last_record[file_number] = file_mode[file_number] == FILE_MODE_BINARY
        ? offset_in_file + (long)record_size : (long)record_number;
    Variable *root = get_variable_ptr(root_index);
    char prefix[128];
    int written = instance.num_dims > 0
        ? snprintf(prefix, sizeof(prefix), "%s@%d", root->name, array_index)
        : snprintf(prefix, sizeof(prefix), "%s", root->name);
    if (written < 0 || (size_t)written >= sizeof(prefix)) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    unsigned char *buffer = calloc(record_size, 1);
    if (!buffer) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return 0;
    }
    int success = 1;
    if (writing) {
        size_t offset = 0;
        success = transfer_user_type_fields(&user_types[instance.type_index], prefix,
                                            buffer, &offset, 1, 0);
        if (success && (fseek(file_handles[file_number], offset_in_file, SEEK_SET) != 0 ||
            fwrite(buffer, 1, record_size, file_handles[file_number]) != record_size ||
            fflush(file_handles[file_number]) != 0)) {
            report_runtime_error(ERR_BAD_FILE_NUMBER);
            success = 0;
        }
    } else {
        if (fseek(file_handles[file_number], offset_in_file, SEEK_SET) != 0) {
            report_runtime_error(ERR_BAD_FILE_NUMBER);
            success = 0;
        } else {
            size_t bytes_read = fread(buffer, 1, record_size, file_handles[file_number]);
            if (bytes_read < record_size) {
                memset(buffer + bytes_read, 0, record_size - bytes_read);
            }
            size_t offset = 0;
            success = transfer_user_type_fields(&user_types[instance.type_index], prefix,
                                                buffer, &offset, 0, 0);
        }
    }
    free(buffer);
    return success;
}

/* The primitive type name whose packed file image a variable of type vt
 * has; untyped variables record as SINGLE. */
static const char *variable_record_type_name(int vt) {
    return vt >= VT_STRING && vt <= VT_DOUBLE ? primitive_types[vt - 1].name : "SINGLE";
}

/* Bytes a numeric variable of type vt occupies in a file record. */
static int numeric_variable_record_size(int vt) {
    return vt >= VT_STRING && vt <= VT_DOUBLE ? primitive_types[vt - 1].bytes : 4;
}

/* The string value a PUT/GET variable argument names: a scalar's s_value
 * or one element of s_array; NULL while it holds no data yet. */
static BasicString *variable_string_value(Variable *v, int array_index) {
    if (array_index >= 0) {
        return v->s_array && array_index < v->array_size
            ? v->s_array[array_index] : NULL;
    }
    return v->s_value;
}

/* Matches QBasic's `PUT/GET #n, [position,] variable` argument: an
 * identifier, optionally with an array index, that ends the statement.
 * BASIKA's `PUT/GET #n, position, len, data$` form continues with a comma
 * after the length expression (or with an operator inside it), so those
 * statements are left for it. Returns 1 on a match with *var_index_out and
 * *array_index_out set and *input left before the statement separator, 0
 * when the statement does not have this form (*input unchanged), or -1
 * after reporting an error. */
static int parse_record_variable_argument(const char **input, const char *name,
                                          int *var_index_out, int *array_index_out) {
    const char *saved = *input;
    int depth = 0;
    int ended = 0;
    while (!ended) {
        const char *before = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_LPAREN) {
            depth++;
        } else if (t.type == TOKEN_RPAREN) {
            if (depth > 0) depth--;
        } else if (t.type == TOKEN_EOF && depth > 0) {
            break; // unbalanced parentheses
        } else if (depth == 0) {
            if (t.type == TOKEN_EOF || t.type == TOKEN_COLON ||
                t.type == TOKEN_ELSE || t.type == TOKEN_APOSTROPHE ||
                t.type == TOKEN_REM) {
                *input = before; // leave the separator to the statement loop
                ended = 1;
            } else {
                break; // comma or expression: the BASIKA length form
            }
        }
    }
    if (!ended) {
        *input = saved;
        return 0;
    }
    int var_index = find_variable(name);
    if (var_index < 0) {
        *input = saved;
        return 0;
    }
    *input = saved; // re-parse the optional array index after the identifier
    int array_index = parse_array_index(input, var_index);
    if (array_index < 0 && get_variable_ptr(var_index)->num_dims > 0) {
        // A whole array is not a record; only single elements are.
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return -1;
    }
    *var_index_out = var_index;
    *array_index_out = array_index;
    return 1;
}

/* PUT #n/GET #n with a numeric or string variable (a scalar, an array
 * element or a record member) as in QBasic: exactly the variable's bytes
 * move - the type's size for a numeric, the current length for a string.
 * BINARY files are positioned by byte and move exactly those bytes;
 * RANDOM files always move a whole LEN-byte record, zero-filling the bytes
 * past the variable, and data larger than LEN raises Bad record length. */
static int transfer_variable_record(int fnum, double rec, int var_index,
                                    int array_index, int writing) {
    if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
        report_runtime_error(ERR_BAD_FILE_NUMBER);
        return 0;
    }
    if (file_mode[fnum] != FILE_MODE_RANDOM && file_mode[fnum] != FILE_MODE_BINARY) {
        report_runtime_error(ERR_BAD_FILE_MODE);
        return 0;
    }
    Variable *v = get_variable_ptr(var_index);
    int string_variable = v->string_declared || var_type(v) == VT_STRING;
    BasicString *string_value = NULL;
    size_t size;
    if (string_variable) {
        string_value = variable_string_value(v, array_index);
        size = string_value ? string_value->length
            : v->string_declared ? v->string_fixed_length : 0;
    } else {
        size = (size_t)numeric_variable_record_size(var_type(v));
    }
    int binary = file_mode[fnum] == FILE_MODE_BINARY;
    if (!binary && size > (size_t)file_record_length[fnum]) {
        report_runtime_error(ERR_BAD_RECORD_LENGTH);
        return 0;
    }
    long offset;
    if (!file_record_offset(fnum, rec, &offset)) return 0;
    file_last_record[fnum] = binary ? offset + (long)size : (long)rec;
    size_t transfer_size = binary ? size : (size_t)file_record_length[fnum];
    unsigned char *buffer = calloc(transfer_size ? transfer_size : 1, 1);
    if (!buffer) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return 0;
    }
    const char *type_name = variable_record_type_name(var_type(v));
    int success = 1;
    if (writing) {
        if (string_variable) {
            if (size > 0) memcpy(buffer, string_value->data, size);
        } else {
            encode_record_value(type_name, load_num(v, array_index), buffer);
        }
        if (fseek(file_handles[fnum], offset, SEEK_SET) != 0 ||
            fwrite(buffer, 1, transfer_size, file_handles[fnum]) != transfer_size ||
            fflush(file_handles[fnum]) != 0) {
            report_runtime_error(ERR_BAD_FILE_NUMBER);
            success = 0;
        }
    } else {
        if (fseek(file_handles[fnum], offset, SEEK_SET) != 0) {
            report_runtime_error(ERR_BAD_FILE_NUMBER);
            success = 0;
        } else {
            size_t bytes_read = fread(buffer, 1, size, file_handles[fnum]);
            if (bytes_read < size) memset(buffer + bytes_read, 0, size - bytes_read);
            if (string_variable) {
                if (!assign_string_variable_value(var_index, array_index,
                                                   buffer, size)) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    success = 0;
                }
            } else if (!store_num(v, array_index,
                                  decode_record_value(type_name, buffer))) {
                success = 0; // overflow already reported
            }
        }
    }
    free(buffer);
    return success;
}

/* Inside FUNCTION Name$, "Name = ..." sets the string result, as in QB64:
 * renames the assignment target to the function's own name. */
static void name_string_function_result(Token *target) {
    const ProcedureDef *scope = current_declaration_scope();
    if (!scope || !scope->is_function) return;
    size_t len = strlen(target->text);
    if (len == 0 || strchr("$%&!#.", target->text[len - 1]) ||
        strncasecmp(scope->name, target->text, len) != 0 || strcmp(scope->name + len, "$") != 0) {
        return;
    }
    snprintf(target->text, sizeof(target->text), "%s", scope->name);
    target->var_idx = -1;
    target->type_generation = 0;
    target->string_cache = -1;
}

static void execute_assignment(const char **input, Token var_token) {
    name_string_function_result(&var_token);
    int idx = var_token.var_idx;
    int array_idx = -1;
    int is_member = user_type_count > 0
        ? resolve_user_type_reference_text(input, &var_token, &idx, &array_idx)
        : 0;
    if (is_member < 0) return;
    if (!is_member) {
        int raw_index = find_existing_variable_raw_name(var_token.text);
        UserTypeInstance existing_instance;
        if (raw_index >= 0 && get_user_type_instance_for_root(raw_index, &existing_instance)) {
            idx = raw_index;
        } else {
            idx = strchr(var_token.text, '.') ? find_variable(var_token.text) : var_token.var_idx;
            if (idx == -1) idx = find_variable(var_token.text);
        }
        array_idx = parse_array_index(input, idx);
    }
    const char *saved = *input;
    Token eq = get_next_token(input);
    if (eq.type != TOKEN_EQUALS) {
        *input = saved;
        report_runtime_error(ERR_SYNTAX_ERROR);
        return;
    }

    UserTypeInstance destination_instance;
    if (get_user_type_instance_for_root(idx, &destination_instance)) {
        Token source_token = get_next_token(input);
        int source_index = source_token.type == TOKEN_IDENTIFIER
            ? find_existing_variable_raw_name(source_token.text) : -1;
        UserTypeInstance source_instance;
        int source_array_index = -1;
        if (source_index >= 0 &&
            get_user_type_instance_for_root(source_index, &source_instance)) {
            if (source_instance.num_dims > 0) {
                source_array_index = parse_array_index(input, source_index);
            }
            const char *trailing_saved = *input;
            Token trailing = get_next_token(input);
            *input = trailing_saved;
            if (source_instance.type_index != destination_instance.type_index ||
                ((source_instance.num_dims > 0) != (source_array_index >= 0)) ||
                ((destination_instance.num_dims > 0) != (array_idx >= 0)) ||
                (trailing.type != TOKEN_EOF && trailing.type != TOKEN_COLON)) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return;
            }
            Variable *source_root = get_variable_ptr(source_index);
            Variable *destination_root = get_variable_ptr(idx);
            char source_prefix[128];
            char destination_prefix[128];
            if (source_instance.num_dims > 0) {
                snprintf(source_prefix, sizeof(source_prefix), "%s@%d",
                         source_root->name, source_array_index);
            } else {
                snprintf(source_prefix, sizeof(source_prefix), "%s", source_root->name);
            }
            if (destination_instance.num_dims > 0) {
                snprintf(destination_prefix, sizeof(destination_prefix), "%s@%d",
                         destination_root->name, array_idx);
            } else {
                snprintf(destination_prefix, sizeof(destination_prefix), "%s",
                         destination_root->name);
            }
            if (copy_user_type_value(source_prefix, destination_prefix,
                    &user_types[destination_instance.type_index], 0, NULL)) return;
            return;
        }
        report_runtime_error(ERR_TYPE_MISMATCH);
        return;
    }

    if (is_string_var(get_variable_ptr(idx)->name)) {
        BasicString dynamic_value = {0};
        const char *dynamic_saved = *input;
        if (parse_dynamic_string_expression(input, &dynamic_value)) {
            assign_string_variable_move(idx, array_idx, &dynamic_value);
            free(dynamic_value.data);
            return;
        }
        *input = dynamic_saved;
        free(dynamic_value.data);
        char value[BASIC_STRING_MAX] = "";
        const char *expr_saved = *input;
        Token tok = get_next_token(input);
        if (tok.type == TOKEN_STRING) {
            strncpy(value, tok.text, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
        } else {
            *input = expr_saved;
            parse_string_expression(input, value, sizeof(value));
        }
        set_string_variable(idx, array_idx, value);
    } else {
        Num value = evaluate_num_text(input);
        if (!runtime_error_occurred) set_numeric_variable_num(idx, array_idx, value);
    }
}

static void clear_variables(int keep_registry) {
    for (int i = 0; i < var_count; i++) {
        if (vars[i].array) {
            free(vars[i].array);
            vars[i].array = NULL;
        }
        if (vars[i].s_array) {
            for (int j = 0; j < vars[i].array_size; j++) {
                basic_string_destroy(vars[i].s_array[j]);
            }
            free(vars[i].s_array);
            vars[i].s_array = NULL;
        }
        if (vars[i].s_value) {
            basic_string_destroy(vars[i].s_value);
            vars[i].s_value = NULL;
        }
        vars[i].array_size = 0;
        vars[i].num_dims = 0;
        vars[i].value = 0;
        if (!keep_registry) vars[i].name[0] = '\0';
    }
    if (!keep_registry) {
        var_count = 0;
        reset_variable_hash_table();
    }

    user_function_count = 0;
    named_constant_count = 0;
    if (++constant_generation == 0) constant_generation = 1;
    declared_type_count = 0;
    global_shared_count = 0;
    clear_static_locals();
    option_base = 0;
    option_base_set = 0;
    arrays_dimensioned = 0;
    for (int i = 0; i < 26; i++) default_type_map[i] = '!';
    default_type_generation++;
    if (default_type_generation == 0) default_type_generation = 1;
    invalidate_expression_name_cache();
}

void basic_output(const char *text) { // Made non-static
    if (!graphics_is_active()) {
        printf("%s", text);
    }
    graphics_print(text);
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n' || text[i] == '\r') {
            print_col = 0;
            print_row++;
        } else {
            print_col++;
        }
    }
}

// Forward declarations for recursive descent expression parser
double evaluate_expression(const char **input);

/* is_string_var for a program token. The answer depends only on the DEF
 * types and declarations, so it is kept on the token until those change.
 * Record fields (names with a dot) and fixed-length strings are not cached. */
static int is_string_identifier_tok(Token *token) {
    if (fixed_string_declarations_present) return is_string_var(token->text);
    if (token->string_generation != default_type_generation) {
        token->string_cache = strchr(token->text, '.') ? -1 : is_string_var(token->text);
        token->string_generation = default_type_generation;
    }
    return token->string_cache >= 0 ? token->string_cache : is_string_var(token->text);
}

static int is_string_token(const Token *t) {
    if (t->type == TOKEN_STRING) return 1;
    if (t->type == TOKEN_IDENTIFIER) {
        if (find_named_constant_tok((Token *)t) >= 0) return 0;
        if (is_string_identifier_tok((Token *)t)) return 1;
    }
    return (t->type == TOKEN_CHR || t->type == TOKEN_LEFT || t->type == TOKEN_RIGHT ||
            t->type == TOKEN_MID || t->type == TOKEN_UCASE || t->type == TOKEN_LCASE ||
            t->type == TOKEN_TRIM || t->type == TOKEN_LTRIM || t->type == TOKEN_RTRIM ||
            t->type == TOKEN_STR || t->type == TOKEN_HEX || t->type == TOKEN_OCT ||
            t->type == TOKEN_MKI || t->type == TOKEN_MKL || t->type == TOKEN_MKS || t->type == TOKEN_MKD ||
            t->type == TOKEN_STRING_FUNC || t->type == TOKEN_DEFLATE || t->type == TOKEN_INFLATE || t->type == TOKEN_INKEY || t->type == TOKEN_INPUTS || t->type == TOKEN_GETS || t->type == TOKEN_ENVIRON ||
            t->type == TOKEN_TIME || t->type == TOKEN_DATE || t->type == TOKEN_TAB ||
            t->type == TOKEN_SPACE || t->type == TOKEN_SPC ||
            t->type == TOKEN_ARGVS || t->type == TOKEN_COMMANDS);
}

#define MAX_CACHED_EXPRESSION_INSTRUCTIONS 256

/* Operand stack depth for cached expressions; deeper ones use the parser. A
 * small stack keeps the frame under a page, avoiding a stack probe per call. */
#define CACHED_EXPRESSION_STACK 64

enum {
    CEXPR_NUMBER,
    CEXPR_VARIABLE,
    CEXPR_SUBSCRIPT, /* the lone variable subscript of a 1-D array element */
    CEXPR_ARRAY,     /* argc subscripts -> element */
    CEXPR_FUNCTION,  /* SIN, INT, CINT, ... of one argument */
    CEXPR_FUNCTION0, /* RND or TIMER without an argument */
    CEXPR_QB64,      /* _RGB, _RED, _ROUND, ... of argc arguments */
    CEXPR_POINT,     /* POINT(x, y) or POINT(n) */
    CEXPR_NEGATE,
    CEXPR_POWER,
    CEXPR_ADD,
    CEXPR_SUBTRACT,
    CEXPR_MULTIPLY,
    CEXPR_DIVIDE,
    CEXPR_MOD,
    CEXPR_IDIV,
    CEXPR_EQUAL,
    CEXPR_NOT_EQUAL,
    CEXPR_LESS,
    CEXPR_LESS_EQUAL,
    CEXPR_GREATER,
    CEXPR_GREATER_EQUAL,
    CEXPR_NOT,
    CEXPR_AND,
    CEXPR_OR,
    CEXPR_XOR,
    CEXPR_EQV,
    CEXPR_IMP
};

typedef struct {
    Statement *statement;
    int pos;
    int instruction_count;
    int failed;
    int depth;     /* operands on the stack after the instructions so far */
    int max_depth;
    CompiledExpressionInstruction instructions[MAX_CACHED_EXPRESSION_INSTRUCTIONS];
} CachedExpressionParser;

static int emit_cached_call(CachedExpressionParser *parser, unsigned char opcode, int token_index, int argc) {
    if (parser->instruction_count >= MAX_CACHED_EXPRESSION_INSTRUCTIONS) {
        parser->failed = 1;
        return 0;
    }
    parser->instructions[parser->instruction_count++] = (CompiledExpressionInstruction){
        .opcode = opcode,
        .token_index = token_index,
        .argc = argc
    };
    if (opcode == CEXPR_NUMBER) {
        const Token *token = &parser->statement->tokens[token_index];
        parser->instructions[parser->instruction_count - 1].number = num_from_token(token);
        parser->instructions[parser->instruction_count - 1].is_double = token->is_double;
    }

    switch (opcode) {
        case CEXPR_NUMBER: case CEXPR_VARIABLE: case CEXPR_SUBSCRIPT: case CEXPR_FUNCTION0:
            parser->depth++;
            break;
        case CEXPR_NEGATE: case CEXPR_NOT: case CEXPR_FUNCTION:
            break;
        case CEXPR_ARRAY: case CEXPR_QB64: case CEXPR_POINT:
            parser->depth += 1 - argc;
            break;
        default:
            parser->depth--;
            break;
    }
    if (parser->depth > parser->max_depth) parser->max_depth = parser->depth;
    return 1;
}

static int emit_cached_expression(CachedExpressionParser *parser, unsigned char opcode, int token_index) {
    return emit_cached_call(parser, opcode, token_index, 0);
}

static int cached_expression_failed(CachedExpressionParser *parser) {
    parser->failed = 1;
    return 0;
}

static int parse_cached_expression(CachedExpressionParser *parser);
static int parse_cached_unary(CachedExpressionParser *parser);

/* A parenthesized argument list, as the parser reads one; returns the
 * number of arguments, or -1. */
static int parse_cached_arguments(CachedExpressionParser *parser) {
    Token *tokens = parser->statement->tokens;
    if (tokens[parser->pos].type != TOKEN_LPAREN) return 0;
    parser->pos++;
    int argc = 0;
    if (tokens[parser->pos].type != TOKEN_RPAREN) {
        while (1) {
            if (!parse_cached_expression(parser)) return -1;
            argc++;
            if (tokens[parser->pos].type != TOKEN_COMMA) break;
            parser->pos++;
        }
    }
    if (tokens[parser->pos].type != TOKEN_RPAREN) return -1;
    parser->pos++;
    return argc;
}

/* Functions of one numeric argument that numeric_function_num evaluates. */
static int cached_numeric_function(TokenType type) {
    return (type >= TOKEN_ABS && type <= TOKEN_SGN) ||
           type == TOKEN_CINT || type == TOKEN_CLNG || type == TOKEN_CSNG || type == TOKEN_CDBL;
}

static int parse_cached_primary(CachedExpressionParser *parser) {
    Token *tokens = parser->statement->tokens;
    Token *token = &tokens[parser->pos];
    int token_index = parser->pos;
    if (token->type == TOKEN_NUMBER) {
        parser->pos++;
        return emit_cached_expression(parser, CEXPR_NUMBER, token_index);
    }
    if (cached_numeric_function(token->type) || token->type == TOKEN_TIMER) {
        parser->pos++;
        if (tokens[parser->pos].type != TOKEN_LPAREN) {
            if (token->type != TOKEN_RND && token->type != TOKEN_TIMER) return cached_expression_failed(parser);
            return emit_cached_expression(parser, CEXPR_FUNCTION0, token_index);
        }
        if (token->type == TOKEN_TIMER || parse_cached_arguments(parser) != 1) return cached_expression_failed(parser);
        return emit_cached_call(parser, CEXPR_FUNCTION, token_index, 1);
    }
    if (token->type == TOKEN_IDENTIFIER) {
        // Names that primary_tok reads specially are left to it.
        const char *name = token->text;
        if (is_string_var(name) ||
            strcasecmp(name, "ERR") == 0 ||
            strcasecmp(name, "ERL") == 0 ||
            strcasecmp(name, "ERDEV") == 0 ||
            strcasecmp(name, "CSRLIN") == 0 ||
            strcasecmp(name, "FREEFILE") == 0 ||
            strcasecmp(name, "POS") == 0 ||
            (name[0] == '_' && (qb64_file_function_id(name) || qb64_string_function_id(name))) ||
            ((name[0] == 'F' || name[0] == 'f') && (name[1] == 'N' || name[1] == 'n'))) {
            return cached_expression_failed(parser);
        }
        parser->pos++;

        if (name[0] == '_') {
            int function_id = qb64_function_id(name);
            // _ROL and _ROR read the declared type of a variable argument,
            // which only the parser sees, once DIM has run.
            int bit_width = 0;
            if (qb64_rotates_variable(tokens, parser->pos, function_id)) {
                if (!primitive_type_of_name(tokens[parser->pos + 1].text)) return cached_expression_failed(parser);
                bit_width = qb64_argument_bit_width(tokens, parser->pos, function_id);
                if (qb64_bit_width_unsigned) bit_width = -bit_width;
                qb64_bit_width_unsigned = 0;
            }
            int argc = function_id ? parse_cached_arguments(parser) : -1;
            if (argc < 0 || !emit_cached_call(parser, CEXPR_QB64, token_index, argc)) {
                return cached_expression_failed(parser);
            }
            parser->instructions[parser->instruction_count - 1].function_id = function_id;
            parser->instructions[parser->instruction_count - 1].bit_width = bit_width;
            return 1;
        }
        if (strcasecmp(name, "POINT") == 0) {
            int argc = tokens[parser->pos].type == TOKEN_LPAREN ? parse_cached_arguments(parser) : -1;
            if (argc != 1 && argc != 2) return cached_expression_failed(parser);
            return emit_cached_call(parser, CEXPR_POINT, token_index, argc);
        }
        if (tokens[parser->pos].type != TOKEN_LPAREN) {
            return emit_cached_expression(parser, CEXPR_VARIABLE, token_index);
        }

        // An array element; a record element such as items(2).x is left to the parser.
        int paren = parser->pos;
        int first_instruction = parser->instruction_count;
        int argc = parse_cached_arguments(parser);
        if (argc < 1 || argc > 3 || tokens[parser->pos].type == TOKEN_DOT) return cached_expression_failed(parser);
        if (tokens[paren + 1].type == TOKEN_IDENTIFIER && tokens[paren + 2].type == TOKEN_RPAREN &&
            parser->instructions[first_instruction].opcode == CEXPR_VARIABLE) {
            parser->instructions[first_instruction].opcode = CEXPR_SUBSCRIPT;
        }
        return emit_cached_call(parser, CEXPR_ARRAY, token_index, argc);
    }
    if (token->type == TOKEN_LPAREN) {
        parser->pos++;
        if (!parse_cached_expression(parser) || tokens[parser->pos].type != TOKEN_RPAREN) {
            return cached_expression_failed(parser);
        }
        parser->pos++;
        return 1;
    }
    return cached_expression_failed(parser);
}

/* ^ binds tightest, left to right; the exponent may carry its own sign. */
static int parse_cached_power(CachedExpressionParser *parser) {
    if (!parse_cached_primary(parser)) return 0;
    while (!parser->failed && parser->statement->tokens[parser->pos].type == TOKEN_POWER) {
        parser->pos++;
        TokenType next = parser->statement->tokens[parser->pos].type;
        int parsed = (next == TOKEN_MINUS || next == TOKEN_PLUS)
            ? parse_cached_unary(parser) : parse_cached_primary(parser);
        if (!parsed || !emit_cached_expression(parser, CEXPR_POWER, -1)) return 0;
    }
    return !parser->failed;
}

static int parse_cached_unary(CachedExpressionParser *parser) {
    TokenType type = parser->statement->tokens[parser->pos].type;
    if (type == TOKEN_PLUS) {
        parser->pos++;
        return parse_cached_unary(parser);
    }
    if (type == TOKEN_MINUS) {
        parser->pos++;
        if (!parse_cached_unary(parser)) return 0;
        return emit_cached_expression(parser, CEXPR_NEGATE, -1);
    }
    return parse_cached_power(parser);
}

/* * and / bind tighter than \, which binds tighter than MOD, as in QBasic. */
static int parse_cached_level(CachedExpressionParser *parser, int level) {
    if (!(level == 0 ? parse_cached_unary(parser) : parse_cached_level(parser, level - 1))) return 0;
    while (!parser->failed) {
        TokenType type = parser->statement->tokens[parser->pos].type;
        unsigned char opcode;
        if (level == 0 && type == TOKEN_STAR) opcode = CEXPR_MULTIPLY;
        else if (level == 0 && type == TOKEN_SLASH) opcode = CEXPR_DIVIDE;
        else if (level == 1 && type == TOKEN_IDIV) opcode = CEXPR_IDIV;
        else if (level == 2 && type == TOKEN_MOD) opcode = CEXPR_MOD;
        else break;

        parser->pos++;
        if (!(level == 0 ? parse_cached_unary(parser) : parse_cached_level(parser, level - 1)) ||
            !emit_cached_expression(parser, opcode, -1)) return 0;
    }
    return !parser->failed;
}

static int parse_cached_term(CachedExpressionParser *parser) {
    return parse_cached_level(parser, 2);
}

static int parse_cached_arithmetic(CachedExpressionParser *parser) {
    if (!parse_cached_term(parser)) return 0;
    while (!parser->failed) {
        TokenType type = parser->statement->tokens[parser->pos].type;
        if (type != TOKEN_PLUS && type != TOKEN_MINUS) break;
        parser->pos++;
        if (!parse_cached_term(parser) ||
            !emit_cached_expression(parser, type == TOKEN_PLUS ? CEXPR_ADD : CEXPR_SUBTRACT, -1)) return 0;
    }
    return !parser->failed;
}

static int parse_cached_relational(CachedExpressionParser *parser) {
    if (!parse_cached_arithmetic(parser)) return 0;
    while (!parser->failed) {
        TokenType type = parser->statement->tokens[parser->pos].type;
        unsigned char opcode;
        if (type == TOKEN_EQUALS) {
            opcode = CEXPR_EQUAL;
            parser->pos++;
        } else if (type == TOKEN_LESS) {
            parser->pos++;
            TokenType modifier = parser->statement->tokens[parser->pos].type;
            if (modifier == TOKEN_GREATER) {
                parser->pos++;
                opcode = CEXPR_NOT_EQUAL;
            } else if (modifier == TOKEN_EQUALS) {
                parser->pos++;
                opcode = CEXPR_LESS_EQUAL;
            } else {
                opcode = CEXPR_LESS;
            }
        } else if (type == TOKEN_GREATER) {
            parser->pos++;
            if (parser->statement->tokens[parser->pos].type == TOKEN_EQUALS) {
                parser->pos++;
                opcode = CEXPR_GREATER_EQUAL;
            } else {
                opcode = CEXPR_GREATER;
            }
        } else {
            break;
        }

        if (!parse_cached_arithmetic(parser) || !emit_cached_expression(parser, opcode, -1)) return 0;
    }
    return !parser->failed;
}

static int parse_cached_not(CachedExpressionParser *parser) {
    if (parser->statement->tokens[parser->pos].type == TOKEN_NOT) {
        parser->pos++;
        if (!parse_cached_not(parser)) return 0;
        return emit_cached_expression(parser, CEXPR_NOT, -1);
    }
    return parse_cached_relational(parser);
}

/* AND, OR, XOR, EQV and IMP, from tightest (level 0) to loosest (level 4). */
static int parse_cached_logical(CachedExpressionParser *parser, int level) {
    static const TokenType operators[] = {TOKEN_AND, TOKEN_OR, TOKEN_XOR, TOKEN_EQV, TOKEN_IMP};
    static const unsigned char opcodes[] = {CEXPR_AND, CEXPR_OR, CEXPR_XOR, CEXPR_EQV, CEXPR_IMP};
    if (!(level == 0 ? parse_cached_not(parser) : parse_cached_logical(parser, level - 1))) return 0;
    while (!parser->failed && parser->statement->tokens[parser->pos].type == operators[level]) {
        parser->pos++;
        if (!(level == 0 ? parse_cached_not(parser) : parse_cached_logical(parser, level - 1)) ||
            !emit_cached_expression(parser, opcodes[level], -1)) return 0;
    }
    return !parser->failed;
}

static int parse_cached_expression(CachedExpressionParser *parser) {
    return parse_cached_logical(parser, 4);
}

static int cached_expression_start(const Statement *statement, int pos) {
    if (pos == 0) return 1;
    switch (statement->tokens[pos - 1].type) {
        case TOKEN_EQUALS:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_UNTIL:
        case TOKEN_TO:
        case TOKEN_STEP:
        case TOKEN_THEN:
        case TOKEN_ELSE:
        case TOKEN_ELSEIF:
        case TOKEN_COMMA:
        case TOKEN_SEMICOLON:
        case TOKEN_COLON:
        case TOKEN_LPAREN:
        case TOKEN_PRINT:
        case TOKEN_SLEEP:
            return 1;
        default:
            return 0;
    }
}

void compile_statement_expressions(Statement *statement) {
    if (!statement || !statement->tokens || statement->token_count <= 0) return;
    statement->compiled_expressions = calloc((size_t)statement->token_count, sizeof(*statement->compiled_expressions));
    if (!statement->compiled_expressions) {
        fprintf(stderr, "Unable to allocate expression cache for BASIC line %d\n", statement->line_number);
        return;
    }

    for (int start = 0; start < statement->token_count; start++) {
        if (!cached_expression_start(statement, start)) continue;

        CachedExpressionParser parser = {
            .statement = statement,
            .pos = start
        };
        // A single name or number is compiled too: reading it with the full
        // parser costs a nine-level descent on every loop iteration.
        if (!parse_cached_expression(&parser) || parser.failed ||
            parser.instruction_count < 1 || parser.max_depth > CACHED_EXPRESSION_STACK) {
            continue;
        }

        TokenType end_type = statement->tokens[parser.pos].type;
        if (end_type != TOKEN_EOF && end_type != TOKEN_COLON && end_type != TOKEN_THEN &&
            end_type != TOKEN_ELSE && end_type != TOKEN_TO && end_type != TOKEN_STEP &&
            end_type != TOKEN_COMMA && end_type != TOKEN_SEMICOLON && end_type != TOKEN_RPAREN) {
            continue;
        }

        CompiledExpression *expression = malloc(sizeof(*expression));
        CompiledExpressionInstruction *instructions =
            malloc((size_t)parser.instruction_count * sizeof(*instructions));
        if (!expression || !instructions) {
            free(expression);
            free(instructions);
            fprintf(stderr, "Unable to compile expression on BASIC line %d\n", statement->line_number);
            return;
        }
        memcpy(instructions, parser.instructions, (size_t)parser.instruction_count * sizeof(*instructions));
        int has_calls = 0;
        for (int i = 0; i < parser.instruction_count; i++) {
            unsigned char opcode = parser.instructions[i].opcode;
            if (opcode == CEXPR_FUNCTION || opcode == CEXPR_FUNCTION0 || opcode == CEXPR_QB64 || opcode == CEXPR_POINT) has_calls = 1;
        }
        *expression = (CompiledExpression){
            .end_pos = parser.pos,
            .has_calls = has_calls,
            .instruction_count = parser.instruction_count,
            .instructions = instructions
        };
        statement->compiled_expressions[start] = expression;
    }
}

static void scan_procedures(void) {
    proc_generation++;
    invalidate_expression_name_cache();
    proc_count = 0;
    clear_procedure_declared_types();
    clear_static_locals();
    // DEFINT, DEFLNG, ... apply to the parameters of the procedures after
    // them, so follow them in source order here; running the program applies
    // them again as it reaches them.
    char saved_type_map[26];
    memcpy(saved_type_map, default_type_map, sizeof(saved_type_map));
    Statement *stmt = get_head();
    while (stmt) {
        if (stmt->token_count > 0) {
            Token t0 = stmt->tokens[0];
            static const struct { TokenType type; char suffix; } def_statements[] = {
                {TOKEN_DEFINT, '%'}, {TOKEN_DEFLNG, '&'}, {TOKEN_DEFSTR, '$'},
                {TOKEN_DEFSNG, '!'}, {TOKEN_DEFDBL, '#'},
            };
            for (size_t d = 0; d < sizeof(def_statements) / sizeof(def_statements[0]); d++) {
                if (t0.type != def_statements[d].type) continue;
                const char *def_ptr = stmt->raw_command;
                get_next_token(&def_ptr);
                parse_def_range(&def_ptr, def_statements[d].suffix);
            }
            if (t0.type == TOKEN_SUB || t0.type == TOKEN_FUNCTION) {
                if (proc_count >= MAX_PROCEDURES) break;
                ProcedureDef *p = &registered_procs[proc_count];
                memset(p, 0, sizeof(ProcedureDef));
                p->is_function = (t0.type == TOKEN_FUNCTION);
                p->header_stmt = stmt;

                int pos = 1;
                if (pos < stmt->token_count && stmt->tokens[pos].type == TOKEN_IDENTIFIER) {
                    strncpy(p->name, stmt->tokens[pos].text, 31);
                    for (int c = 0; p->name[c]; c++) p->name[c] = (char)toupper((unsigned char)p->name[c]);
                    pos++;

                    if (pos < stmt->token_count && stmt->tokens[pos].type == TOKEN_LPAREN) {
                        pos++;
                        while (pos < stmt->token_count && stmt->tokens[pos].type != TOKEN_RPAREN) {
                            if (stmt->tokens[pos].type == TOKEN_IDENTIFIER) {
                                if (p->param_count >= MAX_PROC_PARAMS) {
                                    report_type_declaration_error(stmt);
                                    return;
                                }
                                ProcParamDef *param = &p->params[p->param_count++];
                                param->user_type_index = -1;
                                snprintf(param->name, sizeof(param->name), "%s",
                                         stmt->tokens[pos].text);
                                for (int c = 0; param->name[c]; c++) {
                                    param->name[c] = (char)toupper((unsigned char)param->name[c]);
                                }
                                pos++;
                                if (pos < stmt->token_count &&
                                    stmt->tokens[pos].type == TOKEN_LPAREN) {
                                    param->is_array = 1;
                                    while (pos < stmt->token_count &&
                                           stmt->tokens[pos].type != TOKEN_RPAREN) pos++;
                                    if (pos < stmt->token_count) pos++;
                                }
                                if (pos < stmt->token_count &&
                                    stmt->tokens[pos].type == TOKEN_AS) {
                                    pos++;
                                    if (pos >= stmt->token_count ||
                                        stmt->tokens[pos].type != TOKEN_IDENTIFIER) {
                                        report_runtime_error(ERR_SYNTAX_ERROR);
                                        return;
                                    }
                                    UserType *parameter_type =
                                        find_user_type(stmt->tokens[pos].text);
                                    const char *type_name = stmt->tokens[pos].text;
                                    const char *combined = pos + 1 < stmt->token_count
                                        ? unsigned_type_name(type_name, stmt->tokens[pos + 1].text) : NULL;
                                    if (combined) {
                                        type_name = combined;
                                        pos++;
                                    }
                                    const char *suffix = primitive_type_suffix(type_name);
                                    if (parameter_type) {
                                        param->user_type_index =
                                            (int)(parameter_type - user_types);
                                    } else if (suffix) {
                                        size_t len = strlen(param->name);
                                        if (len > 0 && !is_type_suffix_char(param->name[len - 1]) &&
                                            len + strlen(suffix) < sizeof(param->name)) {
                                            declare_variable_type(param->name, suffix, p, 0);
                                            strcat(param->name, suffix);
                                        }
                                        if (suffix[0] == '$') param->is_string = 1;
                                    } else {
                                        report_runtime_error(ERR_SYNTAX_ERROR);
                                        return;
                                    }
                                    pos++;
                                } else {
                                    size_t len = strlen(param->name);
                                    if (len > 0 && !is_type_suffix_char(param->name[len - 1])) {
                                        int first = toupper((unsigned char)param->name[0]) - 'A';
                                        if (first >= 0 && first < 26) {
                                            char suffix = default_type_map[first];
                                            if (suffix != '\0' && len + 1 < sizeof(param->name)) {
                                                param->name[len] = suffix;
                                                param->name[len + 1] = '\0';
                                            }
                                        }
                                    }
                                    len = strlen(param->name);
                                    if (len > 0 && param->name[len - 1] == '$') {
                                        param->is_string = 1;
                                    }
                                }
                                if (pos < stmt->token_count &&
                                    stmt->tokens[pos].type == TOKEN_COMMA) pos++;
                                continue;
                            }
                            pos++;
                        }
                        if (pos < stmt->token_count && stmt->tokens[pos].type == TOKEN_RPAREN) pos++;
                    }

                    if (pos < stmt->token_count && stmt->tokens[pos].type == TOKEN_STATIC) {
                        p->is_static = 1;
                    }
                }

                p->start_stmt = stmt->next;

                Statement *curr = stmt->next;
                while (curr) {
                    if (curr->token_count > 0 && curr->tokens[0].type == TOKEN_END) {
                        if (curr->token_count > 1 && curr->tokens[1].type == (p->is_function ? TOKEN_FUNCTION : TOKEN_SUB)) {
                            p->end_stmt = curr;
                            break;
                        }
                    }
                    curr = curr->next;
                }
                if (!p->end_stmt) p->end_stmt = stmt;
                proc_count++;
            }
        }
        stmt = stmt->next;
    }
    memcpy(default_type_map, saved_type_map, sizeof(saved_type_map));
    // Names cached as numeric may now be string FUNCTIONs called without "$".
    default_type_generation++;
    if (default_type_generation == 0) default_type_generation = 1;
}

/* An argument written name() is a whole array. Returns 1 and consumes it,
 * setting *var_idx to the array's variable. Record arrays are bound by the
 * user-type code instead. */
static int array_argument_tok(TokenStream *ts, const ProcParamDef *pdef, int *var_idx) {
    Token *name = &ts->tokens[ts->pos];
    if (name->type != TOKEN_IDENTIFIER || pdef->user_type_index >= 0 ||
        ts->tokens[ts->pos + 1].type != TOKEN_LPAREN || ts->tokens[ts->pos + 2].type != TOKEN_RPAREN ||
        is_function_name(name->text)) {
        return 0;
    }
    TokenType after = ts->tokens[ts->pos + 3].type;
    if (after != TOKEN_COMMA && after != TOKEN_RPAREN && after != TOKEN_COLON && after != TOKEN_EOF) return 0;
    *var_idx = resolve_token_variable(name);
    if (*var_idx < 0) return 0;
    ts->pos += 3;
    return 1;
}

static Statement *execute_sub_call(ProcedureDef *proc, TokenStream *ts, Statement *exec_stmt) {
    if (call_stack_depth + frames_being_built >= MAX_CALL_FRAMES) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return NULL;
    }

    int frame_slot = call_stack_depth + frames_being_built;
    CallFrame *frame = &call_stack[frame_slot];
    reset_call_frame(frame);
    frames_being_built++;
    frame->proc = proc;
    frame->return_stmt = exec_stmt;

    for (int p = 0; p < proc->param_count; p++) {
        frame->byref_caller_var_idx[p] = -1;
        frame->byref_caller_frame[p] = -1;
    }

    /* Parentheses wrap the argument list only when they close at the end of
     * the statement, as in CALL Name(a, b); in "Name (a) * 2, b" they belong
     * to the first argument. */
    int has_parens = 0;
    if (ts->pos < exec_stmt->token_count && ts->tokens[ts->pos].type == TOKEN_LPAREN) {
        int depth = 0, close = ts->pos;
        for (; close < exec_stmt->token_count; close++) {
            TokenType type = ts->tokens[close].type;
            if (type == TOKEN_LPAREN) depth++;
            else if (type == TOKEN_RPAREN && --depth == 0) break;
        }
        TokenType after = close < exec_stmt->token_count ? ts->tokens[close + 1].type : TOKEN_EOF;
        if (after == TOKEN_EOF || after == TOKEN_COLON || after == TOKEN_ELSE || after == TOKEN_REM) {
            has_parens = 1;
            ts->pos++;
        }
    }

    int arg_idx = 0;
    while (ts->pos < exec_stmt->token_count && ts->tokens[ts->pos].type != TOKEN_COLON && ts->tokens[ts->pos].type != TOKEN_EOF) {
        if (has_parens && ts->tokens[ts->pos].type == TOKEN_RPAREN) break;
        if (arg_idx >= proc->param_count) break;

        ProcParamDef *pdef = &proc->params[arg_idx];
        Token t_arg = ts->tokens[ts->pos];

        /* CONST and FUNCTION names are values, never by-reference variables. */
        int is_var_id = (t_arg.type == TOKEN_IDENTIFIER) && find_named_constant(t_arg.text) < 0 &&
            !is_function_name(t_arg.text);
        int next_is_sep = (ts->pos + 1 >= exec_stmt->token_count || 
                           ts->tokens[ts->pos + 1].type == TOKEN_COMMA || 
                           ts->tokens[ts->pos + 1].type == TOKEN_RPAREN || 
                           ts->tokens[ts->pos + 1].type == TOKEN_COLON);

        int array_var_idx;
        if (array_argument_tok(ts, pdef, &array_var_idx)) {
            // name() passes the whole array by reference.
            frame->byref_caller_variable[arg_idx] = get_variable_ptr(array_var_idx);
            frame->byref_caller_var_idx[arg_idx] = array_var_idx;
            frame->byref_caller_frame[arg_idx] = call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
        } else if (is_var_id && next_is_sep) {
            int c_var_idx;
            if (pdef->user_type_index >= 0) {
                c_var_idx = find_variable_raw_name(t_arg.text);
                UserTypeInstance argument_type;
                if (!get_user_type_instance_for_root(c_var_idx, &argument_type) ||
                    argument_type.type_index != pdef->user_type_index ||
                    pdef->is_array != (argument_type.num_dims > 0)) {
                    report_runtime_error(ERR_TYPE_MISMATCH);
                    return NULL;
                }
            } else {
                c_var_idx = resolve_token_variable(&ts->tokens[ts->pos]);
            }
            ts->pos++;
            frame->byref_caller_variable[arg_idx] = get_variable_ptr(c_var_idx);
            frame->byref_caller_var_idx[arg_idx] = c_var_idx;
            frame->byref_caller_frame[arg_idx] = call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
        } else {
            frame->byref_caller_var_idx[arg_idx] = -1;
            frame->byref_caller_frame[arg_idx] = -1;

            if (pdef->user_type_index >= 0) {
                if (ts->tokens[ts->pos].type != TOKEN_LPAREN ||
                    ts->tokens[ts->pos + 1].type != TOKEN_IDENTIFIER ||
                    ts->tokens[ts->pos + 2].type != TOKEN_RPAREN) {
                    report_runtime_error(ERR_TYPE_MISMATCH);
                    return NULL;
                }
                ts->pos++;
                Token *source_token = &ts->tokens[ts->pos++];
                int source_index = find_variable_raw_name(source_token->text);
                if (ts->tokens[ts->pos++].type != TOKEN_RPAREN) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return NULL;
                }
                UserTypeInstance source_instance;
                if (!get_user_type_instance_for_root(source_index, &source_instance) ||
                    source_instance.type_index != pdef->user_type_index ||
                    source_instance.num_dims != 0) {
                    report_runtime_error(ERR_TYPE_MISMATCH);
                    return NULL;
                }
                Variable *source_root = get_variable_ptr(source_index);
                char destination_root[128];
                snprintf(destination_root, sizeof(destination_root), "%s", pdef->name);
                if (!copy_user_type_value(source_root->name, destination_root,
                        &user_types[pdef->user_type_index], 0, frame)) return NULL;
                int destination_index = find_variable_raw_name(pdef->name);
                get_or_create_frame_variable(frame, destination_index);
            } else if (pdef->is_string) {
                BasicString val = {0};
                parse_string_expression_tok_heap(ts, &val);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                basic_string_move_assign(&lv->s_value, &val);
                basic_string_release(&val);
            } else {
                Num val = evaluate_num_tok(ts);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                store_num(lv, -1, val);
            }
        }

        arg_idx++;
        if (ts->pos < exec_stmt->token_count && ts->tokens[ts->pos].type == TOKEN_COMMA) {
            ts->pos++;
        }
    }

    if (has_parens && ts->pos < exec_stmt->token_count && ts->tokens[ts->pos].type == TOKEN_RPAREN) {
        ts->pos++;
    }

    frame->return_token_idx = ts->pos;
    frames_being_built--;
    frame->return_depth = call_stack_depth;
    call_stack_depth = frame_slot + 1;
    return proc->start_stmt;
}

static void execute_shared_or_static(TokenStream *ts, int is_static);

static void execute_procedure_statements(ProcedureDef *proc) {
    int saved_line = current_executing_line;
    int saved_source_line_number = current_source_line_number;
    int saved_has_explicit_line_number = current_has_explicit_line_number;
    Statement *curr_proc_stmt = proc->start_stmt;
    int target_depth = call_stack_depth - 1;
    BlockIfFrame block_if_stack[MAX_BLOCK_IF_DEPTH];
    int block_if_depth = 0;
    SelectCaseFrame select_case_stack[MAX_SELECT_CASE_DEPTH];
    int select_case_depth = 0;
    /* FOR, WHILE and DO loops opened in this call sit above these marks on the
     * shared stacks and are dropped when the call returns, even from inside
     * a loop (EXIT SUB/FUNCTION). */
    int for_base = for_ptr, while_base = while_ptr, do_base = do_ptr;
    /* Token position to resume at when a jump lands mid-line (FOR/NEXT on one line). */
    int resume_proc_pos = 0;

    while (curr_proc_stmt && curr_proc_stmt != proc->end_stmt && call_stack_depth > target_depth && !stop_running) {
        Statement *exec_stmt = curr_proc_stmt;
        current_executing_line = exec_stmt->line_number;
        current_source_line_number = exec_stmt->source_line_number;
        current_has_explicit_line_number = exec_stmt->has_explicit_line_number;

        TokenStream ts = {exec_stmt->tokens, resume_proc_pos, exec_stmt};
        resume_proc_pos = 0;
        int jumped = 0;

        while (ts.pos < exec_stmt->token_count && !stop_running && !jumped) {
            Token *current_token = &ts.tokens[ts.pos++];
            const Token *t = current_token;

            switch (t->type) {
            case TOKEN_EOF:
            case TOKEN_COLON:
                continue;

            case TOKEN_IF: {
                Num cond_num;
                if (!evaluate_num_tok_cached(&ts, &cond_num)) cond_num = evaluate_num_tok(&ts);
                /* Truthiness only, so the Num need not become a double. */
                int cond = cond_num.kind < NUM_SINGLE ? cond_num.i != 0 : cond_num.d != 0;
                if (ts.pos < exec_stmt->token_count && (ts.tokens[ts.pos].type == TOKEN_THEN || ts.tokens[ts.pos].type == TOKEN_GOTO)) {
                    ts.pos++;
                }

                if (is_block_if_header(exec_stmt)) {
                    if (block_if_depth >= MAX_BLOCK_IF_DEPTH) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        break;
                    }
                    block_if_stack[block_if_depth++] = (BlockIfFrame){
                        .branch_taken = cond,
                        .else_seen = 0
                    };
                    if (!cond) {
                        Statement *branch = find_next_block_if_clause(exec_stmt, proc->end_stmt);
                        if (!branch) {
                            report_runtime_error(ERR_SYNTAX_ERROR);
                            break;
                        }
                        curr_proc_stmt = branch;
                        jumped = 1;
                        break;
                    }
                    continue;
                }

                if (cond) continue;

                int has_remaining_tokens = 0;
                for (int check_i = ts.pos; check_i < exec_stmt->token_count; check_i++) {
                    if (exec_stmt->tokens[check_i].type != TOKEN_EOF && exec_stmt->tokens[check_i].type != TOKEN_COLON) {
                        has_remaining_tokens = 1;
                        break;
                    }
                }

                if (has_remaining_tokens) {
                    int depth = 0;
                    while (ts.pos < exec_stmt->token_count) {
                        if (ts.tokens[ts.pos].type == TOKEN_EOF) break;
                        Token skip = ts.tokens[ts.pos++];
                        if (skip.type == TOKEN_IF) depth++;
                        else if (skip.type == TOKEN_ELSE) {
                            if (depth == 0) break;
                            depth--;
                        }
                    }
                    continue;
                } else {
                    int depth = 1;
                    Statement *s = exec_stmt->next;
                    while (s && s != proc->end_stmt && depth > 0) {
                        if (s->token_count > 0) {
                            TokenType first_t = s->tokens[0].type;
                            if (first_t == TOKEN_IF && s->tokens[s->token_count - 1].type == TOKEN_THEN) {
                                depth++;
                            } else if (first_t == TOKEN_ELSE) {
                                if (depth == 1) {
                                    curr_proc_stmt = s;
                                    jumped = 1;
                                    break;
                                }
                            } else if (first_t == TOKEN_END && s->token_count > 1 && s->tokens[1].type == TOKEN_IF) {
                                depth--;
                                if (depth == 0) {
                                    curr_proc_stmt = s->next;
                                    jumped = 1;
                                    break;
                                }
                            }
                        }
                        s = s->next;
                    }
                    if (jumped) break;
                }
                continue;
            }

            case TOKEN_ELSEIF: {
                if (!is_block_elseif_header(exec_stmt) || block_if_depth == 0) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                BlockIfFrame *frame = &block_if_stack[block_if_depth - 1];
                if (frame->branch_taken || frame->else_seen) {
                    Statement *end_if = find_block_if_end(exec_stmt, proc->end_stmt);
                    if (!end_if) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    curr_proc_stmt = end_if;
                    jumped = 1;
                    break;
                }
                double cond = evaluate_expression_tok(&ts);
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_THEN) ts.pos++;
                if (cond != 0) {
                    frame->branch_taken = 1;
                    continue;
                }
                Statement *branch = find_next_block_if_clause(exec_stmt, proc->end_stmt);
                if (!branch) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                curr_proc_stmt = branch;
                jumped = 1;
                break;
            }

            case TOKEN_ELSE: {
                if (is_block_else(exec_stmt)) {
                    if (block_if_depth == 0) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    BlockIfFrame *frame = &block_if_stack[block_if_depth - 1];
                    if (frame->branch_taken || frame->else_seen) {
                        Statement *end_if = find_block_if_end(exec_stmt, proc->end_stmt);
                        if (!end_if) {
                            report_runtime_error(ERR_SYNTAX_ERROR);
                            break;
                        }
                        curr_proc_stmt = end_if;
                        jumped = 1;
                        break;
                    }
                    frame->branch_taken = 1;
                    frame->else_seen = 1;
                    continue;
                }
                // Reaching ELSE in a single-line IF means its THEN branch ran:
                // skip the ELSE branch, as the main program does.
                ts.pos = exec_stmt->token_count;
                continue;
            }

            case TOKEN_SELECT: {
                if (!is_select_case_header(exec_stmt) || ts.pos >= exec_stmt->token_count ||
                    ts.tokens[ts.pos].type != TOKEN_CASE) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                ts.pos++; // consume CASE
                SelectCaseFrame *frame = push_select_frame(select_case_stack, &select_case_depth,
                                                           exec_stmt, ts.pos);
                if (!frame) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    break;
                }
                frame->is_string = is_string_token(&ts.tokens[ts.pos]);
                if (frame->is_string) {
                    if (!parse_string_expression_tok_heap(&ts, &frame->str_val)) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                } else {
                    frame->num_val = evaluate_expression_tok(&ts);
                }
                continue;
            }

            case TOKEN_CASE: {
                if (select_case_depth == 0) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                SelectCaseFrame *frame = &select_case_stack[select_case_depth - 1];
                if (frame->branch_taken) {
                    Statement *end_sel = find_select_case_end(exec_stmt, proc->end_stmt);
                    if (!end_sel) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    curr_proc_stmt = end_sel;
                    jumped = 1;
                    break;
                }
                if (is_case_else(exec_stmt)) {
                    frame->branch_taken = 1;
                    /* Skip only ELSE so statements after "CASE ELSE:" still run. */
                    if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_ELSE) ts.pos++;
                    continue;
                }
                int matched = evaluate_case_clause_tok(&ts, exec_stmt, frame);
                if (matched) {
                    frame->branch_taken = 1;
                    continue;
                }
                Statement *branch = find_next_case_clause(exec_stmt, proc->end_stmt);
                if (!branch) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                curr_proc_stmt = branch;
                jumped = 1;
                break;
            }

            case TOKEN_EXIT:
            case TOKEN_END:
                if (is_end_if(exec_stmt) && t->type == TOKEN_END) {
                    if (block_if_depth == 0) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    block_if_depth--;
                    ts.pos++;
                    continue;
                }
                if (is_end_select(exec_stmt) && t->type == TOKEN_END) {
                    if (select_case_depth == 0) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    select_case_depth--;
                    if (select_case_stack[select_case_depth].is_string) {
                        basic_string_release(&select_case_stack[select_case_depth].str_val);
                    }
                    ts.pos++;
                    continue;
                }
                if (t->type == TOKEN_EXIT && ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_FOR) {
                    ts.pos++;
                    if (for_ptr <= for_base) {
                        report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                        break;
                    }
                    ForLoop *loop = &for_stack[--for_ptr];
                    if (block_if_depth > loop->block_depth) block_if_depth = loop->block_depth;
                    release_select_frames(select_case_stack, &select_case_depth, loop->select_depth);
                    int end_ts_pos = loop->start_ts_pos;
                    Statement *target_stmt = skip_for_block(loop->start_stmt, end_ts_pos, &end_ts_pos);
                    ForLoop *repeat = NULL;
                    int finished = target_stmt ? finish_skipped_next(target_stmt, &end_ts_pos, &repeat) : 0;
                    if (finished < 0) break;
                    if (finished > 0) {
                        curr_proc_stmt = repeat->start_stmt;
                        resume_proc_pos = repeat->start_ts_pos;
                    } else if (!target_stmt) {
                        report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                        break;
                    } else if (end_ts_pos < target_stmt->token_count) {
                        curr_proc_stmt = target_stmt;
                        resume_proc_pos = end_ts_pos;
                    } else {
                        curr_proc_stmt = target_stmt->next;
                    }
                    jumped = 1;
                    break;
                }
                if (t->type == TOKEN_EXIT && ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_DO) {
                    ts.pos++;
                    if (do_ptr > 0) {
                        do_ptr--;
                        if (block_if_depth > do_stack[do_ptr].block_depth) block_if_depth = do_stack[do_ptr].block_depth;
                        release_select_frames(select_case_stack, &select_case_depth, do_stack[do_ptr].select_depth);
                    }
                    int end_ts_pos = ts.pos;
                    Statement *target_stmt = skip_do_block(exec_stmt, end_ts_pos, &end_ts_pos);
                    if (target_stmt) {
                        curr_proc_stmt = target_stmt;
                        jumped = 1;
                        break;
                    } else {
                        report_runtime_error(ERR_LOOP_WITHOUT_DO);
                        break;
                    }
                }
                if (ts.pos < exec_stmt->token_count && (ts.tokens[ts.pos].type == TOKEN_SUB || ts.tokens[ts.pos].type == TOKEN_FUNCTION)) {
                    release_select_frames(select_case_stack, &select_case_depth, 0);
                    if (for_ptr > for_base) for_ptr = for_base;
                    if (while_ptr > while_base) while_ptr = while_base;
                    if (do_ptr > do_base) do_ptr = do_base;
                    current_executing_line = saved_line;
                    return;
                }
                if (t->type == TOKEN_END) stop_running = 1;
                break;

            case TOKEN_DO: {
                const char *command_start_ptr = t->start_ptr;
                int has_top_cond = (ts.pos < exec_stmt->token_count &&
                                     (ts.tokens[ts.pos].type == TOKEN_WHILE || ts.tokens[ts.pos].type == TOKEN_UNTIL));
                int should_continue = 1;
                if (has_top_cond) {
                    TokenType cond_type = ts.tokens[ts.pos++].type;
                    double cond_val = evaluate_expression_tok(&ts);
                    should_continue = (cond_type == TOKEN_WHILE) ? (cond_val != 0) : (cond_val == 0);
                }
                if (should_continue) {
                    if (do_ptr == 0 || do_stack[do_ptr-1].stmt != exec_stmt || do_stack[do_ptr-1].ptr != command_start_ptr) {
                        if (do_ptr < 16) {
                            do_stack[do_ptr].stmt = exec_stmt;
                            do_stack[do_ptr].ptr = command_start_ptr;
                            do_stack[do_ptr].block_depth = block_if_depth;
                            do_stack[do_ptr].select_depth = select_case_depth;
                            do_ptr++;
                        } else {
                            report_runtime_error(ERR_OUT_OF_MEMORY);
                        }
                    }
                } else {
                    if (do_ptr > 0 && do_stack[do_ptr-1].stmt == exec_stmt && do_stack[do_ptr-1].ptr == command_start_ptr) {
                        do_ptr--;
                    }
                    int end_ts_pos = ts.pos;
                    Statement *target_stmt = skip_do_block(exec_stmt, end_ts_pos, &end_ts_pos);
                    if (target_stmt) {
                        curr_proc_stmt = target_stmt;
                        jumped = 1;
                        break;
                    } else {
                        report_runtime_error(ERR_LOOP_WITHOUT_DO);
                    }
                }
                continue;
            }

            case TOKEN_LOOP: {
                if (do_ptr > 0) {
                    int has_bottom_cond = (ts.pos < exec_stmt->token_count &&
                                            (ts.tokens[ts.pos].type == TOKEN_WHILE || ts.tokens[ts.pos].type == TOKEN_UNTIL));
                    int should_repeat = 1;
                    if (has_bottom_cond) {
                        TokenType cond_type = ts.tokens[ts.pos++].type;
                        double cond_val = evaluate_expression_tok(&ts);
                        should_repeat = (cond_type == TOKEN_WHILE) ? (cond_val != 0) : (cond_val == 0);
                    }
                    if (should_repeat) {
                        curr_proc_stmt = do_stack[do_ptr-1].stmt;
                        jumped = 1;
                        break;
                    } else {
                        do_ptr--;
                        continue;
                    }
                } else {
                    report_runtime_error(ERR_LOOP_WITHOUT_DO);
                    break;
                }
            }

            case TOKEN_WHILE: {
                const char *command_start_ptr = t->start_ptr;
                double val = evaluate_expression_tok(&ts);
                const char *cond_start = command_start_ptr;
                if (val != 0) {
                    if (while_ptr == 0 || while_stack[while_ptr-1].stmt != exec_stmt || while_stack[while_ptr-1].ptr != cond_start) {
                        if (while_ptr < 16) {
                            while_stack[while_ptr].stmt = exec_stmt;
                            while_stack[while_ptr].ptr = cond_start;
                            while_ptr++;
                        } else {
                            report_runtime_error(ERR_OUT_OF_MEMORY);
                        }
                    }
                } else {
                    if (while_ptr > 0 && while_stack[while_ptr-1].stmt == exec_stmt && while_stack[while_ptr-1].ptr == cond_start) {
                        while_ptr--;
                    }
                    int end_ts_pos = ts.pos;
                    Statement *target_stmt = skip_to_matching_token(exec_stmt, end_ts_pos, TOKEN_WHILE, TOKEN_WEND, &end_ts_pos);
                    if (target_stmt) {
                        curr_proc_stmt = target_stmt;
                        jumped = 1;
                        break;
                    } else {
                        report_runtime_error(ERR_WEND_WITHOUT_WHILE);
                    }
                }
                continue;
            }

            case TOKEN_WEND:
                if (while_ptr > 0) {
                    curr_proc_stmt = while_stack[while_ptr-1].stmt;
                    jumped = 1;
                    break;
                } else {
                    report_runtime_error(ERR_WEND_WITHOUT_WHILE);
                    break;
                }

            case TOKEN_SHARED:
            case TOKEN_STATIC:
                execute_shared_or_static(&ts, t->type == TOKEN_STATIC);
                continue;

            case TOKEN_FOR: {
                Token *var_token = &ts.tokens[ts.pos++];
                if (!for_counter_type_tok(&ts, var_token, exec_stmt->token_count)) break;
                Token eq = ts.tokens[ts.pos++];
                if (eq.type != TOKEN_EQUALS) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                Num start = evaluate_num_tok(&ts);
                ts.pos++; // skip TO
                Num end = evaluate_num_tok(&ts);
                Num step = num_i(1, NUM_INTEGER);
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_STEP) {
                    ts.pos++;
                    step = evaluate_num_tok(&ts);
                }
                
                int idx = resolve_token_variable(var_token);
                ForLoop loop;
                int runs = for_loop_begin(&loop, idx, start, end, step);
                if (runs < 0) break;

                if (!runs) {
                    int end_ts_pos = ts.pos;
                    Statement *target_stmt = skip_for_block(exec_stmt, end_ts_pos, &end_ts_pos);
                    ForLoop *repeat = NULL;
                    int finished = target_stmt ? finish_skipped_next(target_stmt, &end_ts_pos, &repeat) : 0;
                    if (finished < 0) break;
                    if (finished > 0) {
                        curr_proc_stmt = repeat->start_stmt;
                        resume_proc_pos = repeat->start_ts_pos;
                        jumped = 1;
                        break;
                    }
                    if (target_stmt) {
                        // Continue after the matching NEXT, which may share this line.
                        if (end_ts_pos < target_stmt->token_count) {
                            curr_proc_stmt = target_stmt;
                            resume_proc_pos = end_ts_pos;
                        } else {
                            curr_proc_stmt = target_stmt->next;
                        }
                        jumped = 1;
                        break;
                    }
                } else {
                    // A FOR on a variable already looping in this call restarts
                    // that loop, dropping the loops inside it.
                    for (int f = for_ptr - 1; f >= for_base; f--) {
                        if (for_stack[f].var_idx == idx) { for_ptr = f; break; }
                    }
                    if (for_ptr < 16) {
                        for_stack[for_ptr] = loop;
                        for_stack[for_ptr].start_stmt = exec_stmt;
                        for_stack[for_ptr].start_ts_pos = ts.pos;
                        for_stack[for_ptr].block_depth = block_if_depth;
                        for_stack[for_ptr].select_depth = select_case_depth;
                        for_ptr++;
                    } else {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                    }
                }
                continue;
            }

            case TOKEN_NEXT: {
                ForLoop *repeat = NULL;
                int result = run_next_list(&ts, exec_stmt, &repeat);
                if (result > 0) {
                    /* Looping back to a FOR whose line ends with the header
                     * would re-enter the statement only to fall straight
                     * through its remainder; go to the body directly. */
                    if (repeat->start_ts_pos >= repeat->start_stmt->token_count) {
                        curr_proc_stmt = repeat->start_stmt->next;
                        resume_proc_pos = 0;
                    } else {
                        curr_proc_stmt = repeat->start_stmt;
                        resume_proc_pos = repeat->start_ts_pos;
                    }
                    jumped = 1;
                    break;
                }
                continue;
            }

            case TOKEN_LET:
                current_token = &ts.tokens[ts.pos++];
                t = current_token;
                /* fall through */
            case TOKEN_IDENTIFIER: {
                // A metacommand ($RESIZE:ON, $NOPREFIX) takes the whole line and was
                // applied before the program ran; it is not a "$RESIZE:" label.
                if (t->text[0] == '$') {
                    if (strcasecmp(t->text, "$RESIZE") == 0 && !apply_resize_metacommand(t->start_ptr, 1)) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    ts.pos = exec_stmt->token_count;
                    continue;
                }
                // "Name:" is a line label only at the start of a line; after THEN or
                // another statement it calls SUB Name.
                if (current_token == ts.tokens && ts.tokens[ts.pos].type == TOKEN_COLON) {
                    ts.pos++;
                    continue;
                }
                // A SUB name is never a variable: "Name (a) * 2, b" calls it rather
                // than reading Name(a) as an array element first.
                /* With no procedures defined the lookup cannot match, so the
                 * (cached) call is skipped, as in the expression paths. */
                ProcedureDef *sub = proc_count > 0 ? find_procedure_tok(current_token) : NULL;
                if (sub && !sub->is_function) {
                    // Inside a FUNCTION a SUB runs to completion here, then the
                    // calling statement continues.
                    ts.pos = (int)(current_token - ts.tokens) + 1;
                    if (execute_sub_call(sub, &ts, exec_stmt)) {
                        CallFrame *sub_frame = &call_stack[call_stack_depth - 1];
                        execute_procedure_statements(sub);
                        call_stack_depth = sub_frame->return_depth;
                    }
                    continue;
                }
                /* Renaming to the FUNCTION's own name only happens inside a
                 * call; at module scope name_string_function_result is a
                 * no-op, so skip the call entirely. */
                if (call_stack_depth > 0) name_string_function_result(current_token);
                int idx = resolve_token_variable(current_token);
                int array_idx = -1;
                int is_member = user_type_count > 0
                    ? resolve_user_type_reference_tok(&ts, current_token, &idx,
                                                      &array_idx)
                    : 0;
                if (is_member < 0) break;
                if (!is_member) {
                    UserTypeInstance record_instance;
                    if (user_type_count > 0) {
                        int raw_index = find_existing_variable_raw_name(t->text);
                        if (raw_index >= 0 &&
                            get_user_type_instance_for_root(raw_index, &record_instance)) {
                            idx = raw_index;
                        }
                    }
                    /* parse_array_index_tok only acts on a "("; testing for
                     * one first keeps scalar assignments call-free. */
                    if (ts.pos < exec_stmt->token_count &&
                        ts.tokens[ts.pos].type == TOKEN_LPAREN) {
                        array_idx = parse_array_index_tok(&ts, idx);
                    }
                }
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_EQUALS) {
                    ts.pos++;
                    UserTypeInstance record_instance;
                    if (user_type_count > 0 &&
                        get_user_type_instance_for_root(idx, &record_instance)) {
                        assign_user_type_value_tok(&ts, idx, array_idx);
                    } else if (is_member
                            ? is_string_var(get_variable_ptr(idx)->name)
                            : is_string_identifier_tok(current_token)) {
                        BasicString value = {0};
                        if (parse_string_expression_tok_heap(&ts, &value)) {
                            assign_string_variable_move(idx, array_idx, &value);
                        }
                        basic_string_release(&value);
                    } else {
                        Num value;
                        if (!evaluate_num_tok_cached(&ts, &value)) value = evaluate_num_tok(&ts);
                        // An error while evaluating aborts the assignment, as in QBasic.
                        if (!runtime_error_occurred) set_numeric_variable_num(idx, array_idx, value);
                    }
                    continue;
                }
                ts.pos--;
                /* fall through */
            }

            default: {
                const char *command_start_ptr = t->start_ptr;
                const char *temp_ptr = command_start_ptr;
                interpret_statement_text(exec_stmt, &temp_ptr);
                while (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].start_ptr < temp_ptr) ts.pos++;
                break;
            }
            }
        }

        if (!jumped) {
            curr_proc_stmt = curr_proc_stmt->next;
        }
    }
    // An error or END can leave SELECT CASE blocks and loops open.
    release_select_frames(select_case_stack, &select_case_depth, 0);
    if (for_ptr > for_base) for_ptr = for_base;
    if (while_ptr > while_base) while_ptr = while_base;
    if (do_ptr > do_base) do_ptr = do_base;
    current_executing_line = saved_line;
    current_source_line_number = saved_source_line_number;
    current_has_explicit_line_number = saved_has_explicit_line_number;
}



static int evaluate_function_call_string_text(ProcedureDef *proc, const char **input, BasicString *out) {
    if (call_stack_depth + frames_being_built >= MAX_CALL_FRAMES) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return 0;
    }

    int frame_slot = call_stack_depth + frames_being_built;
    CallFrame *frame = &call_stack[frame_slot];
    reset_call_frame(frame);
    frames_being_built++;
    frame->proc = proc;

    for (int p = 0; p < proc->param_count; p++) {
        frame->byref_caller_var_idx[p] = -1;
        frame->byref_caller_frame[p] = -1;
    }

    const char *p = skip_whitespace_fast(*input);
    int has_parens = 0;
    if (*p == '(') {
        has_parens = 1;
        p++;
        *input = p;
    }

    int arg_idx = 0;
    while (*input && **input != ')' && **input != ':' && **input != '\0') {
        if (arg_idx >= proc->param_count) break;

        ProcParamDef *pdef = &proc->params[arg_idx];

        const char *temp = skip_whitespace_fast(*input);
        int starts_with_letter = (isalpha((unsigned char)*temp) || *temp == '_');
        char var_buf[64] = {0};
        int vlen = 0;
        while (isalnum((unsigned char)*temp) || *temp == '_' || *temp == '$' || *temp == '%' || *temp == '!' || *temp == '#' || *temp == '&' || *temp == '~') {
            if (vlen < 63) var_buf[vlen++] = *temp;
            temp++;
        }
        var_buf[vlen] = '\0';
        const char *after_var = skip_whitespace_fast(temp);
        int is_simple_var = (starts_with_letter && vlen > 0 && (*after_var == ',' || *after_var == ')' || *after_var == ':')) &&
            find_named_constant(var_buf) < 0 && !is_function_name(var_buf);

        if (is_simple_var) {
            int c_var_idx = pdef->user_type_index >= 0
                ? find_variable_raw_name(var_buf) : find_variable(var_buf);
            if (pdef->user_type_index >= 0) {
                if (!bind_user_type_parameter_byref(frame, arg_idx, pdef, c_var_idx)) return 0;
            } else {
                frame->byref_caller_variable[arg_idx] = get_variable_ptr(c_var_idx);
                frame->byref_caller_var_idx[arg_idx] = c_var_idx;
                frame->byref_caller_frame[arg_idx] =
                    call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
            }
            *input = after_var;
        } else {
            frame->byref_caller_var_idx[arg_idx] = -1;
            frame->byref_caller_frame[arg_idx] = -1;

            if (pdef->user_type_index >= 0) {
                if (!bind_user_type_parameter_value_text(input, frame, pdef)) return 0;
            } else if (pdef->is_string) {
                BasicString sval = {0};
                parse_string_expression_heap(input, &sval);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                basic_string_move_assign(&lv->s_value, &sval);
                basic_string_release(&sval);
            } else {
                Num val = evaluate_num_text(input);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                store_num(lv, -1, val);
            }
        }

        arg_idx++;
        p = skip_whitespace_fast(*input);
        if (*p == ',') {
            p++;
            *input = p;
        }
    }

    p = skip_whitespace_fast(*input);
    if (has_parens && *p == ')') {
        p++;
        *input = p;
    }

    frames_being_built--;
    frame->return_depth = call_stack_depth;
    call_stack_depth = frame_slot + 1;

    execute_procedure_statements(proc);

    for (int l = 0; l < frame->local_var_count; l++) {
        if (proc_name_match((*frame_local(frame, l)).name, proc->name) && (*frame_local(frame, l)).s_value) {
            BasicString *result = (*frame_local(frame, l)).s_value;
            if (result->data && !out->data && !out->length && !out->capacity) {
                /* Adopt the function result buffer instead of copying it. */
                *out = *result;
                memset(result, 0, sizeof(*result));
            } else {
                basic_string_append(out, result->data, result->length);
            }
            break;
        }
    }

    call_stack_depth = frame->return_depth;

    return 1;
}

static Num evaluate_function_call_numeric(ProcedureDef *proc, TokenStream *ts) {

    if (call_stack_depth + frames_being_built >= MAX_CALL_FRAMES) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return num_i(0, NUM_INTEGER);
    }

    int frame_slot = call_stack_depth + frames_being_built;
    CallFrame *frame = &call_stack[frame_slot];
    reset_call_frame(frame);
    frames_being_built++;
    frame->proc = proc;

    for (int p = 0; p < proc->param_count; p++) {
        frame->byref_caller_var_idx[p] = -1;
        frame->byref_caller_frame[p] = -1;
    }

    int has_parens = 0;
    if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
        has_parens = 1;
        ts->pos++;
    }

    int arg_idx = 0;
    while (ts->tokens[ts->pos].type != TOKEN_RPAREN && ts->tokens[ts->pos].type != TOKEN_COLON && ts->tokens[ts->pos].type != TOKEN_EOF) {
        if (arg_idx >= proc->param_count) break;

        ProcParamDef *pdef = &proc->params[arg_idx];
        Token t_arg = ts->tokens[ts->pos];

        /* CONST and FUNCTION names are values, never by-reference variables. */
        int is_var_id = (t_arg.type == TOKEN_IDENTIFIER) && find_named_constant(t_arg.text) < 0 &&
            !is_function_name(t_arg.text);
        int next_is_sep = (ts->tokens[ts->pos + 1].type == TOKEN_COMMA || ts->tokens[ts->pos + 1].type == TOKEN_RPAREN || ts->tokens[ts->pos + 1].type == TOKEN_COLON);

        int array_var_idx;
        if (array_argument_tok(ts, pdef, &array_var_idx)) {
            // name() passes the whole array by reference.
            frame->byref_caller_variable[arg_idx] = get_variable_ptr(array_var_idx);
            frame->byref_caller_var_idx[arg_idx] = array_var_idx;
            frame->byref_caller_frame[arg_idx] = call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
        } else if (is_var_id && next_is_sep) {
            int c_var_idx = pdef->user_type_index >= 0
                ? find_variable_raw_name(t_arg.text)
                : resolve_token_variable(&ts->tokens[ts->pos]);
            ts->pos++;
            if (pdef->user_type_index >= 0) {
                if (!bind_user_type_parameter_byref(frame, arg_idx, pdef, c_var_idx)) return num_i(0, NUM_INTEGER);
            } else {
                frame->byref_caller_variable[arg_idx] = get_variable_ptr(c_var_idx);
                frame->byref_caller_var_idx[arg_idx] = c_var_idx;
                frame->byref_caller_frame[arg_idx] =
                    call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
            }
        } else {
            frame->byref_caller_var_idx[arg_idx] = -1;
            frame->byref_caller_frame[arg_idx] = -1;

            if (pdef->user_type_index >= 0) {
                if (!bind_user_type_parameter_value_tok(ts, frame, pdef)) return num_i(0, NUM_INTEGER);
            } else if (pdef->is_string) {
                BasicString val = {0};
                parse_string_expression_tok_heap(ts, &val);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                basic_string_move_assign(&lv->s_value, &val);
                basic_string_release(&val);
            } else {
                Num val = evaluate_num_tok(ts);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                store_num(lv, -1, val);
            }
        }

        arg_idx++;
        if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
    }

    if (has_parens && ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;

    frames_being_built--;
    frame->return_depth = call_stack_depth;
    call_stack_depth = frame_slot + 1;

    execute_procedure_statements(proc);

    Num result = num_f(0, NUM_SINGLE);
    for (int l = 0; l < frame->local_var_count; l++) {
        if (proc_name_match((*frame_local(frame, l)).name, proc->name)) {
            result = load_num(frame_local(frame, l), -1);
            break;
        }
    }
    result = num_as_name_type(result, proc->name);

    call_stack_depth = frame->return_depth;

    return result;
}

static int evaluate_function_call_string(ProcedureDef *proc, TokenStream *ts, BasicString *out) {
    if (call_stack_depth + frames_being_built >= MAX_CALL_FRAMES) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return 0;
    }

    int frame_slot = call_stack_depth + frames_being_built;
    CallFrame *frame = &call_stack[frame_slot];
    reset_call_frame(frame);
    frames_being_built++;
    frame->proc = proc;

    for (int p = 0; p < proc->param_count; p++) {
        frame->byref_caller_var_idx[p] = -1;
        frame->byref_caller_frame[p] = -1;
    }

    int has_parens = 0;
    if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
        has_parens = 1;
        ts->pos++;
    }

    int arg_idx = 0;
    while (ts->tokens[ts->pos].type != TOKEN_RPAREN && ts->tokens[ts->pos].type != TOKEN_COLON && ts->tokens[ts->pos].type != TOKEN_EOF) {
        if (arg_idx >= proc->param_count) break;

        ProcParamDef *pdef = &proc->params[arg_idx];
        Token t_arg = ts->tokens[ts->pos];

        /* CONST and FUNCTION names are values, never by-reference variables. */
        int is_var_id = (t_arg.type == TOKEN_IDENTIFIER) && find_named_constant(t_arg.text) < 0 &&
            !is_function_name(t_arg.text);
        int next_is_sep = (ts->tokens[ts->pos + 1].type == TOKEN_COMMA || ts->tokens[ts->pos + 1].type == TOKEN_RPAREN || ts->tokens[ts->pos + 1].type == TOKEN_COLON);

        int array_var_idx;
        if (array_argument_tok(ts, pdef, &array_var_idx)) {
            // name() passes the whole array by reference.
            frame->byref_caller_variable[arg_idx] = get_variable_ptr(array_var_idx);
            frame->byref_caller_var_idx[arg_idx] = array_var_idx;
            frame->byref_caller_frame[arg_idx] = call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
        } else if (is_var_id && next_is_sep) {
            int c_var_idx = pdef->user_type_index >= 0
                ? find_variable_raw_name(t_arg.text)
                : resolve_token_variable(&ts->tokens[ts->pos]);
            ts->pos++;
            if (pdef->user_type_index >= 0) {
                if (!bind_user_type_parameter_byref(frame, arg_idx, pdef, c_var_idx)) return 0;
            } else {
                frame->byref_caller_variable[arg_idx] = get_variable_ptr(c_var_idx);
                frame->byref_caller_var_idx[arg_idx] = c_var_idx;
                frame->byref_caller_frame[arg_idx] =
                    call_stack_depth > 0 ? (call_stack_depth - 1) : -1;
            }
        } else {
            frame->byref_caller_var_idx[arg_idx] = -1;
            frame->byref_caller_frame[arg_idx] = -1;

            if (pdef->user_type_index >= 0) {
                if (!bind_user_type_parameter_value_tok(ts, frame, pdef)) return 0;
            } else if (pdef->is_string) {
                BasicString val = {0};
                parse_string_expression_tok_heap(ts, &val);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                basic_string_move_assign(&lv->s_value, &val);
                basic_string_release(&val);
            } else {
                Num val = evaluate_num_tok(ts);
                Variable *lv = &(*frame_local(frame, frame->local_var_count++));
                memset(lv, 0, sizeof(Variable));
                snprintf(lv->name, sizeof(lv->name), "%s", pdef->name);
                store_num(lv, -1, val);
            }
        }

        arg_idx++;
        if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
    }

    if (has_parens && ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;

    frames_being_built--;
    frame->return_depth = call_stack_depth;
    call_stack_depth = frame_slot + 1;

    execute_procedure_statements(proc);

    for (int l = 0; l < frame->local_var_count; l++) {
        if (proc_name_match((*frame_local(frame, l)).name, proc->name) && (*frame_local(frame, l)).s_value) {
            BasicString *result = (*frame_local(frame, l)).s_value;
            if (result->data && !out->data && !out->length && !out->capacity) {
                /* Adopt the function result buffer instead of copying it. */
                *out = *result;
                memset(result, 0, sizeof(*result));
            } else {
                basic_string_append(out, result->data, result->length);
            }
            break;
        }
    }

    call_stack_depth = frame->return_depth;

    return 1;
}

/* Graphics colors are palette indexes or, in 32-bit mode, &HAARRGGBB values.
 * Negative LONG values such as &HFF0000FF map to the same bit pattern. */
static unsigned int color_from_value(double value) {
    double v = fmod(trunc(value), 4294967296.0);
    if (v < 0) v += 4294967296.0;
    return (unsigned int)v;
}

enum {
    QB64_FN_NONE, QB64_FN_RGB32, QB64_FN_RGBA32, QB64_FN_RGB, QB64_FN_RGBA,
    QB64_FN_RED32, QB64_FN_GREEN32, QB64_FN_BLUE32, QB64_FN_ALPHA32,
    QB64_FN_RED, QB64_FN_GREEN, QB64_FN_BLUE, QB64_FN_ALPHA,
    QB64_FN_PI, QB64_FN_WIDTH, QB64_FN_HEIGHT,
    QB64_FN_KEYDOWN, QB64_FN_KEYHIT, QB64_FN_MOUSEX, QB64_FN_MOUSEY,
    QB64_FN_MOUSEBUTTON, QB64_FN_MOUSEINPUT, QB64_FN_MOUSEWHEEL, QB64_FN_COPYIMAGE,
    QB64_FN_HSB32, QB64_FN_HSBA32, QB64_FN_HUE32, QB64_FN_SATURATION32, QB64_FN_BRIGHTNESS32,
    QB64_FN_PIXELSIZE, QB64_FN_DEFAULTCOLOR, QB64_FN_BACKGROUNDCOLOR, QB64_FN_PALETTECOLOR,
    QB64_FN_FONTWIDTH, QB64_FN_FONTHEIGHT, QB64_FN_SCREENX, QB64_FN_SCREENY, QB64_FN_SCREENEXISTS,
    QB64_FN_DESKTOPWIDTH, QB64_FN_DESKTOPHEIGHT, QB64_FN_DESKTOPREFRESHRATE, QB64_FN_WINDOWHASFOCUS,
    QB64_FN_MOUSEHIDDEN, QB64_FN_MOUSEMOVEMENTX, QB64_FN_MOUSEMOVEMENTY, QB64_FN_MOUSEDISABLED,
    QB64_FN_RESIZE, QB64_FN_RESIZEWIDTH, QB64_FN_RESIZEHEIGHT, QB64_FN_ROUND, QB64_FN_PRINTMODE,
    QB64_FN_FULLSCREEN, QB64_FN_SNDPLAYING, QB64_FN_SNDPAUSED, QB64_FN_SNDLEN, QB64_FN_SNDGETPOS,
    // Math, from QB64 Phoenix Edition.
    QB64_FN_MIN, QB64_FN_MAX, QB64_FN_CLAMP, QB64_FN_CEIL, QB64_FN_HYPOT, QB64_FN_ATAN2,
    QB64_FN_ACOS, QB64_FN_ASIN, QB64_FN_ACOSH, QB64_FN_ASINH, QB64_FN_ATANH,
    QB64_FN_COSH, QB64_FN_SINH, QB64_FN_TANH, QB64_FN_SEC, QB64_FN_CSC, QB64_FN_COT,
    QB64_FN_ARCSEC, QB64_FN_ARCCSC, QB64_FN_ARCCOT, QB64_FN_SECH, QB64_FN_CSCH, QB64_FN_COTH,
    QB64_FN_D2R, QB64_FN_R2D, QB64_FN_D2G, QB64_FN_G2D, QB64_FN_R2G, QB64_FN_G2R,
    // Bits.
    QB64_FN_SHL, QB64_FN_SHR, QB64_FN_ROL, QB64_FN_ROR,
    QB64_FN_SETBIT, QB64_FN_RESETBIT, QB64_FN_TOGGLEBIT, QB64_FN_READBIT,
    // Text.
    QB64_FN_UFONTHEIGHT, QB64_FN_ULINESPACING, QB64_FN_MAPUNICODE, QB64_FN_CONTROLCHR, QB64_FN_BLINK,
    // Timers and process control.
    QB64_FN_FREETIMER, QB64_FN_EXIT
};

static int qb64_function_id(const char *name) {
    static const struct { const char *name; int id; } functions[] = {
        {"_RGB32", QB64_FN_RGB32}, {"_RGBA32", QB64_FN_RGBA32},
        {"_RGB", QB64_FN_RGB}, {"_RGBA", QB64_FN_RGBA},
        {"_RED32", QB64_FN_RED32}, {"_GREEN32", QB64_FN_GREEN32},
        {"_BLUE32", QB64_FN_BLUE32}, {"_ALPHA32", QB64_FN_ALPHA32},
        {"_RED", QB64_FN_RED}, {"_GREEN", QB64_FN_GREEN},
        {"_BLUE", QB64_FN_BLUE}, {"_ALPHA", QB64_FN_ALPHA},
        {"_PI", QB64_FN_PI}, {"_WIDTH", QB64_FN_WIDTH}, {"_HEIGHT", QB64_FN_HEIGHT},
        {"_KEYDOWN", QB64_FN_KEYDOWN}, {"_KEYHIT", QB64_FN_KEYHIT},
        {"_MOUSEX", QB64_FN_MOUSEX}, {"_MOUSEY", QB64_FN_MOUSEY},
        {"_MOUSEBUTTON", QB64_FN_MOUSEBUTTON}, {"_MOUSEINPUT", QB64_FN_MOUSEINPUT},
        {"_MOUSEWHEEL", QB64_FN_MOUSEWHEEL},
        {"_HSB32", QB64_FN_HSB32}, {"_HSBA32", QB64_FN_HSBA32}, {"_HUE32", QB64_FN_HUE32},
        {"_SATURATION32", QB64_FN_SATURATION32}, {"_BRIGHTNESS32", QB64_FN_BRIGHTNESS32},
        {"_PIXELSIZE", QB64_FN_PIXELSIZE}, {"_DEFAULTCOLOR", QB64_FN_DEFAULTCOLOR},
        {"_BACKGROUNDCOLOR", QB64_FN_BACKGROUNDCOLOR}, {"_PALETTECOLOR", QB64_FN_PALETTECOLOR},
        {"_FONTWIDTH", QB64_FN_FONTWIDTH}, {"_FONTHEIGHT", QB64_FN_FONTHEIGHT},
        {"_SCREENX", QB64_FN_SCREENX}, {"_SCREENY", QB64_FN_SCREENY},
        {"_SCREENEXISTS", QB64_FN_SCREENEXISTS}, {"_DESKTOPWIDTH", QB64_FN_DESKTOPWIDTH},
        {"_DESKTOPHEIGHT", QB64_FN_DESKTOPHEIGHT}, {"_DESKTOPREFRESHRATE", QB64_FN_DESKTOPREFRESHRATE},
        {"_WINDOWHASFOCUS", QB64_FN_WINDOWHASFOCUS},
        {"_MOUSEHIDDEN", QB64_FN_MOUSEHIDDEN}, {"_MOUSEMOVEMENTX", QB64_FN_MOUSEMOVEMENTX},
        {"_MOUSEMOVEMENTY", QB64_FN_MOUSEMOVEMENTY}, {"_MOUSEDISABLED", QB64_FN_MOUSEDISABLED}, {"_COPYIMAGE", QB64_FN_COPYIMAGE},
        {"_RESIZE", QB64_FN_RESIZE}, {"_RESIZEWIDTH", QB64_FN_RESIZEWIDTH},
        {"_RESIZEHEIGHT", QB64_FN_RESIZEHEIGHT}, {"_ROUND", QB64_FN_ROUND},
        {"_PRINTMODE", QB64_FN_PRINTMODE}, {"_FULLSCREEN", QB64_FN_FULLSCREEN},
        {"_UFONTHEIGHT", QB64_FN_UFONTHEIGHT}, {"_ULINESPACING", QB64_FN_ULINESPACING},
        {"_MAPUNICODE", QB64_FN_MAPUNICODE}, {"_CONTROLCHR", QB64_FN_CONTROLCHR}, {"_BLINK", QB64_FN_BLINK},
        {"_FREETIMER", QB64_FN_FREETIMER}, {"_EXIT", QB64_FN_EXIT},
        {"_SNDPLAYING", QB64_FN_SNDPLAYING}, {"_SNDPAUSED", QB64_FN_SNDPAUSED},
        {"_SNDLEN", QB64_FN_SNDLEN}, {"_SNDGETPOS", QB64_FN_SNDGETPOS},
        {"_MIN", QB64_FN_MIN}, {"_MAX", QB64_FN_MAX}, {"_CLAMP", QB64_FN_CLAMP},
        {"_CEIL", QB64_FN_CEIL}, {"_HYPOT", QB64_FN_HYPOT}, {"_ATAN2", QB64_FN_ATAN2},
        {"_ACOS", QB64_FN_ACOS}, {"_ASIN", QB64_FN_ASIN}, {"_ACOSH", QB64_FN_ACOSH},
        {"_ASINH", QB64_FN_ASINH}, {"_ATANH", QB64_FN_ATANH}, {"_COSH", QB64_FN_COSH},
        {"_SINH", QB64_FN_SINH}, {"_TANH", QB64_FN_TANH}, {"_SEC", QB64_FN_SEC},
        {"_CSC", QB64_FN_CSC}, {"_COT", QB64_FN_COT}, {"_ARCSEC", QB64_FN_ARCSEC},
        {"_ARCCSC", QB64_FN_ARCCSC}, {"_ARCCOT", QB64_FN_ARCCOT}, {"_SECH", QB64_FN_SECH},
        {"_CSCH", QB64_FN_CSCH}, {"_COTH", QB64_FN_COTH}, {"_D2R", QB64_FN_D2R},
        {"_R2D", QB64_FN_R2D}, {"_D2G", QB64_FN_D2G}, {"_G2D", QB64_FN_G2D},
        {"_R2G", QB64_FN_R2G}, {"_G2R", QB64_FN_G2R},
        {"_SHL", QB64_FN_SHL}, {"_SHR", QB64_FN_SHR}, {"_ROL", QB64_FN_ROL}, {"_ROR", QB64_FN_ROR},
        {"_SETBIT", QB64_FN_SETBIT}, {"_RESETBIT", QB64_FN_RESETBIT},
        {"_TOGGLEBIT", QB64_FN_TOGGLEBIT}, {"_READBIT", QB64_FN_READBIT},
    };
    if (name[0] != '_') return QB64_FN_NONE;
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        if (strcasecmp(name, functions[i].name) == 0) return functions[i].id;
    }
    return QB64_FN_NONE;
}

static int clamp_color_channel(double value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (int)lround(value);
}

static double qb64_function_value(int id, const double *args, int argc) {
    int r, g, b, a;
    switch (id) {
        case QB64_FN_RGB32:
            // _RGB32(r, g, b[, a]) or _RGB32(gray[, a])
            if (argc == 1 || argc == 2) {
                r = g = b = clamp_color_channel(args[0]);
                a = argc == 2 ? clamp_color_channel(args[1]) : 255;
            } else if (argc == 3 || argc == 4) {
                r = clamp_color_channel(args[0]);
                g = clamp_color_channel(args[1]);
                b = clamp_color_channel(args[2]);
                a = argc == 4 ? clamp_color_channel(args[3]) : 255;
            } else {
                break;
            }
            last_expression_is_double = 1;
            return (double)(((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b);
        case QB64_FN_RGBA32:
        case QB64_FN_RGB:
        case QB64_FN_RGBA: {
            int need = id == QB64_FN_RGB ? 3 : 4;
            if (argc != need) break;
            r = clamp_color_channel(args[0]);
            g = clamp_color_channel(args[1]);
            b = clamp_color_channel(args[2]);
            a = need == 4 ? clamp_color_channel(args[3]) : 255;
            last_expression_is_double = 1;
            if (id == QB64_FN_RGBA32) {
                return (double)(((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b);
            }
            return (double)graphics_match_color(r, g, b, a);
        }
        case QB64_FN_RED32:
        case QB64_FN_GREEN32:
        case QB64_FN_BLUE32:
        case QB64_FN_ALPHA32: {
            if (argc != 1) break;
            unsigned int c = color_from_value(args[0]);
            int shift = id == QB64_FN_RED32 ? 16 : id == QB64_FN_GREEN32 ? 8 : id == QB64_FN_BLUE32 ? 0 : 24;
            return (double)((c >> shift) & 0xFF);
        }
        case QB64_FN_RED:
        case QB64_FN_GREEN:
        case QB64_FN_BLUE:
        case QB64_FN_ALPHA:
            if (argc < 1) break;
            graphics_color_components(color_from_value(args[0]), &r, &g, &b, &a);
            return id == QB64_FN_RED ? r : id == QB64_FN_GREEN ? g : id == QB64_FN_BLUE ? b : a;
        case QB64_FN_PI:
            last_expression_is_double = 1;
            return M_PI * (argc >= 1 ? args[0] : 1.0);
        case QB64_FN_WIDTH:
        case QB64_FN_HEIGHT: {
            // _WIDTH/_HEIGHT[(handle)]: of the current destination, or of an image.
            if (!graphics_is_active()) return id == QB64_FN_WIDTH ? 80 : 25;
            int size = graphics_image_size(argc >= 1 ? round_to_int(args[0]) : 1, id == QB64_FN_HEIGHT);
            if (size < 0) break;
            return size;
        }
        case QB64_FN_COPYIMAGE: {
            if (!graphics_is_active()) init_graphics();
            int handle = graphics_copyimage(argc >= 1 ? round_to_int(args[0]) : graphics_get_source());
            if (handle == -1 && argc >= 1 && !graphics_valid_handle(round_to_int(args[0]))) break;
            return handle;
        }
        // Keyboard and mouse state comes from the window; without one it reads as idle.
        case QB64_FN_KEYDOWN:
            if (argc != 1) break;
            return graphics_is_active() && graphics_keydown(round_to_int(args[0])) ? -1 : 0;
        case QB64_FN_KEYHIT:
            return graphics_is_active() ? graphics_keyhit() : 0;
        case QB64_FN_MOUSEX:
            return graphics_is_active() ? graphics_mouse_x() : 0;
        case QB64_FN_MOUSEY:
            return graphics_is_active() ? graphics_mouse_y() : 0;
        case QB64_FN_MOUSEBUTTON:
            if (argc != 1) break;
            return graphics_is_active() ? graphics_mouse_button(round_to_int(args[0])) : 0;
        case QB64_FN_MOUSEINPUT:
            return graphics_is_active() ? graphics_mouse_input() : 0;
        case QB64_FN_MOUSEWHEEL:
            return graphics_is_active() ? graphics_mouse_wheel() : 0;
        case QB64_FN_HSB32:
        case QB64_FN_HSBA32:
            if (argc != (id == QB64_FN_HSB32 ? 3 : 4)) break;
            last_expression_is_double = 1;
            return (double)graphics_hsb_to_rgb(args[0], args[1], args[2], id == QB64_FN_HSB32 ? 100 : args[3]);
        case QB64_FN_HUE32:
        case QB64_FN_SATURATION32:
        case QB64_FN_BRIGHTNESS32: {
            if (argc != 1) break;
            double hue, sat, bri;
            graphics_rgb_to_hsb(color_from_value(args[0]), &hue, &sat, &bri);
            last_expression_is_double = 1;
            return id == QB64_FN_HUE32 ? hue : id == QB64_FN_SATURATION32 ? sat : bri;
        }
        case QB64_FN_PIXELSIZE:
        case QB64_FN_DEFAULTCOLOR:
        case QB64_FN_BACKGROUNDCOLOR: {
            // [(handle)]: of the current destination when left out.
            if (!graphics_is_active()) init_graphics();
            int handle = argc >= 1 ? round_to_int(args[0]) : graphics_get_dest_handle();
            if (id == QB64_FN_PIXELSIZE) {
                int size = graphics_pixel_size(handle);
                if (size < 0) break;
                return size;
            }
            int ok;
            unsigned int color = graphics_image_color(handle, id == QB64_FN_BACKGROUNDCOLOR, &ok);
            if (!ok) break;
            last_expression_is_double = 1;
            return (double)color;
        }
        case QB64_FN_PALETTECOLOR: {
            // _PALETTECOLOR(index[, handle]): the 32-bit color of a palette entry.
            if (argc < 1 || argc > 2) break;
            if (!graphics_is_active()) init_graphics();
            unsigned int color;
            if (!graphics_palette_color(round_to_int(args[0]), argc == 2 ? round_to_int(args[1]) : graphics_get_dest_handle(), &color)) break;
            last_expression_is_double = 1;
            return (double)color;
        }
        case QB64_FN_FONTWIDTH:
        case QB64_FN_FONTHEIGHT: {
            if (!graphics_is_active()) init_graphics();
            int size = graphics_font_size(argc >= 1 ? round_to_int(args[0]) : -1, id == QB64_FN_FONTHEIGHT);
            if (size < 0) break;
            return size;
        }
        case QB64_FN_UFONTHEIGHT:
        case QB64_FN_ULINESPACING: {
            if (!graphics_is_active()) init_graphics();
            int size = graphics_ufont_metric(argc >= 1 ? round_to_int(args[0]) : -1, id == QB64_FN_ULINESPACING);
            if (size < 0) break;
            return size;
        }
        case QB64_FN_MAPUNICODE: {
            if (argc != 1) break;
            int code = graphics_mapunicode_get(round_to_int(args[0]));
            if (code < 0) break;
            return code;
        }
        case QB64_FN_CONTROLCHR:
            return graphics_controlchr() ? 0 : -1;
        case QB64_FN_BLINK:
            return graphics_blink() ? -1 : 0;
        case QB64_FN_FREETIMER:
            // _FREETIMER hands out timer numbers for ON TIMER(t, n) GOSUB;
            // it gives 0 when they are all taken.
            return alloc_freetimer();
        case QB64_FN_EXIT:
            // _EXIT reads -1 once after the user closes the window, then 0.
            return graphics_take_exit_pending() ? -1 : 0;
        case QB64_FN_SCREENX:
        case QB64_FN_SCREENY:
            return graphics_is_active() ? graphics_window_position(id == QB64_FN_SCREENY) : 0;
        case QB64_FN_SCREENEXISTS:
            return graphics_is_active() ? -1 : 0;
        case QB64_FN_DESKTOPWIDTH:
        case QB64_FN_DESKTOPHEIGHT:
            return graphics_desktop_size(id == QB64_FN_DESKTOPHEIGHT);
        case QB64_FN_DESKTOPREFRESHRATE:
            return graphics_desktop_refresh_rate();
        case QB64_FN_WINDOWHASFOCUS:
            return graphics_is_active() ? graphics_window_has_focus() : 0;
        case QB64_FN_MOUSEHIDDEN:
            return graphics_is_active() ? graphics_mouse_hidden() : 0;
        case QB64_FN_MOUSEMOVEMENTX:
        case QB64_FN_MOUSEMOVEMENTY:
            return graphics_is_active() ? graphics_mouse_movement(id == QB64_FN_MOUSEMOVEMENTY) : 0;
        case QB64_FN_MOUSEDISABLED:
            // There is no mouse to read without a window.
            return graphics_is_active() ? 0 : -1;
        case QB64_FN_RESIZE:
            return graphics_is_active() ? graphics_resize_event() : 0;
        case QB64_FN_RESIZEWIDTH:
            return graphics_is_active() ? graphics_resize_width() : 0;
        case QB64_FN_RESIZEHEIGHT:
            return graphics_is_active() ? graphics_resize_height() : 0;
        case QB64_FN_PRINTMODE: {
            // _PRINTMODE[(handle)]: 1 _KEEPBACKGROUND, 2 _ONLYBACKGROUND, 3 _FILLBACKGROUND.
            if (!graphics_is_active()) return PRINTMODE_FILL;
            int mode = graphics_printmode_for(argc >= 1 ? round_to_int(args[0]) : graphics_get_dest_handle(), 0);
            if (!mode) break;
            return mode;
        }
        case QB64_FN_FULLSCREEN:
            // _FULLSCREEN: 0 off, 1 _STRETCH, 2 _SQUAREPIXELS.
            return graphics_fullscreen_mode();
        // _SNDPLAYING(h) and _SNDPAUSED(h) are -1 or 0; _SNDLEN(h) and
        // _SNDGETPOS(h) are seconds.
        case QB64_FN_SNDPLAYING:
        case QB64_FN_SNDPAUSED:
        case QB64_FN_SNDLEN:
        case QB64_FN_SNDGETPOS: {
            if (argc != 1) break;
            int handle = round_to_int(args[0]);
            if (!audio_sound_valid(handle)) break;
            if (id == QB64_FN_SNDPLAYING) return audio_sound_playing(handle) ? -1 : 0;
            if (id == QB64_FN_SNDPAUSED) return audio_sound_paused(handle) ? -1 : 0;
            if (id == QB64_FN_SNDLEN) return audio_sound_length(handle);
            return audio_sound_position(handle);
        }
    }
    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
    return 0;
}

/* POINT(n): 0/1 physical x/y and 2/3 logical (WINDOW) x/y of the last point. */
static double graphics_cursor_query(int n) {
    double wx, wy;
    int px, py;
    get_graphics_cursor(&wx, &wy);
    get_graphics_cursor_physical(&px, &py);
    if (n == 0) return px;
    if (n == 1) return py;
    if (n == 2) return wx;
    if (n == 3) return wy;
    return 0;
}

/* Kept out of line: its string buffers would give primary_tok a frame of
 * several pages and a stack probe on every call. */
static __attribute__((noinline)) double builtin_function_tok(TokenStream *ts, Token *token);

/* Reads the parenthesized argument of a one-argument function. */
static int function_argument_tok(TokenStream *ts, Num *argument) {
    if (ts->tokens[ts->pos].type != TOKEN_LPAREN) return 0;
    ts->pos++;
    *argument = evaluate_num_tok(ts);
    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
    return 1;
}

/* The numeric functions of builtin_function_tok that need no parser state;
 * has_arg is 0 for RND or TIMER without an argument. */
static double math_function_value(TokenType type, double arg, int has_arg) {
    switch (type) {
        case TOKEN_ABS: return fabs(arg);
        case TOKEN_SQR:
            // QBasic has no NaN: an invalid argument is an error.
            if (arg < 0) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                return 0;
            }
            return sqrt(arg);
        case TOKEN_SIN: return sin(arg);
        case TOKEN_COS: return cos(arg);
        case TOKEN_TAN: return tan(arg);
        case TOKEN_ATN: return atan(arg);
        case TOKEN_EXP: return num_checked_float(exp(arg));
        case TOKEN_LOG:
            if (arg <= 0) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                return 0;
            }
            return log(arg);
        case TOKEN_INT: return floor(arg);
        case TOKEN_FIX: return (arg >= 0) ? floor(arg) : ceil(arg);
        case TOKEN_SGN: return (arg > 0) - (arg < 0);
        case TOKEN_TIMER: return seconds_since_midnight();
        case TOKEN_RND:
            if (has_arg && arg < 0) {
                rnd_seed = (unsigned int)(-(long)arg);
                return basika_rnd_next();
            }
            if (has_arg && arg == 0) return last_rnd_value;
            return basika_rnd_next();
        default: return 0;
    }
}

/* A function of one numeric argument (see cached_numeric_function). The
 * conversions and ABS, SGN, INT and FIX keep integer kinds exact. */
static Num numeric_function_num(TokenType type, Num argument) {
    switch (type) {
        case TOKEN_CINT: return num_convert_integer(argument, VT_INTEGER);
        case TOKEN_CLNG: return num_convert_integer(argument, VT_LONG);
        case TOKEN_CSNG: return num_f((double)(float)num_to_double(argument), NUM_SINGLE);
        case TOKEN_CDBL:
            last_expression_is_double = 1;
            return num_f(num_to_double(argument), NUM_DOUBLE);
        case TOKEN_ABS:
            if (num_is_int(argument)) return argument.kind != NUM_UINT64 && argument.i < 0 ? num_negate(argument) : argument;
            return num_f(fabs(argument.d), argument.kind);
        case TOKEN_SGN: return num_i(num_compare(argument, num_i(0, NUM_INTEGER)), NUM_INTEGER);
        case TOKEN_INT: return num_is_int(argument) ? argument : num_f(floor(argument.d), argument.kind);
        case TOKEN_FIX: return num_is_int(argument) ? argument : num_f(trunc(argument.d), argument.kind);
        default: return num_d(math_function_value(type, num_to_double(argument), 1));
    }
}

/* The QB64 functions whose answer depends on the type of their arguments,
 * or that must not round an _INTEGER64 through a double: sets *result and
 * returns 1, or returns 0 for a function of plain doubles. */
static int qb64_function_typed(int function_id, const Num *args, int argc, Num *result);


static int qb64_bit_width(Num value) {
    if (qb64_bit_width_hint) return qb64_bit_width_hint;
    if (value.kind == NUM_INTEGER) return 16;
    if (value.kind == NUM_LONG) return 32;
    return 64;
}

static int var_type_bit_width(int vt);

/* _ROL(x, n) or _ROR(x, n) of a lone variable x. tokens[pos] is the "(". */
static int qb64_rotates_variable(const Token *tokens, int pos, int function_id) {
    return (function_id == QB64_FN_ROL || function_id == QB64_FN_ROR) &&
           tokens[pos].type == TOKEN_LPAREN && tokens[pos + 1].type == TOKEN_IDENTIFIER &&
           tokens[pos + 2].type == TOKEN_COMMA;
}

/* For _ROL/_ROR: the declared width of the first argument when it is a
 * lone variable, f(x, ...), else 0. tokens[pos] is the "(". */
static int qb64_argument_bit_width(const Token *tokens, int pos, int function_id) {
    qb64_bit_width_unsigned = 0;
    if (!qb64_rotates_variable(tokens, pos, function_id)) return 0;
    // A name DIMmed AS a type stands for the name with that type's suffix.
    const char *name = tokens[pos + 1].text;
    char typed[80];
    const char *suffix = primitive_type_of_name(name) ? NULL : find_declared_suffix(name);
    if (suffix) {
        snprintf(typed, sizeof(typed), "%s%s", name, suffix);
        name = typed;
    }
    int vt = var_type_of_name(name);
    qb64_bit_width_unsigned = vt == VT_UBYTE || vt == VT_UINTEGER || vt == VT_ULONG || vt == VT_UINT64;
    return var_type_bit_width(vt);
}

static int var_type_bit_width(int vt) {
    switch (vt) {
        case VT_BYTE: case VT_UBYTE: return 8;
        case VT_INTEGER: case VT_UINTEGER: return 16;
        case VT_LONG: case VT_ULONG: return 32;
        case VT_INT64: case VT_UINT64: return 64;
    }
    return 0;
}

/* A math result: an Illegal function call for a value outside the
 * function's range, as SQR and LOG have (QBasic has no NaN). */
static int qb64_math_result(double value, Num *result) {
    if (!isfinite(value)) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        value = 0;
    }
    *result = num_d(value);
    return 1;
}

static int qb64_function_typed(int function_id, const Num *args, int argc, Num *result) {
    if (function_id >= QB64_FN_MIN && function_id <= QB64_FN_G2R) {
        int want = 1;
        switch (function_id) {
            case QB64_FN_MIN: case QB64_FN_MAX: case QB64_FN_HYPOT: case QB64_FN_ATAN2: want = 2; break;
            case QB64_FN_CLAMP: want = 3; break;
        }
        if (argc != want) {
            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            *result = num_i(0, NUM_INTEGER);
            return 1;
        }
        double x = num_to_double(args[0]);
        switch (function_id) {
            // The value chosen comes back as it was, keeping its type.
            case QB64_FN_MIN: *result = num_compare(args[1], args[0]) < 0 ? args[1] : args[0]; return 1;
            case QB64_FN_MAX: *result = num_compare(args[1], args[0]) > 0 ? args[1] : args[0]; return 1;
            case QB64_FN_CLAMP:
                if (num_compare(args[0], args[1]) < 0) *result = args[1];
                else if (num_compare(args[0], args[2]) > 0) *result = args[2];
                else *result = args[0];
                return 1;
            case QB64_FN_CEIL:
                if (num_is_int(args[0])) { *result = args[0]; return 1; }
                *result = num_f(ceil(args[0].d), args[0].kind);
                return 1;
            case QB64_FN_HYPOT: return qb64_math_result(hypot(x, num_to_double(args[1])), result);
            case QB64_FN_ATAN2: {
                double y = x, xx = num_to_double(args[1]);
                // _ATAN2(0, 0) is 0 rather than an error, as in QB64.
                return qb64_math_result(y == 0 && xx == 0 ? 0 : atan2(y, xx), result);
            }
            case QB64_FN_ACOS: return qb64_math_result(fabs(x) > 1 ? NAN : acos(x), result);
            case QB64_FN_ASIN: return qb64_math_result(fabs(x) > 1 ? NAN : asin(x), result);
            case QB64_FN_ACOSH: return qb64_math_result(x < 1 ? NAN : acosh(x), result);
            case QB64_FN_ASINH: return qb64_math_result(asinh(x), result);
            case QB64_FN_ATANH: return qb64_math_result(fabs(x) >= 1 ? NAN : atanh(x), result);
            case QB64_FN_COSH: return qb64_math_result(cosh(x), result);
            case QB64_FN_SINH: return qb64_math_result(sinh(x), result);
            case QB64_FN_TANH: return qb64_math_result(tanh(x), result);
            case QB64_FN_SEC: return qb64_math_result(cos(x) == 0 ? NAN : 1 / cos(x), result);
            case QB64_FN_CSC: return qb64_math_result(sin(x) == 0 ? NAN : 1 / sin(x), result);
            case QB64_FN_COT: return qb64_math_result(tan(x) == 0 ? NAN : 1 / tan(x), result);
            // The inverses of 1/cos, 1/sin and 1/tan; |x| >= 1 for the first two.
            case QB64_FN_ARCSEC: return qb64_math_result(fabs(x) < 1 ? NAN : acos(1 / x), result);
            case QB64_FN_ARCCSC: return qb64_math_result(fabs(x) < 1 ? NAN : asin(1 / x), result);
            case QB64_FN_ARCCOT: return qb64_math_result(M_PI / 2 - atan(x), result);
            case QB64_FN_SECH: return qb64_math_result(1 / cosh(x), result);
            case QB64_FN_CSCH: return qb64_math_result(x == 0 ? NAN : 1 / sinh(x), result);
            case QB64_FN_COTH: return qb64_math_result(x == 0 ? NAN : 1 / tanh(x), result);
            // Degrees, radians and gradians (400 to a circle).
            case QB64_FN_D2R: return qb64_math_result(x * M_PI / 180, result);
            case QB64_FN_R2D: return qb64_math_result(x * 180 / M_PI, result);
            case QB64_FN_D2G: return qb64_math_result(x * 400 / 360, result);
            case QB64_FN_G2D: return qb64_math_result(x * 360 / 400, result);
            case QB64_FN_R2G: return qb64_math_result(x * 200 / M_PI, result);
            case QB64_FN_G2R: return qb64_math_result(x * M_PI / 200, result);
        }
    }
    if (function_id >= QB64_FN_SHL && function_id <= QB64_FN_READBIT) {
        // _SHL(value, n) and the rest: whole numbers, and a new value back.
        int64_t value, n;
        if (argc != 2) {
            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            *result = num_i(0, NUM_INTEGER);
            return 1;
        }
        int kind = num_whole_operand(args[0], &value);
        if (!kind || !num_whole_operand(args[1], &n)) { *result = num_i(0, NUM_INTEGER); return 1; }
        int width = qb64_bit_width(args[0]);
        uint64_t mask = width == 64 ? UINT64_MAX : ((uint64_t)1 << width) - 1;
        uint64_t bits = (uint64_t)value;
        if (n < 0 || ((function_id >= QB64_FN_SETBIT) && n >= 64)) {
            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            *result = num_i(0, NUM_INTEGER);
            return 1;
        }
        switch (function_id) {
            case QB64_FN_SHL:
                // The result is not cut to the argument's width; assigning
                // it to a variable does that, as in QB64.
                *result = num_i(n >= 64 ? 0 : (int64_t)(bits << n), NUM_INT64);
                return 1;
            case QB64_FN_SHR:
                // Signed values keep their sign bit (an arithmetic shift).
                if (args[0].kind == NUM_UINT64) *result = num_i(n >= 64 ? 0 : (int64_t)(bits >> n), NUM_UINT64);
                else *result = num_i(n >= 64 ? (value < 0 ? -1 : 0) : value >> n, kind);
                return 1;
            case QB64_FN_ROL:
            case QB64_FN_ROR: {
                uint64_t v = bits & mask;
                int r = (int)(n % width);
                if (function_id == QB64_FN_ROR) r = (width - r) % width;
                if (r) v = ((v << r) | (v >> (width - r))) & mask;
                // A signed type's result with its top bit set is negative.
                // The declared width says whether the variable is signed;
                // an untyped value counts as signed.
                if (width < 64 && (v >> (width - 1)) & 1 && !qb64_bit_width_unsigned) v |= ~mask;
                *result = num_i((int64_t)v, kind == NUM_INTEGER && width > 16 ? NUM_LONG : kind);
                return 1;
            }
            case QB64_FN_SETBIT: *result = num_i((int64_t)(bits | ((uint64_t)1 << n)), kind); return 1;
            case QB64_FN_RESETBIT: *result = num_i((int64_t)(bits & ~((uint64_t)1 << n)), kind); return 1;
            case QB64_FN_TOGGLEBIT: *result = num_i((int64_t)(bits ^ ((uint64_t)1 << n)), kind); return 1;
            case QB64_FN_READBIT: *result = num_i(n < 64 && (bits >> n) & 1 ? -1 : 0, NUM_INTEGER); return 1;
        }
    }
    switch (function_id) {
        case QB64_FN_ROUND: {
            // _ROUND(x): the nearest whole number, halves to even, like CINT
            // but for any size; whole numbers come back exactly.
            if (argc != 1) break;
            int64_t whole;
            int kind = num_whole_operand(args[0], &whole);
            if (!kind) { *result = num_i(0, NUM_LONG); return 1; } // too big: Overflow
            if (kind == NUM_INTEGER) kind = NUM_LONG;
            *result = num_i(whole, kind);
            return 1;
        }
    }
    return 0;
}

/* Runs a QB64 function on its arguments as the parsers read them. argc is 5
 * when there were more than 4. */
static Num qb64_function_num(int function_id, const Num *args, int argc) {
    Num typed;
    if (qb64_function_typed(function_id, args, argc, &typed)) return typed;
    double dargs[4];
    for (int k = 0; k < argc && k < 4; k++) dargs[k] = num_to_double(args[k]);
    double value = qb64_function_value(function_id, dargs, argc);
    // Colors are _UNSIGNED LONG values in QB64, so they can be stored in a LONG.
    if (function_id >= QB64_FN_RGB32 && function_id <= QB64_FN_RGBA) {
        return num_i((int64_t)value, NUM_INT64);
    }
    return num_d(value);
}

/* LBOUND(array[, dimension]) and UBOUND(array[, dimension]); an array that
 * is not dimensioned, or a dimension it does not have, is "Subscript out of
 * range". */
static Num array_bound_tok(TokenStream *ts, TokenType which) {
    if (ts->tokens[ts->pos].type != TOKEN_LPAREN || ts->tokens[ts->pos + 1].type != TOKEN_IDENTIFIER) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return num_i(0, NUM_INTEGER);
    }
    ts->pos++;
    Token *name = &ts->tokens[ts->pos++];
    Variable *array = NULL;
    UserTypeInstance record;
    int record_index = user_type_count > 0 ? find_existing_variable_raw_name(name->text) : -1;
    if (record_index >= 0 && get_user_type_instance_for_root(record_index, &record)) {
        array = get_variable_ptr(record_index);
    } else {
        int index = resolve_token_variable(name);
        array = index >= 0 ? get_variable_ptr(index) : NULL;
    }
    int dimension = 1;
    if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
        ts->pos++;
        dimension = array_subscript(evaluate_num_tok(ts));
    }
    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
    if (!array || dimension < 1 || dimension > array->num_dims) {
        report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
        return num_i(0, NUM_INTEGER);
    }
    int bound = which == TOKEN_LBOUND ? array->lower_bounds[dimension - 1] : array->dims[dimension - 1];
    return num_i(bound, bound >= -32768 && bound <= 32767 ? NUM_INTEGER : NUM_LONG);
}

static Num primary_tok(TokenStream *ts) {
    // A missing operand (PRINT 1 +) must not step past the statement's last
    // token: the caller reads the position back as a text pointer.
    if (ts->tokens[ts->pos].type == TOKEN_EOF || ts->tokens[ts->pos].type == TOKEN_COLON) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return num_i(0, NUM_INTEGER);
    }
    Token *token = &ts->tokens[ts->pos++];
    if (token->type == TOKEN_NUMBER) {
        if (token->is_double) last_expression_is_double = 1;
        return num_from_token(token);
    }
    if (token->type == TOKEN_ARGC) return num_i(internal_argc, NUM_INTEGER);
    if (token->type == TOKEN_IDENTIFIER) {
        char first = (char)toupper((unsigned char)token->text[0]);
        if (first == 'E') {
            if (strcasecmp(token->text, "ERR") == 0) return num_i(last_runtime_error_code, NUM_INTEGER);
            if (strcasecmp(token->text, "ERL") == 0) return num_i(last_runtime_error_line, NUM_LONG);
            // No device drivers are simulated, so there is never a device error.
            if (strcasecmp(token->text, "ERDEV") == 0) return num_i(0, NUM_INTEGER);
        }

        if (first == '_') {
            int string_id = qb64_string_function_id(token->text);
            if (string_id && !qb64_string_function_is_string(string_id)) {
                Num value = num_i(0, NUM_INTEGER);
                BasicString unused = {0};
                call_qb64_string_function_tok(ts, string_id, &value, &unused);
                basic_string_release(&unused);
                return value;
            }
            int file_id = qb64_file_function_id(token->text);
            if (file_id && !qb64_file_function_is_string(file_id)) {
                double value = 0;
                BasicString unused = {0};
                call_qb64_file_function_tok(ts, file_id, &value, &unused);
                basic_string_release(&unused);
                if (file_id == QB64_SYS_UPTIME) return num_d(value);
                return num_i((int)value, file_id >= QB64_SYS_OS ? NUM_LONG : NUM_INTEGER);
            }
        }
        if (first == '_' && strcasecmp(token->text, "_SNDOPEN") == 0) {
            // _SNDOPEN(file$[, capabilities$]) -> handle, or 0 if the file
            // cannot be loaded. The capabilities (STREAM, ...) are accepted
            // and ignored: every sound is loaded whole.
            if (ts->tokens[ts->pos].type != TOKEN_LPAREN) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return num_i(0, NUM_LONG);
            }
            ts->pos++;
            BasicString path = {0};
            parse_string_expression_tok_heap(ts, &path);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
                ts->pos++;
                BasicString capabilities = {0};
                parse_string_expression_tok_heap(ts, &capabilities);
                basic_string_release(&capabilities);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            int handle = audio_sound_open(path.data ? (const char *)path.data : "");
            basic_string_release(&path);
            return num_i(handle, NUM_LONG);
        }

        if (first == '_') {
            int function_id = qb64_function_id(token->text);
            if (function_id) {
                Num args[4];
                int argc = 0;
                int width = qb64_argument_bit_width(ts->tokens, ts->pos, function_id);
                if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
                    ts->pos++;
                    if (ts->tokens[ts->pos].type != TOKEN_RPAREN) {
                        while (1) {
                            Num value = evaluate_num_tok(ts);
                            if (argc < 4) args[argc] = value;
                            argc++;
                            if (ts->tokens[ts->pos].type != TOKEN_COMMA) break;
                            ts->pos++;
                        }
                    }
                    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                }
                qb64_bit_width_hint = width;
                Num value = qb64_function_num(function_id, args, argc > 4 ? 5 : argc);
                qb64_bit_width_hint = 0;
                qb64_bit_width_unsigned = 0;
                return value;
            }
        }

        if (first == 'P' && strcasecmp(token->text, "POINT") == 0) {
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
                ts->pos++;
                double x = evaluate_expression_tok(ts);
                if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
                    ts->pos++;
                    double y = evaluate_expression_tok(ts);
                    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                    if (!graphics_is_active()) init_graphics();
                    return num_i((int64_t)graphics_point(x, y), NUM_INT64); // a color: an _UNSIGNED LONG
                } else {
                    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                    return num_d(graphics_cursor_query(round_to_int(x)));
                }
            }
            return num_i(0, NUM_INTEGER);
        }

        int const_idx = find_named_constant_tok(token);
        if (const_idx >= 0) {
            if (named_constants[const_idx].is_wide) last_expression_is_double = 1;
            return named_constants[const_idx].value;
        }

        if (first == 'F' && strcasecmp(token->text, "FREEFILE") == 0) {
            // FREEFILE: the lowest file number not in use.
            for (int fnum = 1; fnum < 16; fnum++) {
                if (!file_handles[fnum]) return num_i(fnum, NUM_INTEGER);
            }
            report_runtime_error(ERR_TOO_MANY_FILES);
            return num_i(0, NUM_INTEGER);
        }
        if (first == 'C' && strcasecmp(token->text, "CSRLIN") == 0) {
            int row, col;
            get_text_cursor(&row, &col);
            if (!graphics_is_active()) row = print_row + 1;
            return num_i(row, NUM_INTEGER);
        }
        if (first == 'P' && strcasecmp(token->text, "POS") == 0) {
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) ts->pos++;
            (void)evaluate_num_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            int col = print_col + 1;
            if (graphics_is_active()) {
                int row;
                get_text_cursor(&row, &col);
            }
            return num_i(col, NUM_INTEGER);
        }

        if (first == 'F' && (token->text[1] == 'N' || token->text[1] == 'n')) {
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
                ts->pos++; // (
                for (int i = 0; i < user_function_count; i++) {
                    if (strcasecmp(user_functions[i].name, token->text) == 0) {
                        Num arg_val = evaluate_num_tok(ts);
                        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
                        
                        return evaluate_user_function(&user_functions[i], arg_val);
                    }
                }
            }
        }

        ProcedureDef *pfunc = proc_count > 0 ? find_procedure_tok(token) : NULL;
        if (pfunc && pfunc->is_function) {
            return evaluate_function_call_numeric(pfunc, ts);
        }

        int member_index = -1;
        int member_array_index = -1;
        int is_member = user_type_count > 0
            ? resolve_user_type_reference_tok(ts, token, &member_index,
                                              &member_array_index)
            : 0;
        if (is_member != 0) {
            if (is_member < 0) return num_i(0, NUM_INTEGER);
            Variable *member = get_variable_ptr(member_index);
            if (!member || is_string_var(member->name)) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return num_i(0, NUM_INTEGER);
            }
            if (var_is_wide(member)) last_expression_is_double = 1;
            return load_num(member, member_array_index >= 0 ? member_array_index : -1);
        }

        int idx = resolve_token_variable(token);
        Variable *v = (idx != -1) ? get_variable_ptr(idx) : NULL;
        
        if (v && var_is_wide(v)) last_expression_is_double = 1;

        int array_idx = parse_array_index_tok(ts, idx);
        if (!v) return num_f(0, NUM_SINGLE);
        return load_num(v, array_idx >= 0 ? array_idx : -1);
    }
    if (token->type == TOKEN_LPAREN) {
        Num val = evaluate_num_tok(ts);
        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        return val;
    }

    if (token->type == TOKEN_LBOUND || token->type == TOKEN_UBOUND) return array_bound_tok(ts, token->type);

    Num argument;
    switch (token->type) {
        case TOKEN_CINT: case TOKEN_CLNG: case TOKEN_CSNG: case TOKEN_CDBL:
        case TOKEN_ABS: case TOKEN_SGN: case TOKEN_INT: case TOKEN_FIX:
            if (function_argument_tok(ts, &argument)) return numeric_function_num(token->type, argument);
            break;
        default:
            break;
    }
    double result = builtin_function_tok(ts, token);
    switch (token->type) {
        // Functions with whole-number results return integers, so LOF of a
        // large file or CVL prints every digit.
        case TOKEN_CVI:
            return num_i((int64_t)result, NUM_INTEGER);
        case TOKEN_CVL: case TOKEN_LEN: case TOKEN_ASC: case TOKEN_INSTR: case TOKEN_LOF:
        case TOKEN_LOC: case TOKEN_EOF_FUNC: case TOKEN_PEEK: case TOKEN_VARPTR:
        case TOKEN_PRINTWIDTH:
            if (result == trunc(result) && fabs(result) < 9.2e18) {
                int64_t whole = (int64_t)result;
                return num_i(whole, whole >= INT32_MIN && whole <= INT32_MAX ? NUM_LONG : NUM_INT64);
            }
            break;
        default:
            break;
    }
    return num_d(result);
}

static __attribute__((noinline)) double builtin_function_tok(TokenStream *ts, Token *token) {
    if (token->type == TOKEN_LOADFONT) {
        /* _LOADFONT(filename$, size%[, style$]) -> handle, or -1 if the font
         * cannot be loaded. style$ lists BOLD, ITALIC and UNDERLINE; MONOSPACE,
         * UNICODE and DONTBLEND are accepted, and MEMORY (font data in
         * filename$) is not supported. */
        if (ts->tokens[ts->pos].type != TOKEN_LPAREN) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }
        ts->pos++;
        char buf[BASIC_STRING_MAX] = "";
        parse_string_expression_tok(ts, buf, sizeof(buf));
        if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
        int size = evaluate_int_tok(ts);
        char style_text[BASIC_STRING_MAX] = "";
        if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
            ts->pos++;
            parse_string_expression_tok(ts, style_text, sizeof(style_text));
        }
        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        int style = 0, from_memory = 0;
        for (char *word = strtok(style_text, " ,"); word; word = strtok(NULL, " ,")) {
            if (strcasecmp(word, "BOLD") == 0) style |= FONT_STYLE_BOLD;
            else if (strcasecmp(word, "ITALIC") == 0) style |= FONT_STYLE_ITALIC;
            else if (strcasecmp(word, "UNDERLINE") == 0) style |= FONT_STYLE_UNDERLINE;
            else if (strcasecmp(word, "MEMORY") == 0) from_memory = 1;
            else if (strcasecmp(word, "MONOSPACE") != 0 && strcasecmp(word, "UNICODE") != 0 &&
                     strcasecmp(word, "DONTBLEND") != 0) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                return 0;
            }
        }
        if (from_memory) return -1;
        if (!graphics_is_active()) init_graphics();
        return (double)graphics_loadfont(buf, size, style);
    }
    if (token->type == TOKEN_FONT) return graphics_is_active() ? graphics_current_font() : 16;
    if (token->type == TOKEN_DEST) return graphics_get_dest();
    // _AUTODISPLAY: -1 while drawing is shown automatically, 0 after _DISPLAY.
    if (token->type == TOKEN_AUTODISPLAY) return graphics_get_autodisplay() ? -1 : 0;
    if (token->type == TOKEN_SOURCE) return graphics_get_source();
    if (token->type == TOKEN_NEWIMAGE) {
        /* _NEWIMAGE(width, height[, mode]) -> handle */
        double args[3] = {0, 0, 256};
        int argc = 0;
        if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
            ts->pos++;
            while (argc < 3) {
                args[argc++] = evaluate_expression_tok(ts);
                if (ts->tokens[ts->pos].type != TOKEN_COMMA) break;
                ts->pos++;
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        }
        if (argc < 2) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 0;
        }
        if (!graphics_is_active()) init_graphics();
        int handle = graphics_newimage(round_to_int(args[0]), round_to_int(args[1]), round_to_int(args[2]));
        if (handle == -1) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return handle;
    }
    if (token->type == TOKEN_LOADIMAGE) {
        /* _LOADIMAGE(filename$, mode&) -> handle */
        if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
            ts->pos++;
            char buf[BASIC_STRING_MAX] = "";
            parse_string_expression_tok(ts, buf, sizeof(buf));
            int mode = 32;
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
                ts->pos++;
                mode = evaluate_int_tok(ts);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (!graphics_is_active()) init_graphics();
            return (double)graphics_loadimage(buf, mode);
        }
         return 0;
     }
     if ((token->type >= TOKEN_ABS && token->type <= TOKEN_SGN) || token->type == TOKEN_EOF_FUNC || token->type == TOKEN_TIMER || token->type == TOKEN_KEY || token->type == TOKEN_STRIG || token->type == TOKEN_PEN || token->type == TOKEN_STICK || token->type == TOKEN_ASC || token->type == TOKEN_LEN || token->type == TOKEN_INSTR || token->type == TOKEN_VAL || token->type == TOKEN_PEEK || token->type == TOKEN_VARPTR || token->type == TOKEN_LOF || token->type == TOKEN_LOC || token->type == TOKEN_CVI || token->type == TOKEN_CVL || token->type == TOKEN_CVS || token->type == TOKEN_CVD || token->type == TOKEN_PRINTWIDTH) {
        TokenType ft = token->type;
        int has_arg = 0;
        double arg = 0;

        if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
            ts->pos++;
            has_arg = 1;
            if (ft == TOKEN_VARPTR) {
                Token *var_token = &ts->tokens[ts->pos++];
                Token var_tok = *var_token;
                int var_idx = -1;
                int array_idx = -1;
                if (var_tok.type == TOKEN_IDENTIFIER) {
                    var_idx = resolve_token_variable(var_token);
                    array_idx = parse_array_index_tok(ts, var_idx);
                }
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                if (var_idx < 0) return 0;
                if (array_idx >= 0) return 61440.0 + var_idx * 256.0 + array_idx;
                return 61440.0 + var_idx * 256.0;
            }
            if (ft == TOKEN_CVI || ft == TOKEN_CVL || ft == TOKEN_CVS || ft == TOKEN_CVD) {
                // The argument is binary data, so it is kept with its length:
                // MKS$(25) starts with two zero bytes.
                char buf[8] = {0};
                BasicString argument = {0};
                parse_string_expression_tok_heap(ts, &argument);
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                int len = argument.length < sizeof(buf) ? (int)argument.length : (int)sizeof(buf);
                if (len > 0) memcpy(buf, argument.data, (size_t)len);
                basic_string_release(&argument);
                if (ft == TOKEN_CVI) {
                    int16_t val = 0;
                    int cpy = (len > 2) ? 2 : len;
                    if (cpy > 0) memcpy(&val, buf, (size_t)cpy);
                    return (double)val;
                } else if (ft == TOKEN_CVL) {
                    int32_t val = 0;
                    int cpy = (len > 4) ? 4 : len;
                    if (cpy > 0) memcpy(&val, buf, (size_t)cpy);
                    return (double)val;
                } else if (ft == TOKEN_CVS) {
                    float val = 0.0f;
                    int cpy = (len > 4) ? 4 : len;
                    if (cpy > 0) memcpy(&val, buf, (size_t)cpy);
                    return (double)val;
                } else {
                    double val = 0.0;
                    int cpy = (len > 8) ? 8 : len;
                    if (cpy > 0) memcpy(&val, buf, (size_t)cpy);
                    return val;
                }
            }
            if (ft == TOKEN_ASC || ft == TOKEN_LEN || ft == TOKEN_VAL || ft == TOKEN_PRINTWIDTH) {
                // LEN(var$) reads the length without copying the string; any
                // other argument (a FUNCTION call, an expression) is parsed below.
                if (ft == TOKEN_LEN && ts->tokens[ts->pos].type == TOKEN_IDENTIFIER &&
                    is_string_var(ts->tokens[ts->pos].text) && !is_function_name(ts->tokens[ts->pos].text)) {
                    int length_start = ts->pos;
                    Token *length_token = &ts->tokens[ts->pos++];
                    int length_idx = resolve_token_variable(length_token);
                    int length_array_idx = parse_array_index_tok(ts, length_idx);
                    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) {
                        ts->pos++;
                        Variable *length_var = get_variable_ptr(length_idx);
                        BasicString *length_value = length_array_idx >= 0 && length_var->s_array
                            ? length_var->s_array[length_array_idx] : length_var->s_value;
                        return length_value ? (double)length_value->length : 0.0;
                    }
                    ts->pos = length_start;
                }
                // Kept with its length, so zero bytes inside count (LEN(MKI$(5)) is 2).
                BasicString argument = {0};
                parse_string_expression_tok_heap(ts, &argument);
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                const char *text = argument.data ? (const char *)argument.data : "";
                double result = 0;
                if (ft == TOKEN_ASC) result = argument.length ? (unsigned char)text[0] : 0;
                else if (ft == TOKEN_LEN) result = (double)argument.length;
                else if (ft == TOKEN_VAL) result = atof(text);
                else result = graphics_printwidth(text);
                basic_string_release(&argument);
                return result;
            }
            if (ft == TOKEN_INSTR) {
                BasicString s1 = {0}, s2 = {0};
                int start = 1;
                Token next = ts->tokens[ts->pos];
                if (next.type != TOKEN_STRING && next.type != TOKEN_IDENTIFIER) {
                    start = evaluate_int_tok(ts);
                    ts->pos++; // comma
                }
                parse_string_expression_tok_heap(ts, &s1);
                ts->pos++; // comma
                parse_string_expression_tok_heap(ts, &s2);
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                // A byte-by-byte search, so strings with zero bytes match too.
                double found = 0;
                if (start >= 1) {
                    for (size_t at = (size_t)start - 1; at + s2.length <= s1.length; at++) {
                        if (s2.length == 0 || memcmp(s1.data + at, s2.data, s2.length) == 0) {
                            found = (double)(at + 1);
                            break;
                        }
                    }
                }
                basic_string_release(&s1);
                basic_string_release(&s2);
                return found;
            }
            arg = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        }

        switch (ft) {
            case TOKEN_EOF_FUNC: {
                int fnum = round_to_int(arg);
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                int c = fgetc(file_handles[fnum]);
                if (c == EOF) return -1; // true, so NOT EOF(n) is false
                ungetc(c, file_handles[fnum]);
                return 0;
            }
            case TOKEN_LOF: {
                int fnum = round_to_int(arg);
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                long cur = ftell(file_handles[fnum]);
                fseek(file_handles[fnum], 0, SEEK_END);
                long size = ftell(file_handles[fnum]);
                fseek(file_handles[fnum], cur, SEEK_SET);
                return (double)size;
            }
            case TOKEN_LOC: {
                int fnum = round_to_int(arg);
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                // RANDOM: last record used; BINARY: last byte used.
                if (file_mode[fnum] == FILE_MODE_RANDOM || file_mode[fnum] == FILE_MODE_BINARY) {
                    return (double)file_last_record[fnum];
                }
                return (double)ftell(file_handles[fnum]);
            }
            case TOKEN_KEY: return (double)get_graphics_key();
            case TOKEN_PEN:
                if (arg < 0 || arg > 9) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    return 0;
                }
                return graphics_is_active() ? graphics_pen(round_to_int(arg)) : 0;
            case TOKEN_STICK:
                if (arg < 0 || arg > 3) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    return 0;
                }
                return graphics_is_active() ? graphics_stick(round_to_int(arg)) : 0;
            case TOKEN_STRIG: {
                int s = round_to_int(arg);
                if (s >= 0 && s < 8) {
                    if (s % 2 == 0) {
                        double val = strig_button_pressed_since[s] ? -1.0 : 0.0;
                        strig_button_pressed_since[s] = 0;
                        return val;
                    } else {
                        return strig_button_currently_pressed[s - 1] ? -1.0 : 0.0;
                    }
                }
                return 0;
            }
            case TOKEN_PEEK: {
                int addr = round_to_int(arg);
                if (addr < 0 || addr >= 65536) return 0;
                return basika_memory[addr];
            }
            default: return math_function_value(ft, arg, has_arg);
        }
    }

    return 0;
}

/* Operator precedence follows QBasic, from tightest to loosest: ^ (left to
 * right), negation, * and /, \, MOD, + and -, relations, NOT, AND, OR, XOR,
 * EQV, IMP. */
static Num unary_tok(TokenStream *ts);

static Num power_tok(TokenStream *ts) {
    Num val = primary_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_POWER) {
        ts->pos++;
        // The exponent may carry its own sign: 2 ^ -1.
        TokenType next = ts->tokens[ts->pos].type;
        Num exponent = (next == TOKEN_MINUS || next == TOKEN_PLUS) ? unary_tok(ts) : primary_tok(ts);
        val = num_power(val, exponent);
    }
    return val;
}

static Num unary_tok(TokenStream *ts) {
    if (ts->tokens[ts->pos].type == TOKEN_PLUS) {
        ts->pos++;
        return unary_tok(ts);
    }
    if (ts->tokens[ts->pos].type == TOKEN_MINUS) {
        ts->pos++;
        return num_negate(unary_tok(ts));
    }
    return power_tok(ts);
}

static Num multiply_tok(TokenStream *ts) {
    Num val = unary_tok(ts);
    while (1) {
        TokenType type = ts->tokens[ts->pos].type;
        if (type == TOKEN_STAR) { ts->pos++; val = num_multiply(val, unary_tok(ts)); }
        else if (type == TOKEN_SLASH) { ts->pos++; val = num_divide(val, unary_tok(ts)); }
        else break;
    }
    return val;
}

static Num integer_divide_tok(TokenStream *ts) {
    Num val = multiply_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_IDIV) {
        ts->pos++;
        val = num_integer_divide(val, multiply_tok(ts), 0);
    }
    return val;
}

static Num modulo_tok(TokenStream *ts) {
    Num val = integer_divide_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_MOD) {
        ts->pos++;
        val = num_integer_divide(val, integer_divide_tok(ts), 1);
    }
    return val;
}

static Num arithmetic_expression_tok(TokenStream *ts) {
    Num val = modulo_tok(ts);
    while (1) {
        TokenType type = ts->tokens[ts->pos].type;
        if (type == TOKEN_PLUS) { ts->pos++; val = num_add(val, modulo_tok(ts)); }
        else if (type == TOKEN_MINUS) { ts->pos++; val = num_subtract(val, modulo_tok(ts)); }
        else break;
    }
    return val;
}

/* is_string_token only sees the root of a record element reference such as
 * items(2).label, so probe the full reference (on a copy of the stream) to
 * learn whether its final field is a string. */
static int is_string_member_reference_tok(const TokenStream *ts) {
    if (user_type_count == 0) return 0;
    const Token *base = &ts->tokens[ts->pos];
    if (base->type != TOKEN_IDENTIFIER || ts->tokens[ts->pos + 1].type != TOKEN_LPAREN) return 0;
    TokenStream probe = *ts;
    probe.pos++;
    int member_index = -1, member_array_index = -1;
    if (resolve_user_type_reference_tok(&probe, base, &member_index, &member_array_index) <= 0) return 0;
    Variable *member = get_variable_ptr(member_index);
    return member && is_string_var(member->name);
}

static int is_string_member_reference_text(const char *input) {
    if (user_type_count == 0) return 0;
    Token base = get_next_token(&input);
    if (base.type != TOKEN_IDENTIFIER || *skip_whitespace_fast(input) != '(') return 0;
    int member_index = -1, member_array_index = -1;
    if (resolve_user_type_reference_text(&input, &base, &member_index, &member_array_index) <= 0) return 0;
    Variable *member = get_variable_ptr(member_index);
    return member && is_string_var(member->name);
}

/* A string comparison (a$ = b$, a$ <> b$, ...), evaluated at the relational
 * level so it combines with AND/OR/NOT like a numeric comparison. */
static Num string_comparison_tok(TokenStream *ts) {
    BasicString left = {0};
    BasicString right = {0};
    if (!parse_string_expression_tok_heap(ts, &left)) {
        basic_string_release(&left);
        return num_i(0, NUM_INTEGER);
    }

    TokenType op = ts->tokens[ts->pos].type;
    if (op != TOKEN_EQUALS && op != TOKEN_LESS && op != TOKEN_GREATER) {
        basic_string_release(&left);
        return num_i(0, NUM_INTEGER);
    }
    ts->pos++;

    TokenType modifier = ts->tokens[ts->pos].type;
    if ((op == TOKEN_LESS && (modifier == TOKEN_GREATER || modifier == TOKEN_EQUALS)) ||
        (op == TOKEN_GREATER && modifier == TOKEN_EQUALS)) {
        ts->pos++;
    } else {
        modifier = TOKEN_EOF;
    }

    if (!parse_string_expression_tok_heap(ts, &right)) {
        basic_string_release(&right);
        basic_string_release(&left);
        return num_i(0, NUM_INTEGER);
    }

    int comparison = strcmp((const char *)left.data, (const char *)right.data);
    basic_string_release(&right);
    basic_string_release(&left);
    if (op == TOKEN_EQUALS) return num_truth(comparison == 0);
    if (op == TOKEN_LESS) {
        if (modifier == TOKEN_GREATER) return num_truth(comparison != 0);
        if (modifier == TOKEN_EQUALS) return num_truth(comparison <= 0);
        return num_truth(comparison < 0);
    }
    return num_truth(modifier == TOKEN_EQUALS ? comparison >= 0 : comparison > 0);
}

static Num relational_expression_tok(TokenStream *ts) {
    if (ts->tokens[ts->pos].type != TOKEN_EOF &&
        (is_string_token(&ts->tokens[ts->pos]) || is_string_member_reference_tok(ts))) {
        return string_comparison_tok(ts);
    }
    Num val = arithmetic_expression_tok(ts);
    while (1) {
        TokenType type = ts->tokens[ts->pos].type;
        if (type == TOKEN_EQUALS) {
            ts->pos++;
            val = num_truth(num_compare(val, arithmetic_expression_tok(ts)) == 0);
        }
        else if (type == TOKEN_LESS) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_GREATER) {
                ts->pos++;
                val = num_truth(num_compare(val, arithmetic_expression_tok(ts)) != 0);
            } else if (ts->tokens[ts->pos].type == TOKEN_EQUALS) {
                ts->pos++;
                val = num_truth(num_compare(val, arithmetic_expression_tok(ts)) <= 0);
            } else {
                val = num_truth(num_compare(val, arithmetic_expression_tok(ts)) < 0);
            }
        }
        else if (type == TOKEN_GREATER) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_EQUALS) {
                ts->pos++;
                val = num_truth(num_compare(val, arithmetic_expression_tok(ts)) >= 0);
            } else {
                val = num_truth(num_compare(val, arithmetic_expression_tok(ts)) > 0);
            }
        }
        else break;
    }
    return val;
}

static Num logical_not_tok(TokenStream *ts) {
    if (ts->tokens[ts->pos].type == TOKEN_NOT) {
        ts->pos++;
        Num operand = logical_not_tok(ts);
        return num_logical('~', operand, operand);
    }
    return relational_expression_tok(ts);
}

static Num bitwise_and_tok(TokenStream *ts) {
    Num val = logical_not_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_AND) {
        ts->pos++;
        val = num_logical('&', val, logical_not_tok(ts));
    }
    return val;
}

static Num bitwise_or_tok(TokenStream *ts) {
    Num val = bitwise_and_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_OR) {
        ts->pos++;
        val = num_logical('|', val, bitwise_and_tok(ts));
    }
    return val;
}

static Num exclusive_or_tok(TokenStream *ts) {
    Num val = bitwise_or_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_XOR) {
        ts->pos++;
        val = num_logical('^', val, bitwise_or_tok(ts));
    }
    return val;
}

/* EQV and then IMP bind loosest of all, as in QBasic. */
static Num equivalence_tok(TokenStream *ts) {
    Num val = exclusive_or_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_EQV) {
        ts->pos++;
        val = num_logical('=', val, exclusive_or_tok(ts));
    }
    return val;
}

/* The variable a name in a cached expression reads, or NULL with *const_idx
 * set to its CONST index, or to -1 if the name is not a numeric variable
 * after all, which leaves the expression to the full parser. */
static inline __attribute__((always_inline)) Variable *resolve_cached_name(
        CompiledExpression *expression, Token *token, int *const_idx) {
    *const_idx = find_named_constant_tok(token);
    if (*const_idx >= 0) return NULL;
    // Expressions are compiled before DIM and SUB/FUNCTION declarations
    // take effect, so names that turned out not to be numeric
    // variables are left to the full parser.
    if (proc_count > 0 && find_procedure_tok(token)) {
        expression->rejected_proc_generation = proc_generation + 1;
        return NULL;
    }
    if (user_type_count > 0 && strchr(token->text, '.')) return NULL;
    int variable_index = resolve_token_variable(token);
    Variable *variable = variable_index >= 0 ? get_variable_ptr(variable_index) : NULL;
    if (!variable || var_type(variable) == VT_STRING || variable->string_declared) return NULL;
    return variable;
}

/* parse_array_index_tok reads a lone variable subscript, a(i), itself when
 * there are no procedures. */
static inline int cached_subscript_is_direct(void) {
    return proc_count == 0 && user_function_count == 0;
}

/* The numeric variable of such a subscript, or NULL. */
static Variable *cached_subscript_variable(Token *token) {
    int variable_index = resolve_token_variable(token);
    Variable *variable = variable_index >= 0 ? get_variable_ptr(variable_index) : NULL;
    if (!variable || var_type(variable) == VT_STRING || variable->string_declared) return NULL;
    return variable;
}

/* Whether every name in an expression with calls resolves, checked before
 * any of it runs so that falling back to the parser never repeats a call
 * such as RND. */
static __attribute__((noinline)) int cached_names_resolve(TokenStream *ts, CompiledExpression *expression) {
    for (int i = 0; i < expression->instruction_count; i++) {
        const CompiledExpressionInstruction *instruction = &expression->instructions[i];
        unsigned char opcode = instruction->opcode;
        if (opcode != CEXPR_VARIABLE && opcode != CEXPR_SUBSCRIPT && opcode != CEXPR_ARRAY) continue;
        Token *token = &ts->tokens[instruction->token_index];
        if (opcode == CEXPR_SUBSCRIPT && cached_subscript_is_direct()) {
            if (!cached_subscript_variable(token)) return 0;
            continue;
        }
        int const_idx;
        if (resolve_cached_name(expression, token, &const_idx)) continue;
        if (const_idx < 0 || opcode == CEXPR_ARRAY) return 0;
    }
    return 1;
}

/* Subscripts, array elements and calls are kept out of
 * evaluate_cached_expression, so its loop over arithmetic stays in
 * registers. Each takes its operands from values[] and returns the new stack
 * size, or -1 to leave the expression to the parser. */
static __attribute__((noinline)) int cached_subscript(TokenStream *ts, CompiledExpression *expression,
                                                      const CompiledExpressionInstruction *instruction,
                                                      Num *values, int stack_size) {
    Token *token = &ts->tokens[instruction->token_index];
    Variable *variable;
    if (cached_subscript_is_direct()) {
        variable = cached_subscript_variable(token);
        if (!variable) return -1;
        // Only a # suffix makes this subscript double.
        if (name_last_char(variable->name) == '#') last_expression_is_double = 1;
    } else {
        int const_idx;
        variable = resolve_cached_name(expression, token, &const_idx);
        if (!variable) {
            if (const_idx < 0) return -1;
            if (named_constants[const_idx].is_wide) last_expression_is_double = 1;
            values[stack_size++] = named_constants[const_idx].value;
            return stack_size;
        }
        if (var_is_wide(variable)) last_expression_is_double = 1;
    }
    values[stack_size++] = load_num(variable, -1);
    return stack_size;
}

static __attribute__((noinline)) int cached_array_element(TokenStream *ts, CompiledExpression *expression,
                                                          const CompiledExpressionInstruction *instruction,
                                                          Num *values, int stack_size) {
    Token *token = &ts->tokens[instruction->token_index];
    int const_idx;
    Variable *array = resolve_cached_name(expression, token, &const_idx);
    if (!array) return -1;
    int variable_index = resolve_token_variable(token);
    int indices[3];
    stack_size -= instruction->argc;
    for (int k = 0; k < instruction->argc; k++) indices[k] = array_subscript(values[stack_size + k]);
    if (var_is_wide(array)) last_expression_is_double = 1;
    ensure_array_dimensioned(variable_index, instruction->argc);
    int array_idx = calc_linear_index(array, indices, instruction->argc);
    values[stack_size++] = load_num(array, array_idx >= 0 ? array_idx : -1);
    return stack_size;
}

static __attribute__((noinline)) int cached_call(TokenStream *ts, const CompiledExpressionInstruction *instruction,
                                                 Num *values, int stack_size) {
    TokenType type = ts->tokens[instruction->token_index].type;
    switch (instruction->opcode) {
        case CEXPR_FUNCTION:
            values[stack_size - 1] = numeric_function_num(type, values[stack_size - 1]);
            break;
        case CEXPR_FUNCTION0:
            values[stack_size++] = num_d(math_function_value(type, 0, 0));
            break;
        case CEXPR_QB64: {
            Num args[4];
            int argc = instruction->argc;
            stack_size -= argc;
            for (int k = 0; k < argc && k < 4; k++) args[k] = values[stack_size + k];
            if (instruction->bit_width) {
                qb64_bit_width_hint = instruction->bit_width < 0 ? -instruction->bit_width : instruction->bit_width;
                qb64_bit_width_unsigned = instruction->bit_width < 0;
            }
            values[stack_size++] = qb64_function_num(instruction->function_id, args, argc > 4 ? 5 : argc);
            qb64_bit_width_hint = 0;
            qb64_bit_width_unsigned = 0;
            break;
        }
        case CEXPR_POINT: {
            stack_size -= instruction->argc;
            double x = num_to_double(values[stack_size]);
            if (instruction->argc == 2) {
                double y = num_to_double(values[stack_size + 1]);
                if (!graphics_is_active()) init_graphics();
                values[stack_size++] = num_i((int64_t)graphics_point(x, y), NUM_INT64); // a color: an _UNSIGNED LONG
            } else {
                values[stack_size++] = num_d(graphics_cursor_query(round_to_int(x)));
            }
            break;
        }
    }
    return stack_size;
}

static int evaluate_cached_expression(TokenStream *ts, CompiledExpression *expression, Num *result) {
    if (expression->has_calls && !cached_names_resolve(ts, expression)) return 0;
    const unsigned int pass_generation = expression_name_generation;

    // The compiler checked the stack depth. Locals keep the loop from
    // reloading the program after every store.
    const CompiledExpressionInstruction *instructions = expression->instructions;
    const int instruction_count = expression->instruction_count;
    Num values[CACHED_EXPRESSION_STACK];
    int stack_size = 0;
#if defined(__GNUC__)
    /* Computed-goto dispatch: every opcode jumps straight to its handler,
     * so one instruction costs a single indirect branch instead of the
     * NUMBER/VARIABLE pre-tests plus a switch. The table lists the opcodes
     * in enum order. */
    static const void *const cexpr_dispatch[] = {
        &&L_NUMBER, &&L_VARIABLE, &&L_SUBSCRIPT, &&L_ARRAY,
        &&L_FUNCTION, &&L_FUNCTION0, &&L_QB64, &&L_POINT,
        &&L_NEGATE, &&L_POWER, &&L_ADD, &&L_SUBTRACT,
        &&L_MULTIPLY, &&L_DIVIDE, &&L_MOD, &&L_IDIV,
        &&L_EQUAL, &&L_NOT_EQUAL, &&L_LESS, &&L_LESS_EQUAL,
        &&L_GREATER, &&L_GREATER_EQUAL, &&L_NOT, &&L_AND,
        &&L_OR, &&L_XOR, &&L_EQV, &&L_IMP
    };
#define CEXPR_NEXT do { \
        if (++i >= instruction_count) goto cexpr_done; \
        instruction = &instructions[i]; \
        goto *cexpr_dispatch[instruction->opcode]; \
    } while (0)
    int i = 0;
    if (instruction_count <= 0) goto cexpr_done;
    const CompiledExpressionInstruction *instruction = &instructions[0];
    goto *cexpr_dispatch[instruction->opcode];

L_NUMBER:
    if (instruction->is_double) last_expression_is_double = 1;
    values[stack_size++] = instruction->number;
    CEXPR_NEXT;
L_VARIABLE: {
    int const_idx;
    Variable *variable;
    /* The instruction caches what the name resolved to, so a plain
     * variable read is a pointer load; the cache is only trusted at
     * module scope and while nothing has changed how names resolve. */
    if (instruction->resolve_state &&
        expression->name_cache_generation == expression_name_generation &&
        call_stack_depth == 0) {
        if (instruction->resolve_state == 1) {
            variable = instruction->resolved_var;
            const_idx = -1;
        } else if (instruction->resolve_state == 2) {
            variable = NULL;
            const_idx = instruction->argc;
        } else {
            return 0;
        }
    } else {
        variable = resolve_cached_name(expression, &ts->tokens[instruction->token_index], &const_idx);
        if (call_stack_depth == 0) {
            /* Inside a call the same name can mean a local of the
             * frame, so only the program's own scope is cached. The
             * expression's generation is stamped once the whole pass
             * is through, in case a call changed things midway. */
            CompiledExpressionInstruction *cache = (CompiledExpressionInstruction *)instruction;
            if (variable) {
                cache->resolved_var = variable;
                cache->resolve_state = 1;
            } else if (const_idx >= 0) {
                cache->argc = const_idx;
                cache->resolve_state = 2;
            } else {
                cache->resolve_state = 3;
            }
        }
    }
    if (!variable) {
        if (const_idx < 0) return 0;
        if (named_constants[const_idx].is_wide) last_expression_is_double = 1;
        values[stack_size++] = named_constants[const_idx].value;
        CEXPR_NEXT;
    }
    if (var_is_wide(variable)) last_expression_is_double = 1;
    values[stack_size++] = load_num(variable, -1);
    CEXPR_NEXT;
}
L_SUBSCRIPT:
    stack_size = cached_subscript(ts, expression, instruction, values, stack_size);
    if (stack_size < 0) return 0;
    CEXPR_NEXT;
L_ARRAY:
    stack_size = cached_array_element(ts, expression, instruction, values, stack_size);
    if (stack_size < 0) return 0;
    CEXPR_NEXT;
L_FUNCTION: L_FUNCTION0: L_QB64: L_POINT:
    stack_size = cached_call(ts, instruction, values, stack_size);
    CEXPR_NEXT;
L_NEGATE:
    values[stack_size - 1] = num_negate(values[stack_size - 1]);
    CEXPR_NEXT;
L_NOT:
    values[stack_size - 1] = num_logical('~', values[stack_size - 1], values[stack_size - 1]);
    CEXPR_NEXT;
// Binary operators replace the top two operands; each is its own label, so
// an instruction takes one dispatch.
#define CACHED_BINARY_GOTO(label, operation) \
    label: { \
        Num right = values[--stack_size]; \
        Num left = values[stack_size - 1]; \
        values[stack_size - 1] = operation; \
        CEXPR_NEXT; \
    }
CACHED_BINARY_GOTO(L_POWER, num_power(left, right))
CACHED_BINARY_GOTO(L_ADD, num_add(left, right))
CACHED_BINARY_GOTO(L_SUBTRACT, num_subtract(left, right))
CACHED_BINARY_GOTO(L_MULTIPLY, num_multiply(left, right))
CACHED_BINARY_GOTO(L_DIVIDE, num_divide(left, right))
CACHED_BINARY_GOTO(L_MOD, num_integer_divide(left, right, 1))
CACHED_BINARY_GOTO(L_IDIV, num_integer_divide(left, right, 0))
CACHED_BINARY_GOTO(L_EQUAL, num_truth(num_compare(left, right) == 0))
CACHED_BINARY_GOTO(L_NOT_EQUAL, num_truth(num_compare(left, right) != 0))
CACHED_BINARY_GOTO(L_LESS, num_truth(num_compare(left, right) < 0))
CACHED_BINARY_GOTO(L_LESS_EQUAL, num_truth(num_compare(left, right) <= 0))
CACHED_BINARY_GOTO(L_GREATER, num_truth(num_compare(left, right) > 0))
CACHED_BINARY_GOTO(L_GREATER_EQUAL, num_truth(num_compare(left, right) >= 0))
CACHED_BINARY_GOTO(L_AND, num_logical('&', left, right))
CACHED_BINARY_GOTO(L_OR, num_logical('|', left, right))
CACHED_BINARY_GOTO(L_XOR, num_logical('^', left, right))
CACHED_BINARY_GOTO(L_EQV, num_logical('=', left, right))
CACHED_BINARY_GOTO(L_IMP, num_logical('>', left, right))
#undef CACHED_BINARY_GOTO
#undef CEXPR_NEXT
cexpr_done:
#else
    for (int i = 0; i < instruction_count; i++) {
        const CompiledExpressionInstruction *instruction = &instructions[i];
        // Numbers and variables, the commonest, are tested before the switch.
        if (instruction->opcode == CEXPR_NUMBER) {
            if (instruction->is_double) last_expression_is_double = 1;
            values[stack_size++] = instruction->number;
            continue;
        }
        if (instruction->opcode == CEXPR_VARIABLE) {
            int const_idx;
            Variable *variable;
            /* The instruction caches what the name resolved to, so a plain
             * variable read is a pointer load; the cache is only trusted at
             * module scope and while nothing has changed how names resolve. */
            if (instruction->resolve_state &&
                expression->name_cache_generation == expression_name_generation &&
                call_stack_depth == 0) {
                if (instruction->resolve_state == 1) {
                    variable = instruction->resolved_var;
                    const_idx = -1;
                } else if (instruction->resolve_state == 2) {
                    variable = NULL;
                    const_idx = instruction->argc;
                } else {
                    return 0;
                }
            } else {
                variable = resolve_cached_name(expression, &ts->tokens[instruction->token_index], &const_idx);
                if (call_stack_depth == 0) {
                    /* Inside a call the same name can mean a local of the
                     * frame, so only the program's own scope is cached. The
                     * expression's generation is stamped once the whole pass
                     * is through, in case a call changed things midway. */
                    CompiledExpressionInstruction *cache = (CompiledExpressionInstruction *)instruction;
                    if (variable) {
                        cache->resolved_var = variable;
                        cache->resolve_state = 1;
                    } else if (const_idx >= 0) {
                        cache->argc = const_idx;
                        cache->resolve_state = 2;
                    } else {
                        cache->resolve_state = 3;
                    }
                }
            }
            if (!variable) {
                if (const_idx < 0) return 0;
                if (named_constants[const_idx].is_wide) last_expression_is_double = 1;
                values[stack_size++] = named_constants[const_idx].value;
                continue;
            }
            if (var_is_wide(variable)) last_expression_is_double = 1;
            values[stack_size++] = load_num(variable, -1);
            continue;
        }
        switch (instruction->opcode) {
            case CEXPR_SUBSCRIPT:
                stack_size = cached_subscript(ts, expression, instruction, values, stack_size);
                if (stack_size < 0) return 0;
                continue;
            case CEXPR_ARRAY:
                stack_size = cached_array_element(ts, expression, instruction, values, stack_size);
                if (stack_size < 0) return 0;
                continue;
            case CEXPR_FUNCTION:
            case CEXPR_FUNCTION0:
            case CEXPR_QB64:
            case CEXPR_POINT:
                stack_size = cached_call(ts, instruction, values, stack_size);
                continue;
            case CEXPR_NEGATE:
                values[stack_size - 1] = num_negate(values[stack_size - 1]);
                continue;
            case CEXPR_NOT:
                values[stack_size - 1] = num_logical('~', values[stack_size - 1], values[stack_size - 1]);
                continue;
// Binary operators replace the top two operands; each is its own case, so
// an instruction takes one dispatch.
#define CACHED_BINARY(opcode, operation) \
            case opcode: { \
                Num right = values[--stack_size]; \
                Num left = values[stack_size - 1]; \
                values[stack_size - 1] = operation; \
                continue; \
            }
            CACHED_BINARY(CEXPR_POWER, num_power(left, right))
            CACHED_BINARY(CEXPR_ADD, num_add(left, right))
            CACHED_BINARY(CEXPR_SUBTRACT, num_subtract(left, right))
            CACHED_BINARY(CEXPR_MULTIPLY, num_multiply(left, right))
            CACHED_BINARY(CEXPR_DIVIDE, num_divide(left, right))
            CACHED_BINARY(CEXPR_MOD, num_integer_divide(left, right, 1))
            CACHED_BINARY(CEXPR_IDIV, num_integer_divide(left, right, 0))
            CACHED_BINARY(CEXPR_EQUAL, num_truth(num_compare(left, right) == 0))
            CACHED_BINARY(CEXPR_NOT_EQUAL, num_truth(num_compare(left, right) != 0))
            CACHED_BINARY(CEXPR_LESS, num_truth(num_compare(left, right) < 0))
            CACHED_BINARY(CEXPR_LESS_EQUAL, num_truth(num_compare(left, right) <= 0))
            CACHED_BINARY(CEXPR_GREATER, num_truth(num_compare(left, right) > 0))
            CACHED_BINARY(CEXPR_GREATER_EQUAL, num_truth(num_compare(left, right) >= 0))
            CACHED_BINARY(CEXPR_AND, num_logical('&', left, right))
            CACHED_BINARY(CEXPR_OR, num_logical('|', left, right))
            CACHED_BINARY(CEXPR_XOR, num_logical('^', left, right))
            CACHED_BINARY(CEXPR_EQV, num_logical('=', left, right))
            CACHED_BINARY(CEXPR_IMP, num_logical('>', left, right))
#undef CACHED_BINARY
        }
    }
#endif

    /* The whole pass resolved names with nothing changing underneath it, so
     * the per-instruction caches it filled can be trusted until the next
     * change. Anything unusual (calls that declare things, an aborted pass)
     * simply leaves the stamp stale and the next pass re-resolves. */
    if (call_stack_depth == 0 && expression_name_generation == pass_generation &&
        expression->name_cache_generation != pass_generation) {
        expression->name_cache_generation = pass_generation;
    }
    *result = values[0];
    ts->pos = expression->end_pos;
    return 1;
}

/* The cached-expression gate of evaluate_num_tok, split out so its hottest
 * call sites (IF conditions, assignment right sides) can skip the wrapper
 * calls and, for IF, the Num-to-double round trip. */
static inline int evaluate_num_tok_cached(TokenStream *ts, Num *result) {
    Statement *statement = ts->statement;
    if (statement && ts->tokens == statement->tokens &&
        ts->pos >= 0 && ts->pos < statement->token_count &&
        statement->compiled_expressions) {
        CompiledExpression *expression = statement->compiled_expressions[ts->pos];
        if (expression &&
            !fixed_string_declarations_present &&
            expression->rejected_proc_generation != proc_generation + 1) {
            return evaluate_cached_expression(ts, expression, result);
        }
    }
    return 0;
}

static Num evaluate_num_tok(TokenStream *ts) {
    Num result;
    if (evaluate_num_tok_cached(ts, &result)) return result;
    Statement *statement = ts->statement;
    if (statement && ts->tokens == statement->tokens &&
        ts->pos >= 0 && ts->pos < statement->token_count &&
        statement->compiled_expressions) {
        CompiledExpression *expression = statement->compiled_expressions[ts->pos];
        // DEFINT, DEFSTR, ... may since have changed what a name is; the
        // evaluator checks every name as it reads it.
        if (expression &&
            !fixed_string_declarations_present &&
            expression->rejected_proc_generation != proc_generation + 1) {
            Num result;
            if (evaluate_cached_expression(ts, expression, &result)) return result;
        }
    }

    Num val = equivalence_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_IMP) {
        ts->pos++;
        val = num_logical('>', val, equivalence_tok(ts));
    }
    return val;
}

double evaluate_expression_tok(TokenStream *ts) {
    return num_to_double(evaluate_num_tok(ts));
}

static const char *skip_whitespace_fast(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}


/* The statement interpret_line_at_ptr is executing. Expressions in its text
 * are evaluated with its tokens, so both evaluation paths share one parser. */
static Statement *text_statement = NULL;

/* Index of the token at text position p (which may sit on whitespace before
 * the token), or -1 if p is inside a token. */
static int token_index_at(const Statement *stmt, const char *p) {
    int low = 0, high = stmt->token_count;
    while (low < high) {
        int mid = (low + high + 1) / 2;
        if (stmt->tokens[mid].start_ptr <= p) low = mid;
        else high = mid - 1;
    }
    for (const char *c = stmt->tokens[low].start_ptr; c < p; c++) {
        if (!isspace((unsigned char)*c)) return -1;
    }
    return low;
}

static Num evaluate_num_text(const char **input) {
    const char *p = *input;
    Statement *stmt = text_statement;
    if (stmt && stmt->tokens && p >= stmt->raw_command &&
        p <= stmt->raw_command + strlen(stmt->raw_command)) {
        int pos = token_index_at(stmt, p);
        if (pos >= 0) {
            TokenStream ts = {stmt->tokens, pos, stmt};
            Num result = evaluate_num_tok(&ts);
            *input = stmt->tokens[ts.pos].start_ptr;
            return result;
        }
    }
    // Text from elsewhere (DEF FN bodies, direct input): tokenize a copy.
    Statement *copy = calloc(1, sizeof(Statement));
    if (!copy) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return num_i(0, NUM_INTEGER);
    }
    snprintf(copy->raw_command, sizeof(copy->raw_command), "%s", p);
    tokenize_line(copy);
    TokenStream ts = {copy->tokens, 0, NULL};
    Num result = evaluate_num_tok(&ts);
    *input = p + (copy->tokens[ts.pos].start_ptr - copy->raw_command);
    free(copy->tokens);
    free(copy);
    return result;
}

double evaluate_expression(const char **input) {
    return num_to_double(evaluate_num_text(input));
}

/* QBasic converts numeric arguments that must be whole numbers (string
 * positions, CHR$ codes, file numbers, LOCATE rows, ...) by rounding half
 * to even, like CINT, rather than truncating. */
static int round_to_int(double value) {
    double r = nearbyint(value);
    if (!(r > INT_MIN)) return INT_MIN; // also NaN
    if (r > INT_MAX) return INT_MAX;
    return (int)r;
}

static int evaluate_int_text(const char **input) {
    return round_to_int(evaluate_expression(input));
}

static int evaluate_int_tok(TokenStream *ts) {
    return round_to_int(evaluate_expression_tok(ts));
}

static void interpret_statement_text(Statement *stmt, const char **ptr) {
    Statement *saved = text_statement;
    text_statement = stmt;
    interpret_line_at_ptr(ptr, 0, NULL);
    text_statement = saved;
}

/* Parses "[STEP] (x, y)". STEP offsets are added to (base_x, base_y).
 * Returns 1 on success, 0 when no point is present (input untouched) and -1
 * on a malformed point. */
static int parse_graphics_point(const char **ptr, double base_x, double base_y, double *x, double *y) {
    const char *saved = *ptr;
    int relative = 0;
    Token t = get_next_token(ptr);
    if (t.type == TOKEN_STEP) {
        relative = 1;
        t = get_next_token(ptr);
    }
    if (t.type != TOKEN_LPAREN) {
        *ptr = saved;
        return 0;
    }
    *x = evaluate_expression(ptr);
    if (get_next_token(ptr).type != TOKEN_COMMA) return -1;
    *y = evaluate_expression(ptr);
    if (get_next_token(ptr).type != TOKEN_RPAREN) return -1;
    if (relative) {
        *x += base_x;
        *y += base_y;
    }
    return 1;
}

/* The mode set by the last SCREEN statement (-1 for _NEWIMAGE, -2 before any). */
static int current_screen_mode = -2;

static TokenType peek_token_type(const char *ptr) {
    return get_next_token(&ptr).type;
}

/* Consumes a comma separating optional arguments; returns 0 if there is none. */
static int next_graphics_comma(const char **ptr) {
    const char *saved = *ptr;
    if (get_next_token(ptr).type == TOKEN_COMMA) return 1;
    *ptr = saved;
    return 0;
}

/* True when an argument follows rather than an empty slot or statement end. */
static int graphics_arg_present(const char **ptr) {
    const char *peek = *ptr;
    Token t = get_next_token(&peek);
    return t.type != TOKEN_COMMA && t.type != TOKEN_COLON && t.type != TOKEN_EOF &&
           t.type != TOKEN_APOSTROPHE && t.type != TOKEN_ELSE && t.type != TOKEN_REM;
}

static int graphics_statement_end(const char **ptr) {
    const char *peek = *ptr;
    Token t = get_next_token(&peek);
    return t.type == TOKEN_COLON || t.type == TOKEN_EOF || t.type == TOKEN_APOSTROPHE ||
           t.type == TOKEN_ELSE || t.type == TOKEN_REM;
}

/* Evaluates an optional color argument after a comma. */
static int parse_optional_color(const char **ptr, unsigned int *color) {
    if (!graphics_arg_present(ptr)) return 0;
    *color = color_from_value(evaluate_expression(ptr));
    return 1;
}

/* SHARED name[()] [AS type][, ...] grants a procedure access to module
 * variables; STATIC name[()] [AS type][, ...] keeps the named locals between
 * calls. Both are no-ops outside a procedure. */
static void execute_shared_or_static(TokenStream *ts, int is_static) {
    Statement *stmt = ts->statement;
    if (call_stack_depth == 0) {
        ts->pos = stmt->token_count;
        return;
    }
    CallFrame *frame = &call_stack[call_stack_depth - 1];
    while (ts->pos < stmt->token_count && ts->tokens[ts->pos].type != TOKEN_COLON &&
           ts->tokens[ts->pos].type != TOKEN_EOF) {
        if (ts->tokens[ts->pos].type == TOKEN_IDENTIFIER) {
            char norm[64];
            snprintf(norm, sizeof(norm), "%s", ts->tokens[ts->pos].text);
            for (int c = 0; norm[c]; c++) norm[c] = (char)toupper((unsigned char)norm[c]);
            /* Optional "()" for arrays and "AS type" clause. */
            int look = ts->pos + 1;
            if (look + 1 < stmt->token_count &&
                ts->tokens[look].type == TOKEN_LPAREN &&
                ts->tokens[look + 1].type == TOKEN_RPAREN) look += 2;
            const char *suffix = NULL;
            if (look + 1 < stmt->token_count && ts->tokens[look].type == TOKEN_AS) {
                const char *type_name = ts->tokens[look + 1].text;
                look += 2;
                const char *combined = look < stmt->token_count
                    ? unsigned_type_name(type_name, ts->tokens[look].text) : NULL;
                if (combined) {
                    type_name = combined;
                    look++;
                }
                suffix = primitive_type_suffix(type_name);
            }
            size_t len = strlen(norm);
            if (len > 0 && !is_type_suffix_char(norm[len-1])) {
                if (!suffix && !is_static) {
                    for (int d = 0; d < declared_type_count; d++) {
                        if (declared_types[d].scope == NULL &&
                            strcmp(declared_types[d].name, norm) == 0) {
                            suffix = declared_types[d].suffix;
                            break;
                        }
                    }
                }
                if (!suffix && is_static) suffix = find_declared_suffix(norm);
                if (suffix) {
                    declare_variable_type(norm, suffix, frame->proc, 0);
                    if (len + strlen(suffix) < sizeof(norm)) strcat(norm, suffix);
                } else if (norm[0] >= 'A' && norm[0] <= 'Z') {
                    char default_suffix = default_type_map[norm[0] - 'A'];
                    if (default_suffix != '\0') {
                        norm[len] = default_suffix;
                        norm[len+1] = '\0';
                    }
                }
            }
            if (is_static) {
                add_static_name(frame->proc, norm);
            } else if (frame->shared_count < 32) {
                snprintf(frame->shared_var_names[frame->shared_count++],
                         sizeof(frame->shared_var_names[0]), "%s", norm);
            }
            ts->pos = look - 1;
        }
        ts->pos++;
        if (ts->pos < stmt->token_count && ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
    }
    clear_resolved_variables(frame);
}

/* There is no hardware to read, so I/O ports are simulated: &H3DA (the VGA
 * input status register) reports vertical retrace (bits 8 and 1) during the
 * first millisecond of every 1/60-second frame; every other port reads 0. */
#define VGA_STATUS_PORT 0x3DA
#define RETRACE_FRAME_SECONDS (1.0 / 60.0)
#define RETRACE_SECONDS 0.001

static int simulated_port_value(int port, double now) {
    if (port != VGA_STATUS_PORT) return 0;
    double phase = fmod(now, RETRACE_FRAME_SECONDS);
    return phase < RETRACE_SECONDS ? (8 | 1) : 0;
}

/* WAIT port, and_mask[, xor_mask]: pause until
 * (port XOR xor_mask) AND and_mask is nonzero. A condition the simulated
 * port can never meet returns at once instead of hanging. */
static void execute_wait(int port, int and_mask, int xor_mask) {
    if (port != VGA_STATUS_PORT) return;
    if (((0 ^ xor_mask) & and_mask) == 0 && (((8 | 1) ^ xor_mask) & and_mask) == 0) return;
    double now = wall_time_seconds();
    if (((simulated_port_value(port, now) ^ xor_mask) & and_mask) == 0) {
        // The status has two states, so the condition holds after its next
        // change: the start or end of a retrace. Sleep until then (a late
        // wake-up must not miss the short retrace window).
        double frame_start = floor(now / RETRACE_FRAME_SECONDS) * RETRACE_FRAME_SECONDS;
        double change = (now - frame_start < RETRACE_SECONDS)
            ? frame_start + RETRACE_SECONDS : frame_start + RETRACE_FRAME_SECONDS;
        while (!stop_running && (now = wall_time_seconds()) < change) {
            if (graphics_is_active()) handle_events();
            double remaining = change - now;
            usleep((useconds_t)(remaining * 1000000.0) + 1);
        }
    }
    // A program that waits for retrace has finished drawing its frame.
    if (graphics_is_active()) graphics_present_if_autodisplay();
}

static void execute_statements(Statement *curr, const char *resume_ptr, int resume_ts_pos);
static void run_program_from(Statement *start);
static unsigned int scanned_generation;
static int cont_valid;
static Statement *cont_stmt;
static const char *cont_resume_ptr;
static int cont_resume_ts_pos;
static unsigned int cont_generation;

/* ---- Program editing commands typed in direct mode ----
 * SAVE, LOAD, MERGE, RENUM, AUTO and EDIT are not reserved words, so they
 * are recognized only as the first word of a direct-mode line. */

static int auto_next_line = -1;
static int auto_increment = 10;
static char edit_text[BASIC_LINE_MAX + 16] = "";

/* AUTO: the line number the REPL should offer next, or -1. advance moves on. */
int basika_auto_line(int advance) {
    if (advance && auto_next_line >= 0) auto_next_line += auto_increment;
    return auto_next_line;
}

void basika_auto_cancel(void) {
    auto_next_line = -1;
}

/* EDIT: text the REPL should offer for editing on its next prompt. */
const char *basika_take_edit_text(void) {
    static char taken[300];
    snprintf(taken, sizeof(taken), "%s", edit_text);
    edit_text[0] = '\0';
    return taken;
}

/* Opens file, or file.bas when the name has no extension. */
static FILE *open_program_file(const char *name, const char *mode, char *resolved, size_t size) {
    snprintf(resolved, size, "%s", name);
    FILE *file = fopen(resolved, mode);
    const char *base = strrchr(name, '/');
    if (!file && !strchr(base ? base : name, '.')) {
        snprintf(resolved, size, "%s.bas", name);
        file = fopen(resolved, mode);
    }
    return file;
}

static void format_program_line(const Statement *stmt, char *out, size_t size) {
    if (stmt->has_explicit_line_number) {
        snprintf(out, size, "%d%s%s", stmt->line_number,
                 stmt->raw_command[0] == ' ' ? "" : " ", stmt->raw_command);
    } else {
        snprintf(out, size, "%s", stmt->raw_command);
    }
}

/* Rewrites the line numbers referenced in text (after GOTO, GOSUB, THEN,
 * ELSE, RESTORE, RESUME and RUN, and in ON ... GOTO/GOSUB lists). */
static void renumber_references(const char *text, const int *old_numbers, const int *new_numbers,
                                int count, char *out, size_t size) {
    size_t used = 0;
    out[0] = '\0';
    const char *p = text;
    int expect_line = 0, in_list = 0;
    while (*p) {
        const char *start = p;
        Token t = get_next_token(&p);
        if (t.type == TOKEN_EOF) break;
        const char *token_start = start;
        while (token_start < p && isspace((unsigned char)*token_start)) token_start++;
        const char *replacement = NULL;
        char number[16];
        if (t.type == TOKEN_NUMBER && expect_line) {
            for (int i = 0; i < count; i++) {
                if (old_numbers[i] == t.int_val) {
                    snprintf(number, sizeof(number), "%d", new_numbers[i]);
                    replacement = number;
                    break;
                }
            }
        }
        if (replacement) {
            used += (size_t)snprintf(out + used, used < size ? size - used : 0, "%.*s%s",
                                     (int)(token_start - start), start, replacement);
        } else {
            used += (size_t)snprintf(out + used, used < size ? size - used : 0, "%.*s",
                                     (int)(p - start), start);
        }
        if (t.type == TOKEN_GOTO || t.type == TOKEN_GOSUB) { expect_line = 1; in_list = 1; }
        else if (t.type == TOKEN_THEN || t.type == TOKEN_ELSE || t.type == TOKEN_RESTORE ||
                 t.type == TOKEN_RESUME || t.type == TOKEN_RUN) { expect_line = 1; in_list = 0; }
        else if (t.type == TOKEN_COMMA && in_list) expect_line = 1;
        else if (t.type == TOKEN_NUMBER && in_list) expect_line = 0;
        else { expect_line = 0; in_list = 0; }
        if (used >= size) break;
    }
    if (used < size) snprintf(out + used, size - used, "%s", p);
}

/* RENUM [new][, [old][, increment]] */
static void renumber_program(int new_start, int old_start, int increment) {
    int count = 0;
    for (Statement *s = get_head(); s; s = s->next) count++;
    if (count == 0) return;
    int *old_numbers = malloc(sizeof(int) * (size_t)count);
    int *new_numbers = malloc(sizeof(int) * (size_t)count);
    char (*texts)[300] = malloc(sizeof(*texts) * (size_t)count);
    int *sources = malloc(sizeof(int) * (size_t)count);
    int *explicit_numbers = malloc(sizeof(int) * (size_t)count);
    if (!old_numbers || !new_numbers || !texts || !sources || !explicit_numbers) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        goto done;
    }
    int i = 0, next = new_start, highest_kept = -1;
    for (Statement *s = get_head(); s; s = s->next, i++) {
        old_numbers[i] = s->line_number;
        sources[i] = s->source_line_number;
        explicit_numbers[i] = s->has_explicit_line_number;
        if (s->line_number < old_start) {
            new_numbers[i] = s->line_number;
            highest_kept = s->line_number;
        } else {
            new_numbers[i] = next;
            next += increment;
        }
    }
    // Renumbered lines may not move in front of the lines that stay put.
    if (new_start <= highest_kept || next - increment > 65529) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        goto done;
    }
    i = 0;
    for (Statement *s = get_head(); s; s = s->next, i++) {
        renumber_references(s->raw_command, old_numbers, new_numbers, count, texts[i], sizeof(texts[i]));
    }
    clear_program();
    for (i = 0; i < count; i++) {
        int renumbered = old_numbers[i] >= old_start;
        add_line(new_numbers[i], texts[i], sources[i], renumbered ? 1 : explicit_numbers[i]);
    }
done:
    free(old_numbers);
    free(new_numbers);
    free(texts);
    free(sources);
    free(explicit_numbers);
}

/* Handles a direct-mode editing command; returns 0 if the line is not one. */
static int execute_editor_command(const char *line) {
    const char *ptr = line;
    Token command = get_next_token(&ptr);
    if (command.type != TOKEN_IDENTIFIER || peek_token_type(ptr) == TOKEN_EQUALS) return 0;
    current_executing_line = DIRECT_LINE_NUMBER;
    char path[256] = "", resolved[512];
    if (strcasecmp(command.text, "SAVE") == 0) {
        parse_string_expression(&ptr, path, sizeof(path));
        if (next_graphics_comma(&ptr)) get_next_token(&ptr); // ,A: always ASCII
        snprintf(resolved, sizeof(resolved), "%s", path);
        const char *base = strrchr(path, '/');
        if (!strchr(base ? base : path, '.')) snprintf(resolved, sizeof(resolved), "%s.bas", path);
        if (!path[0]) {
            report_runtime_error(ERR_BAD_FILE_NAME);
            return 1;
        }
        FILE *file = fopen(resolved, "w");
        if (!file) {
            report_runtime_error(ERR_PATH_FILE_ACCESS_ERROR);
            return 1;
        }
        char text[BASIC_LINE_MAX + 16];
        for (Statement *s = get_head(); s; s = s->next) {
            format_program_line(s, text, sizeof(text));
            fprintf(file, "%s\n", text);
        }
        fclose(file);
    } else if (strcasecmp(command.text, "LOAD") == 0 || strcasecmp(command.text, "MERGE") == 0) {
        int merging = toupper((unsigned char)command.text[0]) == 'M';
        parse_string_expression(&ptr, path, sizeof(path));
        int run_after = 0;
        if (!merging && next_graphics_comma(&ptr)) {
            Token flag = get_next_token(&ptr);
            run_after = flag.type == TOKEN_IDENTIFIER && strcasecmp(flag.text, "R") == 0;
        }
        FILE *file = open_program_file(path, "r", resolved, sizeof(resolved));
        if (!file) {
            report_runtime_error(ERR_FILE_NOT_FOUND);
            return 1;
        }
        int last_line_num = 0;
        if (merging) {
            for (Statement *s = get_head(); s; s = s->next) last_line_num = s->line_number;
        } else {
            clear_program();
            clear_variables(0);
            clear_data_pointer();
        }
        load_program_lines(file, resolved, &last_line_num);
        fclose(file);
        if (run_after) run_program();
    } else if (strcasecmp(command.text, "RENUM") == 0) {
        int values[3] = {10, 0, 10};
        int index = 0;
        while (index < 3) {
            if (graphics_arg_present(&ptr)) values[index] = evaluate_int_text(&ptr);
            index++;
            if (!next_graphics_comma(&ptr)) break;
        }
        if (values[0] < 0 || values[1] < 0 || values[2] < 1) {
            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            return 1;
        }
        renumber_program(values[0], values[1], values[2]);
    } else if (strcasecmp(command.text, "AUTO") == 0) {
        int start = 10, increment = 10;
        if (graphics_arg_present(&ptr)) start = evaluate_int_text(&ptr);
        if (next_graphics_comma(&ptr)) increment = evaluate_int_text(&ptr);
        if (start < 0 || increment < 1) {
            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            return 1;
        }
        auto_next_line = start;
        auto_increment = increment;
    } else if (strcasecmp(command.text, "EDIT") == 0) {
        Token number = get_next_token(&ptr);
        Statement *stmt = number.type == TOKEN_NUMBER ? find_line(number.int_val) : NULL;
        if (!stmt) {
            report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
            return 1;
        }
        format_program_line(stmt, edit_text, sizeof(edit_text));
    } else {
        return 0;
    }
    return 1;
}

/* Runs a line typed without a line number through the same statement loop
 * as programs, so colons, one-line loops and GOSUB work, and GOTO continues
 * into the stored program without clearing variables. */
static void execute_direct_line(const char *text) {
    if (scanned_generation != program_edit_generation()) {
        if (!scan_user_types()) return;
        scan_procedures();
        scanned_generation = program_edit_generation();
    }
    Statement *stmt = calloc(1, sizeof(Statement));
    if (!stmt) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        return;
    }
    stmt->line_number = DIRECT_LINE_NUMBER;
    snprintf(stmt->raw_command, sizeof(stmt->raw_command), "%s", text);
    tokenize_line(stmt);
    stop_running = 0;
    break_requested = 0;
    runtime_error_occurred = 0;
    execute_statements(stmt, NULL, -1);
    for (int i = 0; i < for_ptr; i++) {
        if (for_stack[i].start_stmt == stmt) { for_ptr = i; break; }
    }
    free(stmt->tokens);
    free(stmt);
}

static void close_file_number(int fnum) {
    if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) return;
    fclose(file_handles[fnum]);
    file_handles[fnum] = NULL;
    reset_file_field_state(fnum);
}

/* Byte offset of record `record` for PUT/GET on a RANDOM (LEN-byte records)
 * or BINARY (1-based byte position) file; reports Bad record number below 1. */
static int file_record_offset(int fnum, double record, long *offset) {
    if (record < 1.0 || record > 2147483647.0) {
        report_runtime_error(ERR_BAD_RECORD_NUMBER);
        return 0;
    }
    long position = (long)record;
    *offset = file_mode[fnum] == FILE_MODE_BINARY
        ? position - 1 : (position - 1) * (long)file_record_length[fnum];
    return 1;
}

/* Parses the optional record argument of PUT #n/GET #n. An omitted record
 * means the record (or byte) after the last one used. */
static double parse_record_number(const char **ptr, int fnum, int *has_more) {
    double record = -1;
    *has_more = 0;
    if (next_graphics_comma(ptr)) {
        if (graphics_arg_present(ptr)) record = evaluate_expression(ptr);
        *has_more = next_graphics_comma(ptr);
    }
    if (record < 0 && fnum >= 1 && fnum < 16) record = (double)file_last_record[fnum] + 1;
    return record;
}

/* CLEAR: zero every variable and array (keeping their dimensions), empty
 * strings, close all files and reset the GOSUB/FOR/WHILE/DO stacks. CONST
 * values, TYPE declarations and DEF FN functions are kept. */
static void clear_statement(void) {
    for (int i = 0; i < var_count; i++) {
        Variable *v = &vars[i];
        v->value = 0;
        if (v->array) memset(v->array, 0, (size_t)v->array_size * sizeof(*v->array));
        if (v->s_array) {
            for (int j = 0; j < v->array_size; j++) {
                basic_string_destroy(v->s_array[j]);
                v->s_array[j] = NULL;
            }
        }
        if (v->s_value) {
            basic_string_destroy(v->s_value);
            v->s_value = NULL;
        }
        if (v->string_declared && v->num_dims == 0) assign_string_variable_value(i, -1, NULL, 0);
    }
    for (int i = 0; i < static_local_count; i++) {
        release_variable_storage(&static_locals[i].var);
        static_locals[i].var.value = 0;
    }
    for (int fnum = 1; fnum < 16; fnum++) close_file_number(fnum);
    gosub_ptr = 0;
    for_ptr = 0;
    while_ptr = 0;
    do_ptr = 0;
}

/* CHAIN passes the variables listed in COMMON to the next program by
 * position: their values are copied here, and the new program's COMMON
 * statements take them in the same order. */
#define MAX_COMMON_VALUES 128
static Variable chain_values[MAX_COMMON_VALUES];
static int chain_is_string[MAX_COMMON_VALUES];
static int chain_value_count = 0;
static int chain_value_next = 0;
static int common_variable_count = 0;
static int common_variables[MAX_COMMON_VALUES];
static char pending_chain_path[512] = "";

static void release_chain_values(void) {
    for (int i = 0; i < chain_value_count; i++) release_variable_storage(&chain_values[i]);
    chain_value_count = 0;
    chain_value_next = 0;
}

/* COMMON [SHARED] [/block/] var[()] [AS type][, ...] */
static void execute_common(const char **ptr) {
    if (call_stack_depth > 0) {
        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }
    int shared = 0;
    const char *saved = *ptr;
    if (get_next_token(ptr).type == TOKEN_SHARED) shared = 1;
    else *ptr = saved;
    saved = *ptr;
    if (get_next_token(ptr).type == TOKEN_SLASH) {
        // Named blocks are accepted; all COMMON variables share one list.
        get_next_token(ptr);
        get_next_token(ptr);
    } else {
        *ptr = saved;
    }
    while (1) {
        Token var = get_next_token(ptr);
        if (var.type != TOKEN_IDENTIFIER) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return;
        }
        saved = *ptr;
        if (get_next_token(ptr).type == TOKEN_LPAREN && get_next_token(ptr).type == TOKEN_RPAREN) {
            saved = *ptr;
        }
        *ptr = saved;
        if (get_next_token(ptr).type == TOKEN_AS) {
            Token type = get_next_token(ptr);
            const char *type_name = type.text;
            const char *unsigned_saved = *ptr;
            if (strcasecmp(type_name, "_UNSIGNED") == 0) {
                Token width = get_next_token(ptr);
                if (unsigned_type_name(type_name, width.text)) type_name = unsigned_type_name(type_name, width.text);
                else *ptr = unsigned_saved;
            }
            const char *suffix = primitive_type_suffix(type_name);
            if (!suffix) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return;
            }
            declare_variable_type(var.text, suffix, NULL, shared);
        } else {
            *ptr = saved;
        }
        int idx = find_variable(var.text);
        if (idx < 0) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            return;
        }
        if (shared && global_shared_count < MAX_GLOBAL_SHARED) {
            GlobalSharedName *entry = &global_shared_names[global_shared_count++];
            snprintf(entry->full, sizeof(entry->full), "%s", vars[idx].name);
            snprintf(entry->base, sizeof(entry->base), "%.*s",
                     (int)numeric_base_length(vars[idx].name, strlen(vars[idx].name)), vars[idx].name);
        }
        int listed = 0;
        for (int i = 0; i < common_variable_count; i++) if (common_variables[i] == idx) listed = 1;
        if (!listed && common_variable_count < MAX_COMMON_VALUES) common_variables[common_variable_count++] = idx;
        // Values passed by CHAIN arrive in COMMON order.
        if (!listed && chain_value_next < chain_value_count) {
            int is_string = is_string_var(vars[idx].name);
            if (is_string != chain_is_string[chain_value_next]) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return;
            }
            if (!copy_variable_contents(&vars[idx], &chain_values[chain_value_next])) return;
            chain_value_next++;
        }
        if (!next_graphics_comma(ptr)) break;
    }
}

/* CHAIN file$[, line]: remembers the COMMON values and stops this program;
 * run_program_from loads and starts the next one. */
static void execute_chain(const char **ptr) {
    char path[256] = "";
    parse_string_expression(ptr, path, sizeof(path));
    if (next_graphics_comma(ptr)) (void)evaluate_expression(ptr); // GW-BASIC start line: ignored
    char resolved[512];
    snprintf(resolved, sizeof(resolved), "%s", path);
    FILE *probe = fopen(resolved, "r");
    if (!probe) {
        snprintf(resolved, sizeof(resolved), "%s.bas", path);
        probe = fopen(resolved, "r");
    }
    if (!probe) {
        report_runtime_error(ERR_FILE_NOT_FOUND);
        return;
    }
    fclose(probe);
    release_chain_values();
    for (int i = 0; i < common_variable_count && i < MAX_COMMON_VALUES; i++) {
        Variable *source = &vars[common_variables[i]];
        memset(&chain_values[i], 0, sizeof(Variable));
        snprintf(chain_values[i].name, sizeof(chain_values[i].name), "%s", source->name);
        chain_is_string[i] = is_string_var(source->name);
        if (!copy_variable_contents(&chain_values[i], source)) return;
        if (source->s_value && !chain_values[i].s_value) {
            basic_string_assign(&chain_values[i].s_value, source->s_value->data, source->s_value->length);
        }
        chain_value_count++;
    }
    snprintf(pending_chain_path, sizeof(pending_chain_path), "%s", resolved);
    stop_running = 1;
}

/* Replaces the program with the lines of a .bas file (CHAIN and LOAD). */
/* True when line ends in a QB64 line continuation: a "_" after a space,
 * outside strings and ' comments. Removes the "_". */
static int take_line_continuation(char *line) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) n--;
    if (n == 0 || line[n - 1] != '_' || (n >= 2 && line[n - 2] != ' ' && line[n - 2] != '\t')) return 0;
    int in_string = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (line[i] == '"') in_string = !in_string;
        else if (!in_string && line[i] == '\'') return 0;
    }
    if (in_string) return 0;
    line[n - 1] = '\0';
    return 1;
}

/* Reads one program line from a file: any length up to size - 1 characters,
 * without its CR/LF, with " _" continuation lines joined on. *physical_line
 * counts the file lines read. Returns 0 at the end of the file. */
int read_program_line(FILE *file, char *line, size_t size, int *physical_line) {
    static char *buffer = NULL;
    static size_t capacity = 0;
    size_t used = 0;
    line[0] = '\0';
    while (1) {
        ssize_t got = getline(&buffer, &capacity, file);
        if (got < 0) return used > 0 || line[0];
        (*physical_line)++;
        buffer[strcspn(buffer, "\r\n")] = '\0';
        size_t length = strlen(buffer);
        if (used + length >= size) length = size - 1 - used;
        memcpy(line + used, buffer, length);
        used += length;
        line[used] = '\0';
        if (!take_line_continuation(line)) return 1;
        used = strlen(line);
    }
}

/* ---- Metacommands read while loading a program: $INCLUDE, $INCLUDEONCE,
 * $IF / $ELSEIF / $ELSE / $END IF, $LET, $SCREENHIDE and $SCREENSHOW ---- */

#define MAX_INCLUDE_DEPTH 16
#define MAX_META_IF_DEPTH 32
#define MAX_META_VARS 64
#define MAX_INCLUDE_ONCE 128

static struct { char name[48]; long value; } meta_vars[MAX_META_VARS];
static int meta_var_count = 0;
static struct { int parent_active, taken, active; } meta_if[MAX_META_IF_DEPTH];
static int meta_if_depth = 0;
static char include_once_seen[MAX_INCLUDE_ONCE][PATH_MAX];
static int include_once_count = 0;

static void meta_set_var(const char *name, long value) {
    for (int i = 0; i < meta_var_count; i++) {
        if (strcasecmp(meta_vars[i].name, name) == 0) {
            meta_vars[i].value = value;
            return;
        }
    }
    if (meta_var_count >= MAX_META_VARS) return;
    snprintf(meta_vars[meta_var_count].name, sizeof(meta_vars[0].name), "%s", name);
    meta_vars[meta_var_count++].value = value;
}

static void meta_reset(void) {
    meta_var_count = 0;
    meta_if_depth = 0;
    include_once_count = 0;
#ifdef __APPLE__
    meta_set_var("MAC", -1); meta_set_var("MACOSX", -1);
    meta_set_var("LINUX", 0);
#else
    meta_set_var("MAC", 0); meta_set_var("MACOSX", 0);
    meta_set_var("LINUX", __linux__ ? -1 : 0);
#endif
    meta_set_var("WIN", 0); meta_set_var("WINDOWS", 0);
    meta_set_var("32BIT", sizeof(void *) == 4 ? -1 : 0);
    meta_set_var("64BIT", sizeof(void *) == 8 ? -1 : 0);
}

/* A name from the $IF/$LET words, or a number; unknown names are 0. */
static long meta_value(const char *word) {
    if (isdigit((unsigned char)word[0]) || (word[0] == '-' && isdigit((unsigned char)word[1]))) {
        return strtol(word, NULL, 0);
    }
    for (int i = 0; i < meta_var_count; i++) {
        if (strcasecmp(meta_vars[i].name, word) == 0) return meta_vars[i].value;
    }
    return 0;
}

/* Splits text into words: names, numbers and the operators = <> < > <= >=. */
static int meta_words(const char *text, char words[][48], int max) {
    int count = 0;
    while (*text && count < max) {
        while (isspace((unsigned char)*text)) text++;
        if (!*text || *text == '\'') break;
        int n = 0;
        if (strchr("=<>", *text)) {
            while (*text && strchr("=<>", *text) && n < 3) words[count][n++] = *text++;
        } else {
            while (*text && !isspace((unsigned char)*text) && !strchr("=<>", *text) && n < 47) {
                words[count][n++] = *text++;
            }
        }
        words[count][n] = '\0';
        count++;
    }
    return count;
}

/* Evaluates "[NOT] a [op b] {AND|OR [NOT] c [op d]}" with a trailing THEN dropped. */
static int meta_condition(const char *text) {
    char words[24][48];
    int count = meta_words(text, words, 24);
    if (count > 0 && strcasecmp(words[count - 1], "THEN") == 0) count--;
    int result = 0, have_result = 0, pending_or = 0, i = 0;
    while (i < count) {
        int negate = 0;
        while (i < count && strcasecmp(words[i], "NOT") == 0) { negate = !negate; i++; }
        if (i >= count) break;
        long left = meta_value(words[i++]);
        int truth = left != 0;
        if (i + 1 < count && strchr("=<>", words[i][0])) {
            const char *op = words[i];
            long right = meta_value(words[i + 1]);
            if (strcmp(op, "=") == 0) truth = left == right;
            else if (strcmp(op, "<>") == 0) truth = left != right;
            else if (strcmp(op, "<") == 0) truth = left < right;
            else if (strcmp(op, ">") == 0) truth = left > right;
            else if (strcmp(op, "<=") == 0) truth = left <= right;
            else if (strcmp(op, ">=") == 0) truth = left >= right;
            i += 2;
        }
        if (negate) truth = !truth;
        if (!have_result) result = truth;
        else result = pending_or ? (result || truth) : (result && truth);
        have_result = 1;
        if (i < count) {
            pending_or = strcasecmp(words[i], "OR") == 0;
            i++;
        }
    }
    return result;
}

static int meta_active(void) { return meta_if_depth == 0 || meta_if[meta_if_depth - 1].active; }

/* If line is a conditional metacommand, applies it and returns 1. */
static int meta_conditional_line(const char *line) {
    if (line[0] != '$') return 0;
    if (strncasecmp(line, "$IF", 3) == 0 && !isalnum((unsigned char)line[3])) {
        int parent = meta_active();
        int truth = parent && meta_condition(line + 3);
        if (meta_if_depth < MAX_META_IF_DEPTH) {
            meta_if[meta_if_depth].parent_active = parent;
            meta_if[meta_if_depth].taken = truth;
            meta_if[meta_if_depth].active = truth;
            meta_if_depth++;
        }
        return 1;
    }
    if (strncasecmp(line, "$ELSEIF", 7) == 0 && meta_if_depth > 0) {
        int top = meta_if_depth - 1;
        int truth = meta_if[top].parent_active && !meta_if[top].taken && meta_condition(line + 7);
        meta_if[top].active = truth;
        if (truth) meta_if[top].taken = 1;
        return 1;
    }
    if (strncasecmp(line, "$ELSE", 5) == 0 && !isalnum((unsigned char)line[5]) && meta_if_depth > 0) {
        int top = meta_if_depth - 1;
        meta_if[top].active = meta_if[top].parent_active && !meta_if[top].taken;
        meta_if[top].taken = 1;
        return 1;
    }
    if ((strncasecmp(line, "$END IF", 7) == 0 || strncasecmp(line, "$ENDIF", 6) == 0) && meta_if_depth > 0) {
        meta_if_depth--;
        return 1;
    }
    if (strncasecmp(line, "$LET", 4) == 0 && isspace((unsigned char)line[4])) {
        if (meta_active()) {
            char words[4][48];
            int count = meta_words(line + 4, words, 4);
            if (count >= 3 && strcmp(words[1], "=") == 0) meta_set_var(words[0], meta_value(words[2]));
        }
        return 1;
    }
    return 0;
}

/* If line is $INCLUDE:'file' (or the comment form '$INCLUDE:'file'), copies
 * the file name into name and returns 1. */
static int meta_include_name(const char *line, char *name, size_t size) {
    if (line[0] == '\'') line++;
    if (strncasecmp(line, "$INCLUDE", 8) != 0 || strncasecmp(line, "$INCLUDEONCE", 12) == 0) return 0;
    line += 8;
    while (isspace((unsigned char)*line)) line++;
    if (*line++ != ':') return 0;
    while (isspace((unsigned char)*line)) line++;
    if (*line++ != '\'') return 0;
    const char *end = strchr(line, '\'');
    if (!end || (size_t)(end - line) >= size) return 0;
    memcpy(name, line, (size_t)(end - line));
    name[end - line] = '\0';
    return 1;
}

static int load_source_file(FILE *file, const char *path, int *last_line_num, int depth);

static int starts_with_word(const char *text, const char *word) {
    size_t length = strlen(word);
    return strncasecmp(text, word, length) == 0 && !isalnum((unsigned char)text[length]) &&
           text[length] != '_';
}

/* A one-line procedure, SUB P: PRINT G: END SUB, is loaded as its header,
 * its body and its END SUB on separate lines, the way the procedure scan
 * expects them. Returns 0 for any other line. A numbered line keeps its number
 * on the header; the body and END SUB take the next two numbers. */
static int load_one_line_procedure(const char *line, int *last_line_num, int source_line_number) {
    const char *header = line;
    long number = -1;
    if (isdigit((unsigned char)*line)) {
        char *after;
        number = strtol(line, &after, 10);
        line = after;
        while (isspace((unsigned char)*line)) line++;
    }
    const char *keyword = starts_with_word(line, "SUB") ? "SUB"
                        : starts_with_word(line, "FUNCTION") ? "FUNCTION" : NULL;
    if (!keyword) return 0;
    const char *first_colon = NULL, *last_colon = NULL;
    int in_string = 0;
    for (const char *p = line; *p; p++) {
        if (*p == '"') in_string = !in_string;
        else if (in_string) continue;
        else if (*p == '\'') break;
        else if (*p == ':') {
            if (!first_colon) first_colon = p;
            last_colon = p;
        }
    }
    if (!first_colon) return 0;
    // The last part closes the procedure when it is END SUB / END FUNCTION.
    const char *tail = last_colon + 1;
    while (isspace((unsigned char)*tail)) tail++;
    int has_end = 0;
    if (starts_with_word(tail, "END")) {
        const char *what = tail + 3;
        while (isspace((unsigned char)*what)) what++;
        if (starts_with_word(what, keyword)) {
            const char *rest = what + strlen(keyword);
            while (isspace((unsigned char)*rest)) rest++;
            has_end = *rest == '\0' || *rest == '\'';
        }
    }
    if (!has_end) last_colon = NULL;

    char part[BASIC_LINE_MAX], text[BASIC_LINE_MAX];
    snprintf(part, sizeof(part), "%.*s", (int)(first_colon - header), header);
    interpret_line(part, 1, last_line_num, source_line_number);
    const char *body = first_colon + 1;
    int body_length = last_colon ? (int)(last_colon - body) : (int)strlen(body);
    if (body_length < 0) body_length = 0;
    int blank = 1;
    for (int i = 0; i < body_length; i++) blank &= isspace((unsigned char)body[i]) != 0;
    if (!blank) {
        snprintf(text, sizeof(text), "%.*s", body_length, body);
        if (number >= 0) snprintf(part, sizeof(part), "%ld %s", number + 1, text);
        else snprintf(part, sizeof(part), "%s", text);
        interpret_line(part, 1, last_line_num, source_line_number);
    }
    if (has_end) {
        if (number >= 0) {
            snprintf(part, sizeof(part), "%ld %s", number + 2, tail);
            interpret_line(part, 1, last_line_num, source_line_number);
        } else {
            interpret_line(tail, 1, last_line_num, source_line_number);
        }
    }
    return 1;
}

static void include_program_file(const char *name, const char *including_path, int *last_line_num, int depth) {
    char candidate[PATH_MAX], resolved[PATH_MAX];
    FILE *file = NULL;
    // Relative to the including file first, then the current folder.
    const char *slash = including_path ? strrchr(including_path, '/') : NULL;
    if (slash && name[0] != '/') {
        snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)(slash - including_path), including_path, name);
        file = open_program_file(candidate, "r", resolved, sizeof(resolved));
    }
    if (!file) file = open_program_file(name, "r", resolved, sizeof(resolved));
    if (!file) {
        fprintf(stderr, "Include file not found: %s\n", name);
        return;
    }
    load_source_file(file, resolved, last_line_num, depth + 1);
    fclose(file);
}

/* Reads a program source file line by line, expanding includes and dropping
 * the lines a false $IF branch skips; hands the rest to interpret_line.
 * Returns 0 if the file asked to be skipped ($INCLUDEONCE seen before). */
static int load_source_file(FILE *file, const char *path, int *last_line_num, int depth) {
    char buffer[BASIC_LINE_MAX];
    int first_line = depth == 0, physical_line = 0;
    int if_base = meta_if_depth;
    while (1) {
        int source_line_number = physical_line + 1;
        if (!read_program_line(file, buffer, sizeof(buffer), &physical_line)) break;
        if (first_line) {
            first_line = 0;
            if (buffer[0] == '#' && buffer[1] == '!') continue;
        }
        const char *line = buffer;
        while (isspace((unsigned char)*line)) line++;
        if (meta_conditional_line(line)) continue;
        if (!meta_active()) continue;
        if (strncasecmp(line, "$INCLUDEONCE", 12) == 0) {
            char real[PATH_MAX];
            if (!realpath(path, real)) snprintf(real, sizeof(real), "%s", path);
            int seen = 0;
            for (int i = 0; i < include_once_count; i++) seen |= strcmp(include_once_seen[i], real) == 0;
            if (seen) {
                meta_if_depth = if_base;
                return 0;
            }
            if (include_once_count < MAX_INCLUDE_ONCE) {
                snprintf(include_once_seen[include_once_count++], PATH_MAX, "%s", real);
            }
            continue;
        }
        char name[PATH_MAX];
        if (meta_include_name(line, name, sizeof(name))) {
            if (depth + 1 >= MAX_INCLUDE_DEPTH) fprintf(stderr, "Includes nested too deeply: %s\n", name);
            else include_program_file(name, path, last_line_num, depth);
            continue;
        }
        if (strncasecmp(line, "$SCREENHIDE", 11) == 0) graphics_set_screen_hidden(1);
        else if (strncasecmp(line, "$SCREENSHOW", 11) == 0) graphics_set_screen_hidden(0);
        if (load_one_line_procedure(line, last_line_num, depth == 0 ? source_line_number : 0)) continue;
        interpret_line(buffer, 1, last_line_num, depth == 0 ? source_line_number : 0);
    }
    meta_if_depth = if_base;
    return 1;
}

/* Loads a program file's lines into the program, as LOAD, MERGE and running
 * a file do; last_line_num is where automatic line numbering continues. */
void load_program_lines(FILE *file, const char *path, int *last_line_num) {
    meta_reset();
    load_source_file(file, path, last_line_num, 0);
}

static int load_program_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    clear_program();
    int last_line_num = 0;
    load_program_lines(file, path, &last_line_num);
    fclose(file);
    return 1;
}

/* LOCK/UNLOCK #n[, record | [first] TO last]: POSIX advisory locks, so a
 * range another process holds is "Permission denied". RANDOM files lock
 * records, BINARY files bytes, and other files the whole file. */
static void execute_lock(const char **ptr, int locking) {
    const char *saved = *ptr;
    if (get_next_token(ptr).type != TOKEN_HASH) *ptr = saved;
    int fnum = evaluate_int_text(ptr);
    if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
        report_runtime_error(ERR_BAD_FILE_NUMBER);
        return;
    }
    double first = 0, last = 0;
    int whole_file = 1;
    if (next_graphics_comma(ptr)) {
        whole_file = 0;
        first = 1;
        if (peek_token_type(*ptr) != TOKEN_TO) first = evaluate_expression(ptr);
        last = first;
        if (peek_token_type(*ptr) == TOKEN_TO) {
            get_next_token(ptr);
            last = evaluate_expression(ptr);
        }
        if (first < 1 || last < first) {
            report_runtime_error(ERR_BAD_RECORD_NUMBER);
            return;
        }
    }
    struct flock region;
    memset(&region, 0, sizeof(region));
    region.l_type = locking ? F_WRLCK : F_UNLCK;
    region.l_whence = SEEK_SET;
    if (!whole_file && (file_mode[fnum] == FILE_MODE_RANDOM || file_mode[fnum] == FILE_MODE_BINARY)) {
        long unit = file_mode[fnum] == FILE_MODE_BINARY ? 1 : file_record_length[fnum];
        region.l_start = (off_t)((long)first - 1) * unit;
        region.l_len = (off_t)((long)last - (long)first + 1) * unit;
    }
    fflush(file_handles[fnum]);
    if (fcntl(fileno(file_handles[fnum]), F_SETLK, &region) != 0 && locking) {
        report_runtime_error(ERR_PERMISSION_DENIED);
    }
}

/* $RESIZE:{ON|OFF|STRETCH|SMOOTH}. Returns 0 when text is not a valid
 * $RESIZE line; applies it unless check_only is set. */
static int apply_resize_metacommand(const char *text, int check_only) {
    text = skip_whitespace_fast(text);
    if (strncasecmp(text, "$RESIZE", 7) != 0) return 0;
    text = skip_whitespace_fast(text + 7);
    if (*text++ != ':') return 0;
    text = skip_whitespace_fast(text);
    static const struct { const char *name; int allow, scaling; } modes[] = {
        {"ON", 1, RESIZE_SCALE_NONE}, {"OFF", 0, RESIZE_SCALE_NONE},
        {"STRETCH", 1, RESIZE_SCALE_STRETCH}, {"SMOOTH", 1, RESIZE_SCALE_SMOOTH},
    };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        size_t len = strlen(modes[i].name);
        if (strncasecmp(text, modes[i].name, len) != 0) continue;
        const char *rest = skip_whitespace_fast(text + len);
        if (*rest != '\0' && *rest != '\'') continue;
        if (!check_only) graphics_set_resize(modes[i].allow, modes[i].scaling);
        return 1;
    }
    return 0;
}

/* _RESIZE [{ON|OFF}][, {_STRETCH|_SMOOTH}] */
/* The QB64 sound statements: _SNDPLAY, _SNDLOOP, _SNDSTOP, _SNDPAUSE and
 * _SNDCLOSE handle; _SNDVOL handle, volume; _SNDPLAYCOPY handle[, volume];
 * _SNDPLAYFILE file$[, , volume]. Volumes run from 0 to 1. Returns 0 if name
 * is not one of them. */
static int execute_sound_statement(const char *name, const char **ptr) {
    if (strncasecmp(name, "_SND", 4) != 0) return 0;
    const char *command = name + 4;
    if (strcasecmp(command, "PLAYFILE") == 0) {
        char path[1024] = "";
        parse_string_expression(ptr, path, sizeof(path));
        double volume = 1;
        if (next_graphics_comma(ptr)) {
            // The second argument (QB64's ignored sync flag) may be left out.
            if (graphics_arg_present(ptr)) (void)evaluate_expression(ptr);
            if (next_graphics_comma(ptr)) volume = evaluate_expression(ptr);
        }
        int handle = audio_sound_open(path);
        if (!handle) {
            report_runtime_error(ERR_FILE_NOT_FOUND);
            return 1;
        }
        audio_sound_play_copy(handle, volume);
        audio_sound_close(handle);
        return 1;
    }

    static const char *const commands[] = {"PLAY", "LOOP", "STOP", "PAUSE", "CLOSE", "VOL", "PLAYCOPY"};
    int which = -1;
    for (int i = 0; i < (int)(sizeof(commands) / sizeof(commands[0])); i++) {
        if (strcasecmp(command, commands[i]) == 0) which = i;
    }
    if (which < 0) return 0;
    int handle = evaluate_int_text(ptr);
    double volume = 1;
    if (which == 5) {
        if (!next_graphics_comma(ptr)) {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return 1;
        }
        volume = evaluate_expression(ptr);
    } else if (which == 6 && next_graphics_comma(ptr)) {
        volume = evaluate_expression(ptr);
    }
    int ok;
    switch (which) {
        case 0: ok = audio_sound_play(handle, 0); break;
        case 1: ok = audio_sound_play(handle, 1); break;
        case 2: ok = audio_sound_stop(handle); break;
        case 3: ok = audio_sound_pause(handle); break;
        case 4: ok = audio_sound_close(handle); break;
        case 5: ok = audio_sound_volume(handle, volume); break;
        default: ok = audio_sound_play_copy(handle, volume); break;
    }
    if (!ok) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
    return 1;
}

static void execute_resize_statement(const char **ptr) {
    const char *saved = *ptr;
    Token t = get_next_token(ptr);
    int allow = 1, scaling = -1;
    if (t.type == TOKEN_OFF) {
        allow = 0;
        scaling = RESIZE_SCALE_NONE;
        saved = *ptr;
        t = get_next_token(ptr);
    } else if (t.type == TOKEN_ON) {
        saved = *ptr;
        t = get_next_token(ptr);
    }
    if (t.type == TOKEN_COMMA && allow) {
        Token method = get_next_token(ptr);
        if (method.type == TOKEN_IDENTIFIER && strcasecmp(method.text, "_STRETCH") == 0) {
            scaling = RESIZE_SCALE_STRETCH;
        } else if (method.type == TOKEN_IDENTIFIER && strcasecmp(method.text, "_SMOOTH") == 0) {
            scaling = RESIZE_SCALE_SMOOTH;
        } else {
            report_runtime_error(ERR_SYNTAX_ERROR);
            return;
        }
    } else if (t.type != TOKEN_EOF && t.type != TOKEN_COLON && t.type != TOKEN_REM) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return;
    } else {
        *ptr = saved;
    }
    graphics_set_resize(allow, scaling);
}

/* DIM SHARED: makes the module variable full (declared as name) visible
 * inside every procedure. */
static void share_with_procedures(const char *full, const char *name) {
    if (global_shared_count >= MAX_GLOBAL_SHARED) return;
    GlobalSharedName *entry = &global_shared_names[global_shared_count++];
    snprintf(entry->full, sizeof(entry->full), "%s", full);
    size_t base_len = 0;
    while (name[base_len] && base_len < sizeof(entry->base) - 1 &&
           !is_type_suffix_char(name[base_len]) && name[base_len] != '~') {
        entry->base[base_len] = (char)toupper((unsigned char)name[base_len]);
        base_len++;
    }
    entry->base[base_len] = '\0';
    for (int f = 0; f < call_stack_depth; f++) {
        clear_resolved_variables(&call_stack[f]);
    }
}

/* CONST name = "text": a string constant is kept as a string variable of
 * that name (shared with every procedure when declared in the module). */
static void define_string_constant(const char *name, const char **ptr) {
    size_t len = strlen(name);
    const ProcedureDef *scope = current_declaration_scope();
    if (len > 0 && name[len - 1] != '$' && !declare_variable_type(name, "$", scope, scope == NULL)) {
        report_runtime_error(ERR_DUPLICATE_DEFINITION);
        return;
    }
    char value[BASIC_STRING_MAX] = "";
    parse_string_expression(ptr, value, sizeof(value));
    int idx = find_variable(name);
    if (runtime_error_occurred || idx < 0) return;
    set_string_variable(idx, -1, value);
    if (!scope) share_with_procedures(vars[idx].name, name);
}

/* The type after AS in DIM: a TYPE, a primitive type (with _UNSIGNED), or
 * STRING * length. Returns 0 after reporting a syntax error. */
static int parse_dim_type(const char **ptr, UserType **user_type, const char **suffix, int *fixed_length) {
    *user_type = NULL;
    *suffix = NULL;
    *fixed_length = -1;
    Token type = get_next_token(ptr);
    *user_type = find_user_type(type.text);
    if (*user_type) return 1;
    const char *type_name = type.text;
    const char *unsigned_saved = *ptr;
    if (strcasecmp(type_name, "_UNSIGNED") == 0) {
        Token width = get_next_token(ptr);
        if (unsigned_type_name(type_name, width.text)) type_name = unsigned_type_name(type_name, width.text);
        else *ptr = unsigned_saved;
    }
    *suffix = primitive_type_suffix(type_name);
    if (!*suffix) {
        report_runtime_error(ERR_SYNTAX_ERROR);
        return 0;
    }
    if ((*suffix)[0] == '$') {
        const char *star_saved = *ptr;
        if (get_next_token(ptr).type == TOKEN_STAR) {
            Token length_token = get_next_token(ptr);
            if (length_token.type != TOKEN_NUMBER || length_token.int_val < 0) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return 0;
            }
            *fixed_length = length_token.int_val;
            *suffix = NULL; /* fixed-length strings keep their own storage */
        } else {
            *ptr = star_saved;
        }
    }
    return 1;
}

void interpret_line_at_ptr(const char **ptr_addr, int is_direct, int *last_line_num) {
    const char *ptr = *ptr_addr;
    Token t = get_next_token(&ptr);

    if (t.type == TOKEN_EOF || t.type == TOKEN_COLON) {
        *ptr_addr = ptr;
        return;
    }

    if (is_direct) {
        // $NOPREFIX applies to the program lines after it as they are read.
        if (last_line_num && t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "$NOPREFIX") == 0) {
            lexer_set_noprefix(1);
        }
        if (t.type == TOKEN_NUMBER) {
            if (last_line_num) *last_line_num = t.int_val;
            // Typing a line number alone deletes that line, as in BASICA.
            if (!last_line_num && *skip_whitespace_fast(ptr) == '\0') delete_line(t.int_val);
            else add_line(t.int_val, ptr, current_source_line_number, 1);
            return;
        } else if (last_line_num) {
            *last_line_num += 10;
            add_line(*last_line_num, *ptr_addr, current_source_line_number, 0);
            return;
        } else if (t.type != TOKEN_LIST && t.type != TOKEN_RUN && t.type != TOKEN_NEW &&
                   t.type != TOKEN_CONT) {
            if (!execute_editor_command(*ptr_addr)) execute_direct_line(*ptr_addr);
            *ptr_addr += strlen(*ptr_addr);
            return;
        }
    }

    {
        if (t.type == TOKEN_ON) {
            const char *saved = ptr;
            Token next = get_next_token(&ptr);
            if (next.type == TOKEN_ERR) {
                Token g = get_next_token(&ptr);
                if (g.type == TOKEN_GOTO) {
                    Token target = get_next_token(&ptr);
                    int target_line = resolve_target_line(target);
                    if (target_line != -1) {
                        on_error_goto_line = target_line;
                        *ptr_addr = ptr;
                        return;
                    }
                }
            } else if (next.type == TOKEN_PEN) {
                // ON PEN GOSUB line
                if (get_next_token(&ptr).type != TOKEN_GOSUB) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                int target_line = resolve_target_line(get_next_token(&ptr));
                if (target_line == -1) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                on_pen_line = target_line;
                *ptr_addr = ptr;
                return;
            } else if (next.type == TOKEN_KEY || next.type == TOKEN_TIMER || next.type == TOKEN_STRIG) {
                Token lparen = get_next_token(&ptr);
                double n_val = 0, n_val2 = 0;
                int timer_two_args = 0;
                if (lparen.type == TOKEN_LPAREN) {
                    n_val = evaluate_expression(&ptr);
                    const char *saved_comma = ptr;
                    Token maybe_comma = get_next_token(&ptr);
                    if (next.type == TOKEN_TIMER && maybe_comma.type == TOKEN_COMMA) {
                        // ON TIMER(t, n) GOSUB: t from _FREETIMER, n seconds.
                        n_val2 = evaluate_expression(&ptr);
                        timer_two_args = 1;
                    } else {
                        ptr = saved_comma;
                    }
                    Token rparen = get_next_token(&ptr);
                    if (rparen.type != TOKEN_RPAREN) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        return;
                    }
                } else {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }

                Token gosub_tok = get_next_token(&ptr);
                if (gosub_tok.type != TOKEN_GOSUB) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                
                Token target_tok = get_next_token(&ptr);
                int target_line = resolve_target_line(target_tok);
                if (target_line == -1) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                if (next.type == TOKEN_KEY) {
                    int k = (int)n_val;
                    if (k >= 1 && k < 32) {
                        on_key_line[k] = target_line;
                    } else {
                        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                        return;
                    }
                } else if (next.type == TOKEN_TIMER) {
                    int slot = 0;
                    double interval = n_val;
                    if (timer_two_args) {
                        slot = (int)n_val;
                        interval = n_val2;
                    }
                    if (!arm_timer(slot, interval, target_line)) return;
                } else if (next.type == TOKEN_STRIG) {
                    int s = (int)n_val;
                    if (s >= 0 && s < 8) {
                        on_strig_line[s] = target_line;
                    } else {
                        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                        return;
                    }
                }
                *ptr_addr = ptr;
                return;
            }
            ptr = saved;
        } else if (t.type == TOKEN_KEY) {
            const char *saved_key = ptr;
            Token first = get_next_token(&ptr);
            if (first.type == TOKEN_LPAREN) {
                int k = evaluate_int_text(&ptr);
                Token rparen = get_next_token(&ptr);
                if (rparen.type == TOKEN_RPAREN) {
                    Token mode = get_next_token(&ptr);
                    if (mode.type == TOKEN_ON) {
                        if (k >= 1 && k < 32) key_state[k] = 1;
                        *ptr_addr = ptr;
                        return;
                    } else if (mode.type == TOKEN_OFF) {
                        if (k >= 1 && k < 32) {
                            key_state[k] = 0;
                            if (key_event_pending[k]) key_event_pending_count--;
                            key_event_pending[k] = 0;
                        }
                        *ptr_addr = ptr;
                        return;
                    } else if (mode.type == TOKEN_STOP) {
                        if (k >= 1 && k < 32) key_state[k] = 2;
                        *ptr_addr = ptr;
                        return;
                    }
                }
            } else if (first.type == TOKEN_ON || first.type == TOKEN_OFF || (first.type == TOKEN_STOP)) {
                *ptr_addr = ptr;
                return;
            }
            ptr = saved_key;
        } else if (t.type == TOKEN_TIMER) {
            // TIMER ON|OFF|STOP, or TIMER(t) ON|OFF|STOP|FREE for a timer
            // number from _FREETIMER: runs, holds or releases that trap.
            int slot = 0;
            const char *saved_paren = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                slot = evaluate_int_text(&ptr);
                if (get_next_token(&ptr).type != TOKEN_RPAREN) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
            } else {
                ptr = saved_paren;
            }
            set_timer_state(slot, get_next_token(&ptr));
            *ptr_addr = ptr;
            return;
        } else if (t.type == TOKEN_PEN) {
            // PEN ON | OFF | STOP: enables, disables or holds ON PEN trapping.
            Token mode = get_next_token(&ptr);
            if (mode.type == TOKEN_ON) pen_state = 1;
            else if (mode.type == TOKEN_OFF) {
                pen_state = 0;
                if (pen_event_pending) device_event_pending_count--;
                pen_event_pending = 0;
            }
            else if (mode.type == TOKEN_STOP) pen_state = 2;
            else report_runtime_error(ERR_SYNTAX_ERROR);
            *ptr_addr = ptr;
            return;
        } else if (t.type == TOKEN_STRIG) {
            Token mode = get_next_token(&ptr);
            if (mode.type == TOKEN_ON) {
                strig_state = 1;
                *ptr_addr = ptr;
                return;
            } else if (mode.type == TOKEN_OFF) {
                strig_state = 0;
                for (int i = 0; i < 8; i++) {
                    if (strig_event_pending[i]) device_event_pending_count--;
                    strig_event_pending[i] = 0;
                }
                *ptr_addr = ptr;
                return;
            } else if (mode.type == TOKEN_STOP) {
                strig_state = 2;
                *ptr_addr = ptr;
                return;
            }
        }
        
        if (t.type == TOKEN_PRINT) {
            int fnum = -1;
            const char *check_ptr = ptr;
            Token first = get_next_token(&check_ptr);
            if (first.type == TOKEN_HASH) {
                ptr = check_ptr;
                fnum = evaluate_int_text(&ptr);
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return;
                }
                const char *comma_ptr = ptr;
                Token sep = get_next_token(&comma_ptr);
                if (sep.type == TOKEN_COMMA) ptr = comma_ptr;
            }
            
            int has_args = 0;
            int using_mode = 0;
            char using_fmt[256] = "";
            size_t using_pos = 0;
            /* detect PRINT USING format: either 'PRINT USING "fmt", expr' or 'PRINT #n, USING "fmt", expr' */
            const char *using_check = ptr;
            Token using_tok = get_next_token(&using_check);
            if (using_tok.type == TOKEN_USING) {
                using_mode = 1;
                ptr = using_check; /* advance past USING */
                parse_string_expression(&ptr, using_fmt, sizeof(using_fmt));

                /* skip optional comma or semicolon after format string */
                const char *sep_check_ptr = ptr;
                Token sep_tok = get_next_token(&sep_check_ptr);
                if (sep_tok.type == TOKEN_COMMA || sep_tok.type == TOKEN_SEMICOLON) {
                    ptr = sep_check_ptr;
                }
            }
            while (1) {
                const char *item_saved = ptr;
                Token next = get_next_token(&ptr);

                if (next.type == TOKEN_EOF || next.type == TOKEN_COLON || next.type == TOKEN_ELSE) {
                    // ELSE ends a single-line IF's THEN branch; leave it for the IF.
                    if (next.type == TOKEN_ELSE) ptr = item_saved;
                    if (!has_args) {
                        ptr = item_saved;
                        if (fnum != -1 && file_handles[fnum]) fprintf(file_handles[fnum], "\n");
                        else basic_output("\n");
                    } else if (using_mode) {
                        // Ended with ; or , : still print the text after the last field.
                        char tail[512] = "";
                        using_trailing_text(using_fmt, &using_pos, tail, sizeof(tail));
                        if (fnum != -1 && file_handles[fnum]) {
                            fprintf(file_handles[fnum], "%s", tail);
                            print_col += (int)strlen(tail);
                        } else {
                            basic_output(tail);
                        }
                    }
                    break;
                }
                has_args = 1;
                char val_buf[1024] = "";
                if (is_string_token(&next) || is_string_member_reference_text(item_saved)) {
                    ptr = item_saved;
                    char value[BASIC_STRING_MAX] = "";
                    parse_string_expression(&ptr, value, sizeof(value));
                    if (using_mode) using_format_value(using_fmt, &using_pos, 1, value, 0, val_buf, sizeof(val_buf));
                    else strcpy(val_buf, value);
                } else {
                    ptr = item_saved;
                    last_expression_is_double = 0;
                    Num number = evaluate_num_text(&ptr);
                    if (runtime_error_occurred) break; // the failed item is not printed
                    double val = num_to_double(number);

                    if (using_mode) using_format_value(using_fmt, &using_pos, 0, "", val, val_buf, sizeof(val_buf));
                    else {
                        char digits[64];
                        format_number_plain(number, last_expression_is_double, digits, sizeof(digits));
                        snprintf(val_buf, sizeof(val_buf), "%s%s ", digits[0] == '-' ? "" : " ", digits);
                    }
                }
                if (runtime_error_occurred) break; // a PRINT USING value that does not fit its field
                if (fnum != -1 && file_handles[fnum]) {
                    fprintf(file_handles[fnum], "%s", val_buf);
                    for (int k = 0; val_buf[k]; k++) {
                        if (val_buf[k] == '\n' || val_buf[k] == '\r') { print_col = 0; print_row++; }
                        else print_col++;
                    }
                } else {
                    basic_output(val_buf);
                }

                // Check for separators
                const char *sep_saved = ptr;
                next = get_next_token(&ptr);
                if (next.type == TOKEN_SEMICOLON || next.type == TOKEN_COMMA) {
                    if (next.type == TOKEN_COMMA && !using_mode) {
                        int target = (print_col / 14 + 1) * 14;
                        while (print_col < target) {
                            if (fnum != -1 && file_handles[fnum]) {
                                fprintf(file_handles[fnum], " ");
                                print_col++;
                            } else {
                                basic_output(" ");
                            }
                        }
                    }
                } else {
                    ptr = sep_saved;
                    char tail[512] = "";
                    if (using_mode) using_trailing_text(using_fmt, &using_pos, tail, sizeof(tail));
                    if (fnum != -1 && file_handles[fnum]) {
                        fprintf(file_handles[fnum], "%s\n", tail);
                        print_col = 0;
                        print_row++;
                    } else {
                        basic_output(tail);
                        basic_output("\n");
                    }
                    break;
                }
            }
        } else if (t.type == TOKEN_IDENTIFIER && t.text[0] == '$') {
            // Metacommands such as $RESIZE take effect before the program runs.
            if (strcasecmp(t.text, "$RESIZE") == 0 && !apply_resize_metacommand(*ptr_addr, 1)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            ptr += strlen(ptr);
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_RESIZE") == 0) {
            execute_resize_statement(&ptr);
        } else if (t.type == TOKEN_IDENTIFIER && (strcasecmp(t.text, "_SCREENHIDE") == 0 ||
                   strcasecmp(t.text, "_SCREENSHOW") == 0 || strcasecmp(t.text, "_SCREENICON") == 0)) {
            // _SCREENHIDE, _SCREENSHOW and _SCREENICON (minimize)
            if (!graphics_is_active()) init_graphics();
            if (strcasecmp(t.text, "_SCREENICON") == 0) graphics_window_minimize();
            else graphics_set_screen_hidden(strcasecmp(t.text, "_SCREENHIDE") == 0);
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_SCREENMOVE") == 0) {
            // _SCREENMOVE x, y  or  _SCREENMOVE _MIDDLE
            if (!graphics_is_active()) init_graphics();
            const char *saved_move = ptr;
            Token first = get_next_token(&ptr);
            if (first.type == TOKEN_IDENTIFIER && strcasecmp(first.text, "_MIDDLE") == 0) {
                int w = 0, h = 0;
                w = graphics_desktop_size(0);
                h = graphics_desktop_size(1);
                graphics_window_move_middle(w, h);
            } else {
                ptr = saved_move;
                int x = evaluate_int_text(&ptr);
                if (!next_graphics_comma(&ptr)) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                int y = evaluate_int_text(&ptr);
                if (!runtime_error_occurred) graphics_window_move(x, y);
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_SAVEIMAGE") == 0) {
            // _SAVEIMAGE file$[, handle][, format$]
            char file[1024] = "", format[32] = "";
            parse_string_expression(&ptr, file, sizeof(file));
            int handle = 0;
            if (!runtime_error_occurred && next_graphics_comma(&ptr)) {
                if (graphics_arg_present(&ptr)) handle = evaluate_int_text(&ptr);
                if (next_graphics_comma(&ptr)) parse_string_expression(&ptr, format, sizeof(format));
            }
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                if (!file[0] || !graphics_save_image(file, handle, format, NULL)) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                }
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_SETALPHA") == 0) {
            // _SETALPHA alpha[, color1[ TO color2]][, handle]
            int alpha = evaluate_int_text(&ptr);
            unsigned int low = 0, high = 0;
            int has_range = 0, handle = graphics_is_active() ? graphics_get_dest_handle() : 0;
            if (next_graphics_comma(&ptr)) {
                if (graphics_arg_present(&ptr)) {
                    low = high = color_from_value(evaluate_expression(&ptr));
                    has_range = 1;
                    if (peek_token_type(ptr) == TOKEN_TO) {
                        get_next_token(&ptr);
                        high = color_from_value(evaluate_expression(&ptr));
                    }
                }
                if (next_graphics_comma(&ptr)) handle = evaluate_int_text(&ptr);
            }
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                if (!graphics_set_alpha(alpha, has_range, low, high, handle)) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                }
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_CLEARCOLOR") == 0) {
            // _CLEARCOLOR color[, handle]
            unsigned int color = color_from_value(evaluate_expression(&ptr));
            int handle = graphics_is_active() ? graphics_get_dest_handle() : 0;
            if (next_graphics_comma(&ptr)) handle = evaluate_int_text(&ptr);
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                if (!graphics_clear_color(color, handle)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_PALETTECOLOR") == 0) {
            // _PALETTECOLOR index, color32[, handle]
            int index = evaluate_int_text(&ptr);
            if (!next_graphics_comma(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            unsigned int color = color_from_value(evaluate_expression(&ptr));
            int handle = graphics_is_active() ? graphics_get_dest_handle() : 0;
            if (next_graphics_comma(&ptr)) handle = evaluate_int_text(&ptr);
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                if (!graphics_set_palette_color(index, color, handle)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_IDENTIFIER && (strcasecmp(t.text, "_MOUSEHIDE") == 0 ||
                   strcasecmp(t.text, "_MOUSESHOW") == 0 || strcasecmp(t.text, "_MOUSECURSOR") == 0)) {
            // _MOUSEHIDE; _MOUSESHOW [style$]; _MOUSECURSOR style$
            int hide = strcasecmp(t.text, "_MOUSEHIDE") == 0;
            int cursor_only = strcasecmp(t.text, "_MOUSECURSOR") == 0;
            char style[64] = "";
            const char *after = skip_whitespace_fast(ptr);
            if (!hide && *after && *after != ':' && *after != '\'') {
                parse_string_expression(&ptr, style, sizeof(style));
            } else if (cursor_only) {
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                if (style[0] && !graphics_mouse_cursor(style)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                else if (!cursor_only) graphics_mouse_visible(!hide);
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_MOUSEMOVE") == 0) {
            // _MOUSEMOVE x, y
            int x = evaluate_int_text(&ptr);
            if (!next_graphics_comma(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int y = evaluate_int_text(&ptr);
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                graphics_mouse_move(x, y);
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_KEYCLEAR") == 0) {
            // _KEYCLEAR: drop the waiting key presses
            if (graphics_is_active()) graphics_keyclear();
            else while (get_stdin_char()) {}
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_CLIPBOARD$") == 0) {
            // _CLIPBOARD$ = text$
            if (get_next_token(&ptr).type != TOKEN_EQUALS) {
                report_runtime_error(ERR_SYNTAX_ERROR);
            } else {
                BasicString text = {0};
                parse_string_expression_heap(&ptr, &text);
                if (!runtime_error_occurred) {
                    basic_string_append(&text, "", 1);
                    graphics_clipboard_set((const char *)text.data);
                }
                basic_string_release(&text);
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_WRITEFILE") == 0) {
            // _WRITEFILE file$, contents$
            BasicString name = {0}, contents = {0};
            parse_string_expression_heap(&ptr, &name);
            if (!runtime_error_occurred && get_next_token(&ptr).type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
            if (!runtime_error_occurred) parse_string_expression_heap(&ptr, &contents);
            if (!runtime_error_occurred) {
                int err = qb64_write_file(&name, &contents);
                if (err) report_runtime_error(err);
            }
            basic_string_release(&name);
            basic_string_release(&contents);
        } else if (t.type == TOKEN_IDENTIFIER && execute_sound_statement(t.text, &ptr)) {
            // done
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_FULLSCREEN") == 0) {
            // _FULLSCREEN [{_STRETCH|_SQUAREPIXELS|_OFF}][, _SMOOTH]
            int mode = FULLSCREEN_STRETCH, smooth = 0;
            const char *saved = ptr;
            Token option = get_next_token(&ptr);
            if (option.type == TOKEN_OFF || (option.type == TOKEN_IDENTIFIER && strcasecmp(option.text, "_OFF") == 0)) {
                mode = FULLSCREEN_OFF;
            } else if (option.type == TOKEN_IDENTIFIER && strcasecmp(option.text, "_STRETCH") == 0) {
                mode = FULLSCREEN_STRETCH;
            } else if (option.type == TOKEN_IDENTIFIER && strcasecmp(option.text, "_SQUAREPIXELS") == 0) {
                mode = FULLSCREEN_SQUAREPIXELS;
            } else {
                ptr = saved;
            }
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                Token smoothing = get_next_token(&ptr);
                if (smoothing.type != TOKEN_IDENTIFIER || strcasecmp(smoothing.text, "_SMOOTH") != 0) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                smooth = 1;
            } else {
                ptr = saved;
            }
            if (!graphics_statement_end(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (!graphics_is_active()) init_graphics();
            graphics_set_fullscreen(mode, smooth);
        } else if (t.type == TOKEN_IDENTIFIER && (strcasecmp(t.text, "_CONTROLCHR") == 0 ||
                                                  strcasecmp(t.text, "_BLINK") == 0)) {
            // _CONTROLCHR {ON|OFF} and _BLINK {ON|OFF}
            Token state = get_next_token(&ptr);
            if (state.type != TOKEN_ON && state.type != TOKEN_OFF) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (strcasecmp(t.text, "_BLINK") == 0) graphics_set_blink(state.type == TOKEN_ON);
            else graphics_set_controlchr(state.type == TOKEN_ON);
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_MAPUNICODE") == 0) {
            // _MAPUNICODE unicode& TO ascii%
            int unicode = evaluate_int_text(&ptr);
            if (get_next_token(&ptr).type != TOKEN_TO) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int ascii = evaluate_int_text(&ptr);
            if (!runtime_error_occurred && !graphics_mapunicode_set(unicode, ascii)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_UPRINTSTRING") == 0) {
            // _UPRINTSTRING (x, y), text$[, maxWidth][, encoding][, font][, image]
            if (get_next_token(&ptr).type != TOKEN_LPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
            int px = evaluate_int_text(&ptr);
            if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
            int py = evaluate_int_text(&ptr);
            if (get_next_token(&ptr).type != TOKEN_RPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
            if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
            BasicString text = {0};
            parse_string_expression_heap(&ptr, &text);
            // Optional: maxWidth, encoding, font handle, image handle.
            int opts[4] = {0, 0, -1, -2};
            int given[4] = {0};
            for (int i = 0; i < 4 && !runtime_error_occurred; i++) {
                const char *saved = ptr;
                if (get_next_token(&ptr).type != TOKEN_COMMA) { ptr = saved; break; }
                const char *peek = ptr;
                Token next = get_next_token(&peek);
                if (next.type == TOKEN_COMMA) continue;   // argument left out
                opts[i] = evaluate_int_text(&ptr);
                given[i] = 1;
            }
            if (!runtime_error_occurred) {
                if (!graphics_is_active()) init_graphics();
                int image = given[3] ? opts[3] : graphics_get_dest_handle();
                if (graphics_uprintstring(px, py, (const unsigned char *)(text.data ? text.data : (const unsigned char *)""),
                                          text.length, opts[0], opts[1], opts[2], image)) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                }
            }
            basic_string_release(&text);
        } else if (t.type == TOKEN_IDENTIFIER && strcasecmp(t.text, "_PRINTMODE") == 0) {
            // _PRINTMODE {_KEEPBACKGROUND|_ONLYBACKGROUND|_FILLBACKGROUND}[, handle]
            Token mode_tok = get_next_token(&ptr);
            int mode = 0;
            if (mode_tok.type == TOKEN_IDENTIFIER) {
                if (strcasecmp(mode_tok.text, "_KEEPBACKGROUND") == 0) mode = PRINTMODE_KEEP;
                else if (strcasecmp(mode_tok.text, "_ONLYBACKGROUND") == 0) mode = PRINTMODE_ONLY;
                else if (strcasecmp(mode_tok.text, "_FILLBACKGROUND") == 0) mode = PRINTMODE_FILL;
            }
            if (!mode) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (!graphics_is_active()) init_graphics();
            int handle = graphics_get_dest_handle();
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) handle = evaluate_int_text(&ptr);
            else ptr = saved;
            if (!graphics_printmode_for(handle, mode)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        } else if (t.type == TOKEN_LET || t.type == TOKEN_IDENTIFIER) {
            Token var_token = (t.type == TOKEN_LET) ? get_next_token(&ptr) : t;
            execute_assignment(&ptr, var_token);
        } else if (t.type == TOKEN_LIST && is_direct) {
            // LIST [first][-[last]]
            int first = 0, last = 0x7FFFFFFF;
            const char *saved = ptr;
            Token from = get_next_token(&ptr);
            if (from.type == TOKEN_NUMBER) {
                first = last = from.int_val;
                saved = ptr;
                from = get_next_token(&ptr);
            }
            if (from.type == TOKEN_MINUS) {
                last = 0x7FFFFFFF;
                saved = ptr;
                Token to = get_next_token(&ptr);
                if (to.type == TOKEN_NUMBER) last = to.int_val;
                else ptr = saved;
            } else {
                ptr = saved;
            }
            list_program_range(first, last);
        } else if (t.type == TOKEN_CONT && is_direct) {
            if (!cont_valid || cont_generation != program_edit_generation()) {
                current_executing_line = DIRECT_LINE_NUMBER;
                report_runtime_error(ERR_CANT_CONTINUE);
            } else {
                stop_running = 0;
                break_requested = 0;
                runtime_error_occurred = 0;
                execute_statements(cont_stmt, cont_resume_ptr, cont_resume_ts_pos);
            }
        } else if (t.type == TOKEN_WAIT) {
            int port = evaluate_int_text(&ptr);
            int and_mask = 0, xor_mask = 0;
            if (get_next_token(&ptr).type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            and_mask = evaluate_int_text(&ptr);
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) xor_mask = evaluate_int_text(&ptr);
            else ptr = saved;
            if (port < 0 || port > 65535 || and_mask < 0 || and_mask > 255 || xor_mask < 0 || xor_mask > 255) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                return;
            }
            execute_wait(port, and_mask, xor_mask);
        } else if (t.type == TOKEN_LOCK || t.type == TOKEN_UNLOCK) {
            execute_lock(&ptr, t.type == TOKEN_LOCK);
        } else if (t.type == TOKEN_COMMON) {
            execute_common(&ptr);
        } else if (t.type == TOKEN_CHAIN) {
            execute_chain(&ptr);
        } else if (t.type == TOKEN_DEST || t.type == TOKEN_SOURCE) {
            // _DEST handle / _SOURCE handle (0 is the screen)
            if (!graphics_is_active()) init_graphics();
            int handle = evaluate_int_text(&ptr);
            int ok = t.type == TOKEN_DEST ? graphics_set_dest(handle) : graphics_set_source(handle);
            if (!ok) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
        } else if (t.type == TOKEN_TITLE) {
            // _TITLE text$ sets the window title.
            char title[256] = "";
            parse_string_expression(&ptr, title, sizeof(title));
            set_window_title(title);
        } else if (t.type == TOKEN_PCOPY) {
            // PCOPY source_page, destination_page
            int source = evaluate_int_text(&ptr);
            if (!next_graphics_comma(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int destination = evaluate_int_text(&ptr);
            if (!graphics_is_active() || !graphics_copy_page(source, destination)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_PALETTE) {
            // PALETTE  |  PALETTE attribute, color  |  PALETTE USING array(start)
            if (!graphics_is_active()) init_graphics();
            if (graphics_statement_end(&ptr)) {
                graphics_reset_palette();
            } else if (peek_token_type(ptr) == TOKEN_USING) {
                get_next_token(&ptr);
                Token array_tok = get_next_token(&ptr);
                if (array_tok.type != TOKEN_IDENTIFIER) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                int idx = find_variable(array_tok.text);
                int start = parse_array_index(&ptr, idx);
                Variable *array = get_variable_ptr(idx);
                int size = graphics_palette_size();
                if (!array->array || start < 0 || start + size > array->array_size) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    return;
                }
                int attributes[256];
                long values[256];
                for (int i = 0; i < size; i++) {
                    attributes[i] = i;
                    values[i] = (long)load_double(array, start + i);
                }
                if (!graphics_set_palette(size, attributes, values)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            } else {
                int attribute = evaluate_int_text(&ptr);
                if (!next_graphics_comma(&ptr)) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                long value = (long)nearbyint(evaluate_expression(&ptr));
                if (!graphics_set_palette(1, &attribute, &value)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_CLEAR) {
            // CLEAR [, [memory][, stack]]: the size arguments are accepted and ignored.
            if (call_stack_depth > 0) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                return;
            }
            while (!graphics_statement_end(&ptr)) {
                Token skip = get_next_token(&ptr);
                if (skip.type == TOKEN_EOF) break;
            }
            clear_statement();
        } else if (t.type == TOKEN_WIDTH) {
            // WIDTH [columns][, rows]  |  WIDTH #n, width  |  WIDTH "device", width
            const char *saved = ptr;
            Token first = get_next_token(&ptr);
            if (first.type == TOKEN_HASH || first.type == TOKEN_STRING ||
                (first.type == TOKEN_IDENTIFIER && strcasecmp(first.text, "LPRINT") == 0)) {
                // File and device widths are accepted; output is not wrapped.
                if (first.type == TOKEN_HASH) {
                    int fnum = evaluate_int_text(&ptr);
                    if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                        report_runtime_error(ERR_BAD_FILE_NUMBER);
                        return;
                    }
                    next_graphics_comma(&ptr);
                } else if (first.type == TOKEN_STRING) {
                    next_graphics_comma(&ptr);
                }
                int width = evaluate_int_text(&ptr);
                if (width < 1 || width > 255) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            } else {
                ptr = saved;
                int columns = graphics_get_text_cols(), rows = graphics_get_text_rows();
                if (graphics_arg_present(&ptr)) columns = evaluate_int_text(&ptr);
                if (next_graphics_comma(&ptr)) rows = evaluate_int_text(&ptr);
                if ((columns != 40 && columns != 80) ||
                    (rows != 25 && rows != 30 && rows != 43 && rows != 50 && rows != 60)) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    return;
                }
                if (graphics_is_active()) {
                    graphics_set_text_size(columns, rows);
                    print_col = 0;
                    print_row = 0;
                }
            }
        } else if (t.type == TOKEN_STOP) {
            // STOP reached outside the main run loop (inside a FUNCTION); the break
            // is reported when the calling statement finishes.
            break_requested = 1;
            stop_running = 1;
        } else if (t.type == TOKEN_RUN && is_direct) {
            // RUN [line]
            const char *saved = ptr;
            Token start = get_next_token(&ptr);
            if (start.type == TOKEN_NUMBER) {
                Statement *start_stmt = find_line(start.int_val);
                if (!start_stmt) {
                    current_executing_line = DIRECT_LINE_NUMBER;
                    report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
                } else {
                    run_program_from(start_stmt);
                }
            } else {
                ptr = saved;
                run_program();
            }
        } else if (t.type == TOKEN_NEW && is_direct) {
            clear_program();
            clear_variables(0); // Full reset for NEW
            graphics_release_program_resources();
            clear_data_pointer();
            print_col = 0;
            print_row = 0;
        } else if (t.type == TOKEN_BEEP) {
            if (audio_is_enabled()) basic_output("\a");
        } else if (t.type == TOKEN_SOUND) {
            double frequency = evaluate_expression(&ptr);
            Token sep = get_next_token(&ptr);
            if (sep.type == TOKEN_COMMA) {
                double duration = evaluate_expression(&ptr);
                if (!isfinite(frequency) || !isfinite(duration) ||
                    frequency < 37 || frequency > 32767 ||
                    duration < 0 || duration > 65535) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                } else {
                    audio_sound(frequency, duration);
                }
            }
        } else if (t.type == TOKEN_PLAY) {
            char mml[256] = "";
            parse_string_expression(&ptr, mml, sizeof(mml));
            audio_play(mml);
        } else if (t.type == TOKEN_DATA) {
            while (*ptr && *ptr != ':') ptr++;
        } else if (t.type == TOKEN_READ) {
            parse_read_list(&ptr);
        } else if (t.type == TOKEN_RESTORE) {
            parse_restore(&ptr);
        } else if (t.type == TOKEN_RANDOMIZE) {
            parse_randomize(&ptr);
        } else if (t.type == TOKEN_CONST) {
            // CONST name = value[, name = value...]
            while (1) {
                Token name_tok = get_next_token(&ptr);
                if (name_tok.type != TOKEN_IDENTIFIER) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                const char *eq_saved = ptr;
                Token eq = get_next_token(&ptr);
                if (eq.type != TOKEN_EQUALS) {
                    ptr = eq_saved;
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                const char *value_ptr = ptr;
                Token first_value = get_next_token(&value_ptr);
                size_t name_len = strlen(name_tok.text);
                if ((name_len > 0 && name_tok.text[name_len - 1] == '$') || is_string_token(&first_value)) {
                    define_string_constant(name_tok.text, &ptr);
                } else {
                    last_expression_is_double = 0;
                    // CONST X% = ... has the suffix's type; otherwise the expression's.
                    Num value = num_as_name_type(evaluate_num_text(&ptr), name_tok.text);
                    if (!runtime_error_occurred && !add_named_constant(name_tok.text, value, last_expression_is_double)) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                    }
                }
                if (runtime_error_occurred) break;
                const char *comma_saved = ptr;
                if (get_next_token(&ptr).type != TOKEN_COMMA) {
                    ptr = comma_saved;
                    break;
                }
            }
            *ptr_addr = ptr;
            return;
        } else if (t.type == TOKEN_DEF) {
            Token fn_tok = get_next_token(&ptr);
            if (fn_tok.type == TOKEN_IDENTIFIER && strncasecmp(fn_tok.text, "FN", 2) == 0) {
                get_next_token(&ptr); // (
                Token param = get_next_token(&ptr);
                get_next_token(&ptr); // )
                get_next_token(&ptr); // =
                
                int f_idx = -1;
                for (int i = 0; i < user_function_count; i++) {
                    if (strcasecmp(user_functions[i].name, fn_tok.text) == 0) { f_idx = i; break; }
                }
                if (f_idx == -1 && user_function_count < 64) f_idx = user_function_count++;
                if (f_idx != -1) {
                    strncpy(user_functions[f_idx].name, fn_tok.text, 31);
                    strncpy(user_functions[f_idx].param_name, param.text, 31);
                    int len = 0;
                    while (ptr[len] && ptr[len] != ':') len++;
                    if (len > 255) len = 255;
                    strncpy(user_functions[f_idx].expression, ptr, len);
                    user_functions[f_idx].expression[len] = '\0';
                    compile_user_function_expression(&user_functions[f_idx]);
                    ptr += len;
                }
            }
        } else if (t.type == TOKEN_DEFINT) {
            parse_def_range(&ptr, '%');
        } else if (t.type == TOKEN_DEFLNG) {
            parse_def_range(&ptr, '&');
        } else if (t.type == TOKEN_DEFSTR) {
            parse_def_range(&ptr, '$');
        } else if (t.type == TOKEN_DEFSNG) {
            parse_def_range(&ptr, '!');
        } else if (t.type == TOKEN_DEFDBL) {
            parse_def_range(&ptr, '#');
        } else if (t.type == TOKEN_FILES) {
            char pattern[256] = "*.bas";
            char redirect_file[256] = "";
            const char *saved = ptr;
            Token pat_tok = get_next_token(&ptr);
            if (pat_tok.type == TOKEN_STRING) {
                strncpy(pattern, pat_tok.text, sizeof(pattern)-1);
                const char *comma_saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_COMMA) {
                    parse_string_expression(&ptr, redirect_file, sizeof(redirect_file));
                } else {
                    ptr = comma_saved;
                }
            } else {
                ptr = saved;
            }
            glob_t results; // Use glob.h for wildcard matching
            int res = glob(pattern, GLOB_NOCHECK, NULL, &results); // GLOB_NOCHECK returns pattern if no match
            if (res == 0) {
                FILE *out = NULL;
                if (redirect_file[0]) out = fopen(redirect_file, "w");
                for (size_t i = 0; i < results.gl_pathc; i++) {
                    struct stat st;
                    char datestr[64] = "";
                    if (stat(results.gl_pathv[i], &st) == 0) {
                        struct tm *tm = localtime(&st.st_mtime);
                        if (tm) strftime(datestr, sizeof(datestr), "%m-%d-%y  %I:%M%p", tm); // two spaces for BASICA-like format
                    }
                    if (out) {
                        fprintf(out, "%s\n", results.gl_pathv[i]);
                    } else {
                        char entry[512];
                        if (S_ISDIR(st.st_mode)) {
                            snprintf(entry, sizeof(entry), "%s %13s %s\n", datestr[0] ? datestr : "", "<DIR>", results.gl_pathv[i]);
                        } else {
                            long long fsize = 0;
                            if (stat(results.gl_pathv[i], &st) == 0) fsize = (long long)st.st_size;
                            snprintf(entry, sizeof(entry), "%s %13lld %s\n", datestr[0] ? datestr : "", fsize, results.gl_pathv[i]);
                        }
                        basic_output(entry);
                    }
                }
                if (out) fclose(out);
                globfree(&results);
            } else {
                basic_output("No files found or error.\n");
            }
        } else if (t.type == TOKEN_CHDIR) {
            char path[256] = "";
            parse_string_expression(&ptr, path, sizeof(path));
            if (chdir(path) != 0) report_runtime_error(ERR_PATH_NOT_FOUND);
        } else if (t.type == TOKEN_MKDIR) {
            char path[256] = "";
            parse_string_expression(&ptr, path, sizeof(path));
            if (mkdir(path, 0777) != 0) report_runtime_error(ERR_PATH_FILE_ACCESS_ERROR);
        } else if (t.type == TOKEN_RMDIR) {
            char path[256] = "";
            parse_string_expression(&ptr, path, sizeof(path));
            if (rmdir(path) != 0) report_runtime_error(ERR_PATH_FILE_ACCESS_ERROR);
        } else if (t.type == TOKEN_SHELL) {
            char cmd[256] = "";
            int hide = 0, dont_wait = 0;
            for (;;) {
                const char *saved_opt = ptr;
                Token opt = get_next_token(&ptr);
                if (opt.type == TOKEN_IDENTIFIER && strcasecmp(opt.text, "_HIDE") == 0) {
                    hide = 1;
                } else if (opt.type == TOKEN_IDENTIFIER && strcasecmp(opt.text, "_DONTWAIT") == 0) {
                    dont_wait = 1;
                } else {
                    ptr = saved_opt;
                    break;
                }
            }
            const char *saved = ptr;
            Token cmd_tok = get_next_token(&ptr);
            if (cmd_tok.type != TOKEN_EOF && cmd_tok.type != TOKEN_COLON) {
                ptr = saved;
                parse_string_expression(&ptr, cmd, sizeof(cmd));
                if (cmd[0] != '\0') run_shell_command(cmd, hide, dont_wait);
            } else if (hide || dont_wait) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            } else {
                const char *shell_cmd = getenv("SHELL");
                fflush(stdout);
                system(shell_cmd ? shell_cmd : "sh");
            }
        } else if (t.type == TOKEN_ENVIRON) {
            char cmd[256] = "";
            parse_string_expression(&ptr, cmd, sizeof(cmd));
            // ENVIRON "NAME=text"; an empty text removes NAME. setenv keeps
            // its own copy, so nothing leaks as values are replaced.
            char *eq = strchr(cmd, '=');
            if (eq && eq != cmd) {
                *eq = '\0';
                if (eq[1]) setenv(cmd, eq + 1, 1);
                else unsetenv(cmd);
            } else if (cmd[0]) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_POKE) {
            int addr = evaluate_int_text(&ptr);
            const char *saved_comma = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                int val = evaluate_int_text(&ptr);
                // Ensure address is within bounds before poking
                if (addr >= 0 && addr < 65536) basika_memory[addr] = (unsigned char)(val & 0xFF);
            } else {
                ptr = saved_comma;
            }
        } else if (t.type == TOKEN_SYSTEM || t.type == TOKEN_QUIT) {
            // SYSTEM [code]: ends the program and the interpreter, without waiting
            // for a key; QB64's optional code is the process's exit status.
            int code = 0;
            if (!graphics_statement_end(&ptr)) code = evaluate_int_text(&ptr);
            fflush(NULL);
            exit(code & 0xFF);
        } else if (t.type == TOKEN_REM || t.type == TOKEN_APOSTROPHE) { // APOSTROPHE is also a comment
            while (*ptr) ptr++;
        } else if (t.type == TOKEN_SWAP) {
            Token v1_tok = get_next_token(&ptr);
            int idx1 = find_variable(v1_tok.text);
            int a_idx1 = parse_array_index(&ptr, idx1);
            
            get_next_token(&ptr); // consume comma

            Token v2_tok = get_next_token(&ptr);
            int idx2 = find_variable(v2_tok.text);
            int a_idx2 = parse_array_index(&ptr, idx2);

            int is_str1 = is_string_var(v1_tok.text);
            int is_str2 = is_string_var(v2_tok.text);

            // Procedure locals and parameters resolve through the call frame.
            Variable *first = get_variable_ptr(idx1);
            Variable *second = get_variable_ptr(idx2);
            if (is_str1 != is_str2 || (!is_str1 && var_type(first) != var_type(second))) {
                report_runtime_error(ERR_TYPE_MISMATCH);
            }
            if (runtime_error_occurred) return;

            if (a_idx1 == -1 && a_idx2 == -1) {
                // Swapping entire variables/arrays
                int64_t tmp_val = first->ivalue; first->ivalue = second->ivalue; second->ivalue = tmp_val;
                BasicString *tmp_sval = first->s_value; first->s_value = second->s_value; second->s_value = tmp_sval;
                double *tmp_arr = first->array; first->array = second->array; second->array = tmp_arr;
                BasicString **tmp_sarr = first->s_array; first->s_array = second->s_array; second->s_array = tmp_sarr;
                int tmp_size = first->array_size; first->array_size = second->array_size; second->array_size = tmp_size;
                int tmp_dims = first->num_dims; first->num_dims = second->num_dims; second->num_dims = tmp_dims;
                for (int i = 0; i < 3; i++) {
                    int td = first->dims[i]; first->dims[i] = second->dims[i]; second->dims[i] = td;
                    int tl = first->lower_bounds[i]; first->lower_bounds[i] = second->lower_bounds[i]; second->lower_bounds[i] = tl;
                }
            } else {
                // Element swapping
                if (is_str1) {
                    char s1[BASIC_STRING_MAX], s2[BASIC_STRING_MAX];
                    get_string_variable_value(idx1, a_idx1, s1, sizeof(s1));
                    get_string_variable_value(idx2, a_idx2, s2, sizeof(s2));
                    set_string_variable(idx1, a_idx1, s2);
                    set_string_variable(idx2, a_idx2, s1);
                } else {
                    int element1 = (a_idx1 >= 0 && first->array) ? a_idx1 : -1;
                    int element2 = (a_idx2 >= 0 && second->array) ? a_idx2 : -1;
                    Num val1 = load_num(first, element1);
                    Num val2 = load_num(second, element2);
                    store_num(first, element1, val2);
                    store_num(second, element2, val1);
                }
            }
        } else if (t.type == TOKEN_OPEN) {
            // OPEN file$ [FOR mode] [ACCESS ...] [lock] AS [#]n [LEN = reclen]
            // or the GW-BASIC form OPEN mode$, [#]n, file$[, reclen]
            char first[256] = "";
            char path_buf[256] = "";
            parse_string_expression(&ptr, first, sizeof(first));
            int mode = FILE_MODE_RANDOM;
            int reclen = 128;
            int fnum = 0;
            const char *saved = ptr;
            Token sep = get_next_token(&ptr);
            if (sep.type == TOKEN_COMMA) {
                switch (toupper((unsigned char)first[0])) {
                    case 'I': mode = FILE_MODE_INPUT; break;
                    case 'O': mode = FILE_MODE_OUTPUT; break;
                    case 'A': mode = FILE_MODE_APPEND; break;
                    case 'B': mode = FILE_MODE_BINARY; break;
                    case 'R': mode = FILE_MODE_RANDOM; break;
                    default: report_runtime_error(ERR_BAD_FILE_MODE); return;
                }
                const char *hash_saved = ptr;
                if (get_next_token(&ptr).type != TOKEN_HASH) ptr = hash_saved;
                fnum = evaluate_int_text(&ptr);
                if (get_next_token(&ptr).type != TOKEN_COMMA) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                parse_string_expression(&ptr, path_buf, sizeof(path_buf));
                saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_COMMA) reclen = evaluate_int_text(&ptr);
                else ptr = saved;
            } else {
                snprintf(path_buf, sizeof(path_buf), "%s", first);
                if (sep.type == TOKEN_FOR) {
                    Token m = get_next_token(&ptr);
                    if (m.type == TOKEN_INPUT) mode = FILE_MODE_INPUT;
                    else if (strcasecmp(m.text, "OUTPUT") == 0) mode = FILE_MODE_OUTPUT;
                    else if (strcasecmp(m.text, "APPEND") == 0) mode = FILE_MODE_APPEND;
                    else if (strcasecmp(m.text, "BINARY") == 0) mode = FILE_MODE_BINARY;
                    else if (strcasecmp(m.text, "RANDOM") == 0 || strcasecmp(m.text, "RWB") == 0 ||
                             strcasecmp(m.text, "RW") == 0) mode = FILE_MODE_RANDOM;
                    else {
                        report_runtime_error(ERR_BAD_FILE_MODE);
                        return;
                    }
                } else {
                    ptr = saved;
                }
                // ACCESS and LOCK clauses are accepted; files are not shared.
                Token as_tok = get_next_token(&ptr);
                while (as_tok.type != TOKEN_AS && as_tok.type != TOKEN_EOF && as_tok.type != TOKEN_COLON) {
                    as_tok = get_next_token(&ptr);
                }
                if (as_tok.type != TOKEN_AS) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                const char *hash_saved = ptr;
                if (get_next_token(&ptr).type != TOKEN_HASH) ptr = hash_saved;
                fnum = evaluate_int_text(&ptr);
                if (runtime_error_occurred) return;
                saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_LEN && get_next_token(&ptr).type == TOKEN_EQUALS) {
                    reclen = evaluate_int_text(&ptr);
                } else {
                    ptr = saved;
                }
            }
            if (fnum < 1 || fnum >= 16) {
                report_runtime_error(ERR_BAD_FILE_NUMBER);
                return;
            }
            if (file_handles[fnum]) {
                report_runtime_error(ERR_FILE_ALREADY_OPEN);
                return;
            }
            if (reclen < 1 || reclen > 32767) {
                report_runtime_error(ERR_BAD_RECORD_LENGTH);
                return;
            }
            const char *mode_str = "r";
            if (mode == FILE_MODE_OUTPUT) mode_str = "w";
            else if (mode == FILE_MODE_APPEND) mode_str = "a";
            else if (mode == FILE_MODE_RANDOM || mode == FILE_MODE_BINARY) mode_str = "r+b";
            reset_file_field_state(fnum);
            file_handles[fnum] = fopen(path_buf, mode_str);
            // RANDOM and BINARY create the file when it does not exist yet.
            if (!file_handles[fnum] && (mode == FILE_MODE_RANDOM || mode == FILE_MODE_BINARY)) {
                file_handles[fnum] = fopen(path_buf, "w+b");
            }
            if (!file_handles[fnum]) {
                report_runtime_error(ERR_FILE_NOT_FOUND);
                return;
            }
            file_mode[fnum] = mode;
            file_record_length[fnum] = reclen;
            file_last_record[fnum] = 0;
        } else if (t.type == TOKEN_FIELD) {
            Token hash = get_next_token(&ptr);
            if (hash.type != TOKEN_HASH) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int fnum = evaluate_int_text(&ptr);
            if (get_next_token(&ptr).type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                report_runtime_error(ERR_BAD_FILE_NUMBER);
                return;
            }

            FieldBinding temp_bindings[64];
            int binding_count = 0;
            int total_len = 0;

            while (1) {
                if (binding_count >= 64) {
                    report_runtime_error(ERR_FIELD_OVERFLOW);
                    return;
                }

                int seg_len = evaluate_int_text(&ptr);
                if (seg_len < 0) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    return;
                }

                const char *saved_seg = ptr;
                Token maybe_as = get_next_token(&ptr);
                if (maybe_as.type != TOKEN_AS) {
                    ptr = saved_seg;
                }

                Token var = get_next_token(&ptr);
                if (var.type != TOKEN_IDENTIFIER || !is_string_var(var.text)) {
                    report_runtime_error(ERR_TYPE_MISMATCH);
                    return;
                }
                int idx = find_variable(var.text);
                int array_idx = parse_array_index(&ptr, idx);

                temp_bindings[binding_count].var_idx = idx;
                temp_bindings[binding_count].array_idx = array_idx;
                temp_bindings[binding_count].offset = total_len;
                temp_bindings[binding_count].len = seg_len;
                binding_count++;
                total_len += seg_len;

                const char *sep_saved = ptr;
                Token sep = get_next_token(&ptr);
                if (sep.type != TOKEN_COMMA) {
                    ptr = sep_saved;
                    break;
                }
            }

            reset_file_field_state(fnum);
            if (total_len > file_record_length[fnum]) {
                report_runtime_error(ERR_FIELD_OVERFLOW);
                return;
            }
            file_field_state[fnum].buffer = malloc((size_t)total_len + 1);
            if (!file_field_state[fnum].buffer) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return;
            }
            file_field_state[fnum].size = total_len;
            file_field_state[fnum].binding_count = binding_count;
            memset(file_field_state[fnum].buffer, ' ', (size_t)total_len);
            file_field_state[fnum].buffer[total_len] = '\0';
            for (int i = 0; i < binding_count; i++) {
                file_field_state[fnum].bindings[i] = temp_bindings[i];
            }
        } else if (t.type == TOKEN_LSET || t.type == TOKEN_RSET) {
            Token var = get_next_token(&ptr);
            if (var.type != TOKEN_IDENTIFIER || !is_string_var(var.text)) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return;
            }
            int idx = find_variable(var.text);
            int array_idx = parse_array_index(&ptr, idx);
            if (get_next_token(&ptr).type != TOKEN_EQUALS) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            char value[BASIC_STRING_MAX] = "";
            parse_string_expression(&ptr, value, sizeof(value));
            set_string_variable_with_align(idx, array_idx, value, t.type == TOKEN_RSET);
        } else if (t.type == TOKEN_CLOSE) {
            // CLOSE [[#]n[, [#]n ...]]; with no file numbers every file is closed.
            if (graphics_statement_end(&ptr)) {
                for (int fnum = 1; fnum < 16; fnum++) close_file_number(fnum);
            } else {
                do {
                    const char *hash_saved = ptr;
                    if (get_next_token(&ptr).type != TOKEN_HASH) ptr = hash_saved;
                    close_file_number(evaluate_int_text(&ptr));
                } while (next_graphics_comma(&ptr));
            }
        } else if (t.type == TOKEN_KILL) {
            char path_buf[256] = "";
            parse_string_expression(&ptr, path_buf, sizeof(path_buf));
            if (path_buf[0]) {
                glob_t results;
                int glob_result = glob(path_buf, 0, NULL, &results);
                int deleted_any = 0;
                if (glob_result == 0) {
                    for (size_t i = 0; i < results.gl_pathc; i++) {
                        if (remove(results.gl_pathv[i]) == 0) {
                            deleted_any = 1;
                        } else {
                            char err[256];
                            snprintf(err, sizeof(err), "Delete failed: %s\n", results.gl_pathv[i]);
                            basic_output(err);
                        }
                    }
                    globfree(&results);
                }
                if (!deleted_any && glob_result == GLOB_NOMATCH) {
                    char err[256];
                    snprintf(err, sizeof(err), "Delete failed: %s\n", path_buf);
                    basic_output(err);
                }
            }
        } else if (t.type == TOKEN_DRAW) {
            char cmd[1024] = "";
            parse_string_expression(&ptr, cmd, sizeof(cmd));
            const char *c = cmd;
            double x, y;
            get_graphics_cursor(&x, &y);
            unsigned int color = graphics_get_foreground();
            int scale = 4;
            int angle = 0; // 0, 1, 2, 3 -> 0, 90, 180, 270 degrees
            int ta = 0; // Turn Angle in degrees

            while (*c) {
                while (*c && (isspace((unsigned char)*c) || *c == ';' || *c == ',')) c++;
                if (!*c) break;

                int no_draw = 0;
                int return_pos = 0;
                if (toupper((unsigned char)*c) == 'B') { no_draw = 1; c++; }
                if (toupper((unsigned char)*c) == 'N') { return_pos = 1; c++; }
                
                char op = toupper((unsigned char)*c++);
                int orig_x = x, orig_y = y;

                while (*c && (isspace((unsigned char)*c) || *c == ';' || *c == ',')) c++;

                int val = 0;
                if (op != 'M' && (isdigit((unsigned char)*c) || *c == '+' || *c == '-')) {
                    char *end;
                    val = strtol(c, &end, 10);
                    c = end;
                }

                int dx = 0, dy = 0;
                switch (op) {
                    case 'U': dy = -val; break;
                    case 'D': dy = val; break;
                    case 'L': dx = -val; break;
                    case 'R': dx = val; break;
                    case 'E': dx = val; dy = -val; break;
                    case 'F': dx = val; dy = val; break;
                    case 'G': dx = -val; dy = val; break;
                    case 'H': dx = -val; dy = -val; break;
                    case 'M': {
                        int rel = 0;
                        if (*c == '+' || *c == '-') rel = 1;
                        int mx = strtol(c, (char**)&c, 10);
                        if (*c == ',') c++;
                        int my = strtol(c, (char**)&c, 10);
                        if (rel) {
                            dx = mx;
                            dy = my;
                        } else {
                            x = mx;
                            y = my;
                            if (!no_draw) draw_line(orig_x, orig_y, x, y, color, 0, 0xFFFF);
                            if (return_pos) { x = orig_x; y = orig_y; }
                            set_graphics_cursor(x, y);
                            continue;
                        }
                        break;
                    }
                    case 'A': angle = val % 4; break;
                    case 'T': if (toupper((unsigned char)*c) == 'A') { c++; ta = strtol(c, (char**)&c, 10); } break;
                    case 'S': scale = val; break;
                    case 'C': color = (unsigned int)val; break;
                    case 'P': {
                        unsigned int p_color = (unsigned int)val;
                        unsigned int b_color = color;
                        if (*c == ',') { c++; b_color = strtol(c, (char**)&c, 10); }
                        draw_paint(x, y, p_color, b_color);
                        continue;
                    }
                }

                if (dx != 0 || dy != 0) {
                    dx = dx * scale / 4;
                    dy = dy * scale / 4;
                    
                    if (angle == 1) { int t = dx; dx = -dy; dy = t; }
                    else if (angle == 2) { dx = -dx; dy = -dy; }
                    else if (angle == 3) { int t = dx; dx = dy; dy = -t; }

                    if (ta != 0) {
                        double rad = ta * M_PI / 180.0;
                        int nx = (int)(dx * cos(rad) - dy * sin(rad));
                        int ny = (int)(dx * sin(rad) + dy * cos(rad));
                        dx = nx; dy = ny;
                    }

                    x += dx; y += dy;
                    if (!no_draw) draw_line(orig_x, orig_y, x, y, color, 0, 0xFFFF);
                    if (return_pos) { x = orig_x; y = orig_y; }
                    set_graphics_cursor(x, y);
                }
            }
        } else if (t.type == TOKEN_NAME) {
            char old_name[256] = "";
            char new_name[256] = "";
            parse_string_expression(&ptr, old_name, sizeof(old_name));
            Token as_tok = get_next_token(&ptr);
            if (as_tok.type != TOKEN_AS) {
                report_runtime_error(ERR_SYNTAX_ERROR);
            } else {
                parse_string_expression(&ptr, new_name, sizeof(new_name));
                if (rename(old_name, new_name) != 0) {
                    report_runtime_error(ERR_PATH_FILE_ACCESS_ERROR);
                }
            }
        } else if (t.type == TOKEN_DELETE) {
            // DELETE "file"  or DELETE <line_number>
            Token arg = get_next_token(&ptr);
            if (arg.type == TOKEN_STRING) {
                if (remove(arg.text) == 0) {
                    basic_output("Deleted\n");
                } else {
                    char err[256];
                    snprintf(err, sizeof(err), "Delete failed: %s\n", arg.text);
                    basic_output(err);
                }
            } else if (arg.type == TOKEN_NUMBER) {
                delete_line(arg.int_val);
            }
        } else if (t.type == TOKEN_ERASE) {
            // ERASE var or ERASE var$(index)
            Token var = get_next_token(&ptr);
            if (var.type == TOKEN_IDENTIFIER) {
                int idx = find_variable(var.text);
                // Check for array index
                int array_idx = -1;
                const char *check_ptr = ptr;
                if (get_next_token(&check_ptr).type == TOKEN_LPAREN) {
                    array_idx = parse_array_index(&ptr, idx);
                } // No else, array_idx remains -1 if no paren
                Variable *erased = get_variable_ptr(idx);
                if (is_string_var(var.text)) {
                    // erase string variable or string array element
                    if (array_idx >= 0 && erased->s_array && array_idx < erased->array_size) {
                        basic_string_destroy(erased->s_array[array_idx]);
                        erased->s_array[array_idx] = NULL;
                    } else if (erased->s_array) {
                        // ERASE S$ with no index on a string array: release all memory
                        for (int i = 0; i < erased->array_size; i++) {
                            if (erased->s_array[i]) {
                                basic_string_destroy(erased->s_array[i]);
                            }
                        }
                        free(erased->s_array);
                        erased->s_array = NULL;
                        erased->array_size = 0;
                        erased->num_dims = 0;
                    } else if (array_idx == -1) { // Scalar string variable
                        basic_string_destroy(erased->s_value);
                        erased->s_value = NULL;
                    }
                } else {
                    // numeric variable or array
                    if (array_idx >= 0 && erased->array && array_idx < erased->array_size) {
                        erased->array[array_idx] = 0;
                    } else if (erased->array) {
                        // ERASE A with no index: release numeric array memory
                        free(erased->array);
                        erased->array = NULL;
                        erased->array_size = 0;
                        erased->num_dims = 0;
                    } else if (array_idx == -1) { // Scalar numeric variable
                        erased->value = 0;
                    }
                }
            }
            
        } else if (t.type == TOKEN_COLOR) {
            // COLOR [foreground][, background]
            unsigned int fg = 0, bg = 0;
            int has_fg = parse_optional_color(&ptr, &fg);
            int has_bg = 0;
            if (next_graphics_comma(&ptr)) has_bg = parse_optional_color(&ptr, &bg);
            graphics_set_draw_colors(has_fg, fg, has_bg, bg);
            if (has_fg) set_text_color(fg);
        } else if (t.type == TOKEN_INPUT) {
            const char *saved = ptr;
            int suppress_question = 0;
            char prompt_text[BASIC_TOKEN_TEXT_MAX] = "";
            Token prompt_tok = get_next_token(&ptr);
            if (prompt_tok.type == TOKEN_STRING) {
                snprintf(prompt_text, sizeof(prompt_text), "%s", prompt_tok.text);
                Token sep = get_next_token(&ptr);
                if (sep.type == TOKEN_COMMA) {
                    suppress_question = 1;
                } else if (sep.type != TOKEN_SEMICOLON) {
                    ptr = saved; // not a real prompt, rewind
                }
            } else {
                ptr = saved;
            }

            saved = ptr;
            Token hash = get_next_token(&ptr);
            int fnum = -1;
            if (hash.type == TOKEN_HASH) {
                fnum = evaluate_int_text(&ptr);
                const char *comma_ptr = ptr;
                Token sep = get_next_token(&comma_ptr);
                if (sep.type == TOKEN_COMMA) ptr = comma_ptr;
            } else {
                ptr = saved;
            }

            int input_var_count = 0;
            int var_indices[MAX_INPUT_FIELDS];
            int var_array_idx[MAX_INPUT_FIELDS];
            int var_is_str[MAX_INPUT_FIELDS];
            while (input_var_count < MAX_INPUT_FIELDS) {
                const char *var_saved = ptr;
                Token var = get_next_token(&ptr);
                if (var.type != TOKEN_IDENTIFIER) {
                    ptr = var_saved;
                    break;
                }
                var_indices[input_var_count] = find_variable(var.text);
                var_array_idx[input_var_count] = parse_array_index(&ptr, var_indices[input_var_count]);
                var_is_str[input_var_count] = is_string_var(var.text);
                input_var_count++;
                const char *sep_saved = ptr;
                Token sep = get_next_token(&ptr);
                if (sep.type != TOKEN_COMMA && sep.type != TOKEN_SEMICOLON) {
                    ptr = sep_saved;
                    break;
                }
            }
            if (input_var_count == 0) return;

            char line[INPUT_LINE_MAX];
            char (*fields)[INPUT_LINE_MAX] = malloc(sizeof(*fields) * (MAX_INPUT_FIELDS + 1));
            if (!fields) {
                report_runtime_error(ERR_OUT_OF_MEMORY);
                return;
            }
            while (1) {
                if (fnum == -1) {
                    // The prompt is shown again after "Redo from start".
                    if (prompt_text[0]) basic_output(prompt_text);
                    if (!suppress_question) basic_output("? ");
                }
                if (!read_input_line(fnum, line, sizeof(line))) {
                    if (fnum == -1) line[0] = '\0';
                    else break;
                }
                int count = split_input_fields(line, fields, MAX_INPUT_FIELDS);
                int valid = count == input_var_count;
                for (int i = 0; valid && i < input_var_count; i++) {
                    if (!var_is_str[i] && !is_numeric_input(fields[i])) valid = 0;
                }
                // A file supplies values as they come; the keyboard must match exactly.
                if (fnum != -1) {
                    for (int i = 0; i < input_var_count; i++) {
                        const char *value = i < count && count > 0 ? fields[i] : "";
                        assign_input_value(var_indices[i], var_array_idx[i], var_is_str[i], value);
                    }
                    break;
                }
                if (valid || feof(stdin)) {
                    for (int i = 0; i < input_var_count; i++) {
                        const char *value = valid && i < count ? fields[i] : "";
                        assign_input_value(var_indices[i], var_array_idx[i], var_is_str[i], value);
                    }
                    break;
                }
                basic_output("?Redo from start\n");
            }
            free(fields);
        } else if (t.type == TOKEN_REVERSE) {
            Token var = get_next_token(&ptr);
            int idx = find_variable(var.text);
            int array_idx = parse_array_index(&ptr, idx);
            char target[BASIC_STRING_MAX] = "";
            get_string_variable_value(idx, array_idx, target, sizeof(target));
            if (target[0]) {
                size_t len = strlen(target);
                char rev[256] = "";
                if (len >= sizeof(rev)) len = sizeof(rev) - 1;
                for (size_t i = 0; i < len; i++) rev[i] = target[len - 1 - i];
                rev[len] = '\0';
                set_string_variable(idx, array_idx, rev);
            }
        } else if (t.type == TOKEN_OPTION) {
            Token base = get_next_token(&ptr);
            if (base.type == TOKEN_IDENTIFIER && (strcasecmp(base.text, "_EXPLICIT") == 0 ||
                                                  strcasecmp(base.text, "_EXPLICITARRAY") == 0)) {
                // Checked for the whole program before it runs (check_explicit_declarations).
            } else if (base.type != TOKEN_BASE) {
                report_runtime_error(ERR_SYNTAX_ERROR);
            } else {
                Token val_tok = get_next_token(&ptr);
                if (val_tok.type != TOKEN_NUMBER) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                } else {
                    int val = val_tok.int_val;
                    if (val != 0 && val != 1) {
                        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    } else if (option_base_set || arrays_dimensioned) {
                        report_runtime_error(ERR_DUPLICATE_DEFINITION);
                    } else {
                        option_base = val;
                        option_base_set = 1;
                    }
                }
            }
        } else if (t.type == TOKEN_DIM) {
            // DIM [SHARED] name[([lower TO] upper[, ...])] [AS type][, ...]
            int dim_shared = 0;
            const char *shared_saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_SHARED) dim_shared = 1;
            else ptr = shared_saved;
            const ProcedureDef *dim_scope = current_declaration_scope();
            if (dim_scope) dim_shared = 0; /* DIM SHARED is only meaningful at module level */
            // QB64's DIM [SHARED] AS type name, name...: one type for every name.
            UserType *lead_user_type = NULL;
            const char *lead_suffix = NULL;
            int lead_fixed_length = -1;
            const char *as_saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_AS) {
                if (!parse_dim_type(&ptr, &lead_user_type, &lead_suffix, &lead_fixed_length)) {
                    *ptr_addr = ptr;
                    return;
                }
            } else {
                ptr = as_saved;
            }
            while (1) {
                Token var = get_next_token(&ptr);
                if (var.type != TOKEN_IDENTIFIER) break;

                int dims[3] = {0};
                int lowers[3] = {0};
                int num_dims = 0;
                const char *saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                    int closed = 0;
                    while (num_dims < 3) {
                        int first = array_subscript(evaluate_num_text(&ptr));
                        int lower = option_base;
                        int upper = first;
                        const char *to_saved = ptr;
                        if (get_next_token(&ptr).type == TOKEN_TO) {
                            lower = first;
                            upper = array_subscript(evaluate_num_text(&ptr));
                        } else {
                            ptr = to_saved;
                        }
                        lowers[num_dims] = lower;
                        dims[num_dims] = upper;
                        num_dims++;
                        Token sep = get_next_token(&ptr);
                        if (sep.type == TOKEN_RPAREN) { closed = 1; break; }
                        if (sep.type != TOKEN_COMMA) break;
                    }
                    if (!closed) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                } else {
                    ptr = saved;
                }

                UserType *user_type = lead_user_type;
                const char *suffix = lead_suffix;
                int fixed_length = lead_fixed_length;
                saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_AS) {
                    if (!parse_dim_type(&ptr, &user_type, &suffix, &fixed_length)) break;
                } else {
                    ptr = saved;
                }

                size_t var_len = strlen(var.text);
                if (suffix) {
                    if (var_len > 0 && is_type_suffix_char(var.text[var_len - 1])) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    if (!declare_variable_type(var.text, suffix, dim_scope, dim_shared)) {
                        report_runtime_error(ERR_DUPLICATE_DEFINITION);
                        break;
                    }
                }

                char shared_full[64] = "";
                if (user_type) {
                    int record_idx = find_variable_raw_name(var.text);
                    if (!declare_user_type_instance(record_idx, user_type, num_dims, dims, lowers)) break;
                    if (num_dims > 0) arrays_dimensioned = 1;
                    snprintf(shared_full, sizeof(shared_full), "%s", vars[record_idx].name);
                } else {
                    int idx = find_variable(var.text);
                    if (idx < 0) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        break;
                    }
                    if (fixed_length >= 0) {
                        vars[idx].string_declared = 1;
                        vars[idx].string_fixed_length = (size_t)fixed_length;
                        get_variable_ptr(idx)->string_declared = 1;
                        get_variable_ptr(idx)->string_fixed_length = (size_t)fixed_length;
                        fixed_string_declarations_present = 1;
                        if (num_dims == 0) assign_string_variable_value(idx, -1, NULL, 0);
                    }
                    if (num_dims > 0) {
                        // Inside a procedure this is the frame's local array, which is
                        // created afresh on every call.
                        Variable *target = get_variable_ptr(idx);
                        int total_size = 1;
                        int dim_err = 0;
                        for (int i = 0; i < num_dims; i++) {
                            int dim_size = dims[i] - lowers[i] + 1;
                            if (dim_size <= 0) {
                                report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
                                dim_err = 1;
                                break;
                            }
                            total_size *= dim_size;
                        }
                        if (dim_err) break;

                        // STATIC procedures allocate their arrays once and keep them.
                        if (dim_scope && is_static_local(dim_scope, target->name) &&
                            (target->array || target->s_array) && target->array_size == total_size) {
                            snprintf(shared_full, sizeof(shared_full), "%s", vars[idx].name);
                            saved = ptr;
                            if (get_next_token(&ptr).type != TOKEN_COMMA) {
                                ptr = saved;
                                break;
                            }
                            continue;
                        }

                        if (is_string_var(var.text)) {
                            if (target->s_array) {
                                report_runtime_error(ERR_DUPLICATE_DEFINITION);
                                break;
                            }
                            target->s_array = calloc(total_size, sizeof(BasicString *));
                        } else {
                            if (target->array) {
                                report_runtime_error(ERR_DUPLICATE_DEFINITION);
                                break;
                            }
                            target->array = malloc(total_size * sizeof(double));
                            for (int i = 0; i < total_size; i++) target->array[i] = 0;
                        }
                        target->array_size = total_size;
                        target->num_dims = num_dims;
                        for (int i = 0; i < num_dims; i++) {
                            target->dims[i] = dims[i];
                            target->lower_bounds[i] = lowers[i];
                        }
                        arrays_dimensioned = 1;
                    }
                    snprintf(shared_full, sizeof(shared_full), "%s", vars[idx].name);
                }

                if (dim_shared) share_with_procedures(shared_full, var.text);

                // DIM x AS type = value (FreeBASIC and QB64): sets a scalar's first value.
                saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_EQUALS) {
                    int target = find_variable(var.text);
                    if (num_dims > 0 || user_type) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    if (is_string_var(var.text) || fixed_length >= 0) {
                        BasicString value = {0};
                        if (!parse_string_expression_heap(&ptr, &value) && !runtime_error_occurred) {
                            report_runtime_error(ERR_TYPE_MISMATCH);
                        }
                        if (!runtime_error_occurred) assign_string_variable_move(target, -1, &value);
                        basic_string_release(&value);
                    } else {
                        Num value = evaluate_num_text(&ptr);
                        if (!runtime_error_occurred) set_numeric_variable_num(target, -1, value);
                    }
                    if (runtime_error_occurred) break;
                } else {
                    ptr = saved;
                }

                saved = ptr;
                if (get_next_token(&ptr).type != TOKEN_COMMA) {
                    ptr = saved;
                    break;
                }
            }
        } else if (t.type == TOKEN_PSET || t.type == TOKEN_PRESET) {
            // PSET [STEP](x,y)[,color] / PRESET [STEP](x,y)[,color]
            double cx, cy, x, y;
            get_graphics_cursor(&cx, &cy);
            int point = parse_graphics_point(&ptr, cx, cy, &x, &y);
            if (point == 0) {
                // Legacy BASIKA form without parentheses: PSET x, y
                x = evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_COMMA) point = -1;
                else y = evaluate_expression(&ptr);
            }
            if (point < 0) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            unsigned int col = t.type == TOKEN_PSET ? graphics_get_foreground() : graphics_get_background();
            if (next_graphics_comma(&ptr)) parse_optional_color(&ptr, &col);
            if (!graphics_statement_end(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            set_pixel(x, y, col);
            update_graphics();
        } else if (t.type == TOKEN_LINE && peek_token_type(ptr) == TOKEN_INPUT) {
            // LINE INPUT [;] ["prompt";] [#n,] var$ reads a whole line, commas and all.
            get_next_token(&ptr);
            const char *saved = ptr;
            if (get_next_token(&ptr).type != TOKEN_SEMICOLON) ptr = saved;
            saved = ptr;
            Token prompt = get_next_token(&ptr);
            if (prompt.type == TOKEN_STRING) {
                basic_output(prompt.text);
                Token sep = get_next_token(&ptr);
                if (sep.type != TOKEN_SEMICOLON && sep.type != TOKEN_COMMA) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
            } else {
                ptr = saved;
            }
            int fnum = -1;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_HASH) {
                fnum = evaluate_int_text(&ptr);
                if (get_next_token(&ptr).type != TOKEN_COMMA) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
            } else {
                ptr = saved;
            }
            Token var = get_next_token(&ptr);
            if (var.type != TOKEN_IDENTIFIER || !is_string_var(var.text)) {
                report_runtime_error(ERR_TYPE_MISMATCH);
                return;
            }
            int idx = find_variable(var.text);
            int array_idx = parse_array_index(&ptr, idx);
            char line[INPUT_LINE_MAX];
            if (!read_input_line(fnum, line, sizeof(line)) && runtime_error_occurred) return;
            assign_string_variable_value(idx, array_idx, line, strlen(line));
        } else if (t.type == TOKEN_WRITE) {
            // WRITE [#n,] expr[, expr...]: strings quoted, numbers without padding,
            // items separated by commas, then a newline.
            int fnum = -1;
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_HASH) {
                fnum = evaluate_int_text(&ptr);
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return;
                }
                saved = ptr;
                if (get_next_token(&ptr).type != TOKEN_COMMA) ptr = saved;
            } else {
                ptr = saved;
            }
            BasicString out = {0};
            int first = 1;
            while (!graphics_statement_end(&ptr)) {
                if (!first) basic_string_append(&out, ",", 1);
                first = 0;
                const char *peek = ptr;
                Token next = get_next_token(&peek);
                if (is_string_token(&next) || is_string_member_reference_text(ptr)) {
                    BasicString value = {0};
                    parse_string_expression_heap(&ptr, &value);
                    basic_string_append(&out, "\"", 1);
                    if (value.length) basic_string_append(&out, value.data, value.length);
                    basic_string_append(&out, "\"", 1);
                    basic_string_release(&value);
                } else {
                    last_expression_is_double = 0;
                    Num value = evaluate_num_text(&ptr);
                    if (runtime_error_occurred) break;
                    char number[64];
                    format_number_plain(value, last_expression_is_double, number, sizeof(number));
                    basic_string_append(&out, number, strlen(number));
                }
                if (!next_graphics_comma(&ptr)) {
                    const char *semi = ptr;
                    if (get_next_token(&semi).type == TOKEN_SEMICOLON) ptr = semi;
                    else break;
                }
            }
            if (!runtime_error_occurred) {
                basic_string_append(&out, "\n", 1);
                if (fnum != -1) {
                    fwrite(out.data, 1, out.length, file_handles[fnum]);
                } else {
                    basic_output((const char *)out.data);
                }
            }
            basic_string_release(&out);
        } else if (t.type == TOKEN_LINE) {
            // LINE [[STEP](x1,y1)]-[STEP](x2,y2)[,[color][,[B|BF][,style]]]
            double cx, cy, x1, y1, x2, y2;
            get_graphics_cursor(&cx, &cy);
            int first = parse_graphics_point(&ptr, cx, cy, &x1, &y1);
            if (first < 0) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (first == 0) {
                x1 = cx;
                y1 = cy;
            }
            if (get_next_token(&ptr).type != TOKEN_MINUS ||
                parse_graphics_point(&ptr, x1, y1, &x2, &y2) != 1) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            unsigned int col = graphics_get_foreground();
            int box = 0;
            unsigned int style = 0xFFFF;
            if (next_graphics_comma(&ptr)) {
                parse_optional_color(&ptr, &col);
                if (next_graphics_comma(&ptr)) {
                    const char *flag_saved = ptr;
                    Token flag = get_next_token(&ptr);
                    if (flag.type == TOKEN_IDENTIFIER && strcasecmp(flag.text, "BF") == 0) box = 2;
                    else if (flag.type == TOKEN_IDENTIFIER && strcasecmp(flag.text, "B") == 0) box = 1;
                    else if (flag.type == TOKEN_COMMA) ptr = flag_saved;
                    else {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        return;
                    }
                    if (next_graphics_comma(&ptr)) {
                        if (!graphics_arg_present(&ptr)) {
                            report_runtime_error(ERR_SYNTAX_ERROR);
                            return;
                        }
                        style = color_from_value(evaluate_expression(&ptr)) & 0xFFFF;
                    }
                }
            }
            if (!graphics_statement_end(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            draw_line(x1, y1, x2, y2, col, box, style);
        } else if (t.type == TOKEN_CIRCLE) {
            // CIRCLE [STEP](x,y),radius[,[color][,[start][,[end][,aspect]]]]
            double cx, cy, x, y;
            get_graphics_cursor(&cx, &cy);
            if (parse_graphics_point(&ptr, cx, cy, &x, &y) != 1 ||
                get_next_token(&ptr).type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            double radius = evaluate_expression(&ptr);
            unsigned int col = graphics_get_foreground();
            double angles[2] = {0, 0};
            int has_angle[2] = {0, 0};
            double aspect = 0;
            int has_aspect = 0;
            if (next_graphics_comma(&ptr)) {
                parse_optional_color(&ptr, &col);
                for (int a = 0; a < 2 && next_graphics_comma(&ptr); a++) {
                    if (graphics_arg_present(&ptr)) {
                        angles[a] = evaluate_expression(&ptr);
                        has_angle[a] = 1;
                        if (angles[a] < -2 * M_PI - 1e-6 || angles[a] > 2 * M_PI + 1e-6) {
                            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                            return;
                        }
                    }
                    if (a == 1 && next_graphics_comma(&ptr)) {
                        if (!graphics_arg_present(&ptr)) {
                            report_runtime_error(ERR_SYNTAX_ERROR);
                            return;
                        }
                        aspect = evaluate_expression(&ptr);
                        has_aspect = 1;
                    }
                }
            }
            if (!graphics_statement_end(&ptr)) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            draw_circle(x, y, radius, col, has_angle[0], angles[0], has_angle[1], angles[1],
                        has_aspect, aspect);
        } else if (t.type == TOKEN_PAINT) {
            // PAINT [STEP](x,y)[,[color|tile$][,[border][,background$]]]
            double cx, cy, x, y;
            get_graphics_cursor(&cx, &cy);
            if (parse_graphics_point(&ptr, cx, cy, &x, &y) != 1) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            unsigned int col = graphics_get_foreground();
            BasicString tile = {0};
            int has_tile = 0;
            int has_border = 0;
            unsigned int border = 0;
            if (next_graphics_comma(&ptr)) {
                if (graphics_arg_present(&ptr)) {
                    const char *peek = ptr;
                    Token first = get_next_token(&peek);
                    if (is_string_token(&first)) {
                        if (!parse_string_expression_heap(&ptr, &tile)) {
                            basic_string_release(&tile);
                            return;
                        }
                        has_tile = 1;
                    } else {
                        col = color_from_value(evaluate_expression(&ptr));
                    }
                }
                if (next_graphics_comma(&ptr)) {
                    has_border = parse_optional_color(&ptr, &border);
                    if (next_graphics_comma(&ptr)) {
                        // The background tile only matters for scanline fills that stop
                        // on already-painted rows; this fill tracks visited pixels.
                        const char *peek = ptr;
                        Token background = get_next_token(&peek);
                        if (!is_string_token(&background)) {
                            basic_string_release(&tile);
                            report_runtime_error(ERR_TYPE_MISMATCH);
                            return;
                        }
                        BasicString ignored = {0};
                        parse_string_expression_heap(&ptr, &ignored);
                        basic_string_release(&ignored);
                    }
                }
            }
            if (!graphics_statement_end(&ptr)) {
                basic_string_release(&tile);
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (has_tile) {
                if (tile.length == 0 || tile.length > 64) {
                    basic_string_release(&tile);
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    return;
                }
                draw_paint_tile(x, y, tile.data, (int)tile.length,
                                has_border ? border : graphics_get_foreground());
            } else {
                draw_paint(x, y, col, has_border ? border : col);
            }
            basic_string_release(&tile);
        } else if (t.type == TOKEN_SCREEN) {
            const char *saved_screen = ptr;
            Token nxt = get_next_token(&ptr);
            if (nxt.type == TOKEN_NEWIMAGE) {
                Token lparen = get_next_token(&ptr);
                if (lparen.type == TOKEN_LPAREN) {
                    int w = evaluate_int_text(&ptr);
                    Token comma1 = get_next_token(&ptr);
                    if (comma1.type == TOKEN_COMMA) {
                        int h = evaluate_int_text(&ptr);
                        int colors = 256;
                        const char *saved_comma2 = ptr;
                        Token comma2 = get_next_token(&ptr);
                        if (comma2.type == TOKEN_COMMA) {
                            colors = evaluate_int_text(&ptr);
                            Token rparen = get_next_token(&ptr);
                            (void)rparen;
                        } else if (comma2.type == TOKEN_RPAREN) {
                            // Done
                        } else {
                            ptr = saved_comma2;
                        }
                        if (w <= 0 || h <= 0) {
                            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                        } else {
                            init_graphics();
                            set_screen_newimage(w, h, colors);
                            current_screen_mode = -1;
                        }
                    } else {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                    }
                } else {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                }
            } else {
                // SCREEN [mode][, [colorswitch][, [apage][, [vpage]]]]
                ptr = saved_screen;
                init_graphics();
                int mode = current_screen_mode;
                if (graphics_arg_present(&ptr)) mode = evaluate_int_text(&ptr);
                int args[3] = {0, 0, 0};
                int has_arg[3] = {0, 0, 0};
                for (int a = 0; a < 3 && next_graphics_comma(&ptr); a++) {
                    if (graphics_arg_present(&ptr)) {
                        args[a] = evaluate_int_text(&ptr);
                        has_arg[a] = 1;
                    }
                }
                // SCREEN handle shows an image (QB64).
                if (mode < -1) {
                    if (!graphics_screen_from_image(mode)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                    else current_screen_mode = -1;
                    *ptr_addr = ptr;
                    return;
                }
                // Repeating the current mode (to flip pages) keeps the screen.
                if (mode != current_screen_mode) {
                    set_screen_mode(mode);
                    current_screen_mode = mode;
                }
                if (has_arg[1] || has_arg[2]) {
                    int active = has_arg[1] ? args[1] : 0;
                    int visual = has_arg[2] ? args[2] : active;
                    if (!graphics_set_pages(active, visual)) report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                }
            }
        } else if (t.type == TOKEN_WINDOW) {
            const char *saved = ptr;
            Token nxt = get_next_token(&ptr);
            int use_screen = 0;
            if (nxt.type == TOKEN_SCREEN) {
                use_screen = 1;
            } else {
                ptr = saved;
            }
            const char *saved_paren = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                double x1 = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                double y1 = evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
                get_next_token(&ptr); // minus
                get_next_token(&ptr); // LPAREN
                double x2 = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                double y2 = evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
                graphics_set_window(use_screen, x1, y1, x2, y2);
            } else {
                ptr = saved_paren;
                graphics_reset_window();
            }
        } else if (t.type == TOKEN_VIEW) {
            const char *saved = ptr;
            Token nxt = get_next_token(&ptr);
            int use_screen = 0;
            if (nxt.type == TOKEN_SCREEN) {
                use_screen = 1;
                saved = ptr;
                nxt = get_next_token(&ptr);
            }
            
            if (nxt.type == TOKEN_LPAREN) {
                int x1 = evaluate_int_text(&ptr);
                get_next_token(&ptr); // comma
                int y1 = evaluate_int_text(&ptr);
                get_next_token(&ptr); // RPAREN
                get_next_token(&ptr); // minus
                get_next_token(&ptr); // LPAREN
                int x2 = evaluate_int_text(&ptr);
                get_next_token(&ptr); // comma
                int y2 = evaluate_int_text(&ptr);
                get_next_token(&ptr); // RPAREN
                unsigned int col = 0, bound = 0;
                int has_col = 0, has_bound = 0;
                if (next_graphics_comma(&ptr)) {
                    has_col = parse_optional_color(&ptr, &col);
                    if (next_graphics_comma(&ptr)) has_bound = parse_optional_color(&ptr, &bound);
                }
                graphics_set_view(use_screen, x1, y1, x2, y2, has_col, col, has_bound, bound);
            } else {
                ptr = saved;
                // Maybe VIEW PRINT?
                if (nxt.type == TOKEN_PRINT) {
                    // skip VIEW PRINT for now
                } else {
                    graphics_reset_view();
                }
            } 
        } else if (t.type == TOKEN_LOCATE) {
            int row = evaluate_int_text(&ptr);
            int col = print_col + 1;
            const char *saved_comma = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                col = evaluate_int_text(&ptr);
            } else {
                ptr = saved_comma;
            }
            int max_r = graphics_get_text_rows();
            int max_c = graphics_get_text_cols();
            if (row < 1 || row > max_r || col < 1 || col > max_c) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            } else {
                set_text_cursor(row, col);
                print_col = col - 1;
            }
        } else if (t.type == TOKEN_CLS) {
            graphics_cls();
            if (graphics_is_active()) graphics_present_if_autodisplay();
            print_col = 0;
            print_row = 0;
        } else if (t.type == TOKEN_SLEEP) {
            // SLEEP [seconds]: a key press ends the wait; no argument waits for a key.
            double seconds = graphics_statement_end(&ptr) ? 0 : evaluate_expression(&ptr);
            if (graphics_is_active()) {
                graphics_sleep_seconds(seconds);
            } else if (seconds > 0) {
                usleep((useconds_t)(seconds * 1000000.0));
            } else {
                while (!stop_running && !get_stdin_char()) usleep(10000);
            }
        } else if (t.type == TOKEN_DELAY) {
            double seconds = evaluate_expression(&ptr);
            if (graphics_is_active()) graphics_delay(seconds);
            else if (seconds > 0) usleep((useconds_t)(seconds * 1000000.0));
        } else if (t.type == TOKEN_LIMIT) {
            double fps = evaluate_expression(&ptr);
            if (graphics_is_active()) {
                graphics_limit(fps);
            } else if (fps > 0) {
                static double next_frame = 0;
                struct timeval now_tv;
                gettimeofday(&now_tv, NULL);
                double now = now_tv.tv_sec + now_tv.tv_usec / 1e6;
                double period = 1.0 / fps;
                if (next_frame == 0 || now > next_frame + period) next_frame = now;
                if (next_frame > now) usleep((useconds_t)((next_frame - now) * 1000000.0));
                next_frame += period;
            }
        }
        else if (t.type == TOKEN_SEEK) {
            const char *saved = ptr;
            Token hash = get_next_token(&ptr);
            int fnum = -1;
            if (hash.type == TOKEN_HASH) {
                fnum = evaluate_int_text(&ptr);
                Token sep = get_next_token(&ptr);
                if (sep.type == TOKEN_COMMA) {
                    double pos = evaluate_expression(&ptr);
                    if (fnum >= 1 && fnum < 16 && file_handles[fnum]) {
                        fseek(file_handles[fnum], (long)pos, SEEK_SET);
                    } else {
                        report_runtime_error(ERR_BAD_FILE_NUMBER);
                    }
                } else {
                    ptr = saved;
                }
            } else {
                ptr = saved;
            }
        }
        else if (t.type == TOKEN_PUT) {
            const char *saved = ptr;
            Token next = get_next_token(&ptr);
            if (next.type == TOKEN_LPAREN) {
                int x = evaluate_int_text(&ptr);
                if (get_next_token(&ptr).type == TOKEN_COMMA) {
                    int y = evaluate_int_text(&ptr);
                    get_next_token(&ptr);
                    get_next_token(&ptr);
                    Token var_tok = get_next_token(&ptr);
                    int idx = find_variable(var_tok.text);
                    Variable *image = idx != -1 ? get_variable_ptr(idx) : NULL;
                    if (image && image->array && image->array_size >= 2) {
                        int w = (int)load_double(image, 0);
                        int h = (int)load_double(image, 1);
                        int action = 0;
                        const char *action_saved = ptr;
                        if (get_next_token(&ptr).type == TOKEN_COMMA) {
                            Token act_tok = get_next_token(&ptr);
                            if (act_tok.type == TOKEN_PSET) action = 1;
                            else if (act_tok.type == TOKEN_PRESET) action = 2;
                            else if (act_tok.type == TOKEN_AND) action = 3;
                            else if (act_tok.type == TOKEN_OR) action = 4;
                            else if (act_tok.type == TOKEN_XOR) action = 0;
                        } else {
                            ptr = action_saved;
                        }
                        int k = 2;
                        for (int j = 0; j < h; j++) {
                            for (int i = 0; i < w; i++) {
                                if (k >= image->array_size) break;
                                unsigned int src_col = color_from_value(load_double(image, k++));
                                unsigned int dst_col = color_from_value(get_pixel(x + i, y + j));
                                unsigned int final_col = src_col;
                                switch (action) {
                                    case 0: final_col = src_col ^ dst_col; break;
                                    case 1: final_col = src_col; break;
                                    case 2: final_col = graphics_is_32bit() ? ~src_col : (~src_col & 0x0F); break;
                                    case 3: final_col = src_col & dst_col; break;
                                    case 4: final_col = src_col | dst_col; break;
                                }
                                // 32-bit results stay opaque so XOR/PRESET never erase alpha.
                                if (graphics_is_32bit()) final_col |= 0xFF000000u;
                                set_pixel(x + i, y + j, final_col);
                            }
                        }
                        if (graphics_is_active()) graphics_present_if_autodisplay();
                    }
                }
            } else if (next.type == TOKEN_HASH) {
                int fnum = evaluate_int_text(&ptr);
                Token sep;
                int has_more = 0;
                double rec = parse_record_number(&ptr, fnum, &has_more);
                {
                    if (!has_more) {
                        if (fnum < 1 || fnum >= 16 || !file_handles[fnum] || !file_field_state[fnum].buffer || file_field_state[fnum].size <= 0) {
                            report_runtime_error(ERR_BAD_FILE_NUMBER);
                        } else {
                            long offset;
                            if (!file_record_offset(fnum, rec, &offset)) return;
                            fseek(file_handles[fnum], offset, SEEK_SET);
                            fwrite(file_field_state[fnum].buffer, 1, file_field_state[fnum].size, file_handles[fnum]);
                            fflush(file_handles[fnum]);
                            file_last_record[fnum] = (long)rec;
                        }
                    } else {
                        const char *argument_start = ptr;
                        Token argument = get_next_token(&ptr);
                        int root_index = argument.type == TOKEN_IDENTIFIER
                            ? find_existing_variable_raw_name(argument.text) : -1;
                        UserTypeInstance record_instance;
                        if (root_index >= 0 &&
                            get_user_type_instance_for_root(root_index, &record_instance)) {
                            int array_index = record_instance.num_dims > 0
                                ? parse_array_index(&ptr, root_index) : -1;
                            transfer_user_type_record(fnum, rec, root_index,
                                                      array_index, 1);
                        } else {
                            // QBasic form: PUT #n, [position,] variable with a
                            // numeric or string variable, array element or
                            // record member; anything else is BASIKA's
                            // len, data$ form.
                            int var_index, array_index;
                            int matched = argument.type == TOKEN_IDENTIFIER
                                ? parse_record_variable_argument(&ptr, argument.text,
                                                                &var_index, &array_index) : 0;
                            if (matched > 0) {
                                transfer_variable_record(fnum, rec, var_index,
                                                         array_index, 1);
                            } else if (matched == 0) {
                                ptr = argument_start;
                                int len = evaluate_int_text(&ptr);
                                sep = get_next_token(&ptr);
                                char data[512] = "";
                                if (sep.type == TOKEN_COMMA) {
                                    const char *expr_saved = ptr;
                                    Token tok = get_next_token(&ptr);
                                    if (tok.type == TOKEN_STRING) {
                                        strncpy(data, tok.text, sizeof(data) - 1);
                                    } else if (tok.type == TOKEN_IDENTIFIER && is_string_var(tok.text)) {
                                        int src_idx = find_variable(tok.text);
                                        int src_array_idx = parse_array_index(&ptr, src_idx);
                                        char temp[BASIC_STRING_MAX] = "";
                                        get_string_variable_value(src_idx, src_array_idx, temp, sizeof(temp));
                                        strncpy(data, temp, sizeof(data) - 1);
                                    } else {
                                        ptr = expr_saved;
                                        parse_string_expression(&ptr, data, sizeof(data));
                                    }
                                    if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                                        report_runtime_error(ERR_BAD_FILE_NUMBER);
                                    } else if (len < 0 || len > BASIC_STRING_MAX) {
                                        report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                                    } else {
                                        long offset = (long)((rec - 1) * len);
                                        fseek(file_handles[fnum], offset, SEEK_SET);
                                        file_last_record[fnum] = (long)rec;
                                        char buf[BASIC_STRING_MAX];
                                        memset(buf, ' ', len);
                                        strncpy(buf, data, len);
                                        fwrite(buf, 1, len, file_handles[fnum]);
                                        fflush(file_handles[fnum]);
                                    }
                                } else {
                                    report_runtime_error(ERR_SYNTAX_ERROR);
                                }
                            }
                        }
                    }
                }
            } else {
                ptr = saved;
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
        }
        else if (t.type == TOKEN_GET) {
            const char *saved = ptr;
            Token next = get_next_token(&ptr);
            if (next.type == TOKEN_LPAREN) {
                int x1 = evaluate_int_text(&ptr);
                get_next_token(&ptr);
                int y1 = evaluate_int_text(&ptr);
                get_next_token(&ptr);
                get_next_token(&ptr);
                get_next_token(&ptr);
                int x2 = evaluate_int_text(&ptr);
                get_next_token(&ptr);
                int y2 = evaluate_int_text(&ptr);
                get_next_token(&ptr);
                get_next_token(&ptr);
                Token var_tok = get_next_token(&ptr);
                int idx = find_variable(var_tok.text);
                int w = abs(x2 - x1) + 1;
                int h = abs(y2 - y1) + 1;
                int min_x = (x1 < x2) ? x1 : x2;
                int min_y = (y1 < y2) ? y1 : y2;
                int total_size = 2 + w * h;
                Variable *image = idx != -1 ? get_variable_ptr(idx) : NULL;
                if (image && image->array && image->array_size >= total_size) {
                    store_double_raw(image, 0, (double)w);
                    store_double_raw(image, 1, (double)h);
                    int k = 2;
                    for (int j = 0; j < h; j++) {
                        for (int i = 0; i < w; i++) {
                            store_double_raw(image, k++, graphics_point(min_x + i, min_y + j));
                        }
                    }
                } else {
                    report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
                }
            } else if (next.type == TOKEN_HASH) {
                int fnum = evaluate_int_text(&ptr);
                Token sep;
                int has_more = 0;
                double rec = parse_record_number(&ptr, fnum, &has_more);
                {
                    if (!has_more) {
                        if (fnum < 1 || fnum >= 16 || !file_handles[fnum] || !file_field_state[fnum].buffer || file_field_state[fnum].size <= 0) {
                            report_runtime_error(ERR_BAD_FILE_NUMBER);
                        } else {
                            long offset;
                            if (!file_record_offset(fnum, rec, &offset)) return;
                            fseek(file_handles[fnum], offset, SEEK_SET);
                            file_last_record[fnum] = (long)rec;
                            size_t r = fread(file_field_state[fnum].buffer, 1, file_field_state[fnum].size, file_handles[fnum]);
                            if (r < (size_t)file_field_state[fnum].size) {
                                for (size_t i = r; i < (size_t)file_field_state[fnum].size; i++) file_field_state[fnum].buffer[i] = ' ';
                            }
                            file_field_state[fnum].buffer[file_field_state[fnum].size] = '\0';
                        }
                    } else {
                        const char *argument_start = ptr;
                        Token argument = get_next_token(&ptr);
                        int root_index = argument.type == TOKEN_IDENTIFIER
                            ? find_existing_variable_raw_name(argument.text) : -1;
                        UserTypeInstance record_instance;
                        if (root_index >= 0 &&
                            get_user_type_instance_for_root(root_index, &record_instance)) {
                            int array_index = record_instance.num_dims > 0
                                ? parse_array_index(&ptr, root_index) : -1;
                            transfer_user_type_record(fnum, rec, root_index,
                                                      array_index, 0);
                        } else {
                            // QBasic form: GET #n, [position,] variable with a
                            // numeric or string variable, array element or
                            // record member; anything else is BASIKA's
                            // len, stringvar form.
                            int var_index, array_index;
                            int matched = argument.type == TOKEN_IDENTIFIER
                                ? parse_record_variable_argument(&ptr, argument.text,
                                                                &var_index, &array_index) : 0;
                            if (matched > 0) {
                                transfer_variable_record(fnum, rec, var_index,
                                                         array_index, 0);
                            } else if (matched == 0) {
                                ptr = argument_start;
                                int len = evaluate_int_text(&ptr);
                                sep = get_next_token(&ptr);
                                if (sep.type != TOKEN_COMMA) {
                                    report_runtime_error(ERR_SYNTAX_ERROR);
                                } else {
                                    Token var = get_next_token(&ptr);
                                    if (!(var.type == TOKEN_IDENTIFIER && is_string_var(var.text))) {
                                        report_runtime_error(ERR_SYNTAX_ERROR);
                                    } else {
                                        int idx = find_variable(var.text);
                                        int array_idx = parse_array_index(&ptr, idx);
                                        if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                                            report_runtime_error(ERR_BAD_FILE_NUMBER);
                                        } else if (len < 0 || len >= BASIC_STRING_MAX) {
                                            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                                        } else {
                                            long offset = (long)((rec - 1) * len);
                                            fseek(file_handles[fnum], offset, SEEK_SET);
                                            file_last_record[fnum] = (long)rec;
                                            char buf[BASIC_STRING_MAX];
                                            size_t r = fread(buf, 1, len, file_handles[fnum]);
                                            if (r < (size_t)len) {
                                                for (size_t i = r; i < (size_t)len; i++) buf[i] = ' ';
                                            }
                                            buf[len] = '\0';
                                            int trim = len - 1;
                                            while (trim >= 0 && buf[trim] == ' ') { buf[trim] = '\0'; trim--; }
                                            set_string_variable(idx, array_idx, buf);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                ptr = saved;
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
        } else if (t.type == TOKEN_AUTODISPLAY) {
            // _AUTODISPLAY (QB64) or _AUTODISPLAY ON resumes automatic presents.
            const char *saved = ptr;
            Token state = get_next_token(&ptr);
            if (state.type == TOKEN_ON) {
                graphics_set_autodisplay(1);
            } else if (state.type == TOKEN_OFF) {
                graphics_set_autodisplay(0);
            } else if (state.type == TOKEN_EOF || state.type == TOKEN_COLON ||
                       state.type == TOKEN_APOSTROPHE || state.type == TOKEN_REM ||
                       state.type == TOKEN_ELSE) {
                ptr = saved;
                graphics_set_autodisplay(1);
            } else {
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
        } else if (t.type == TOKEN_DISPLAY) {
            graphics_display();
        } else if (t.type == TOKEN_FREEIMAGE) {
            int handle = evaluate_int_text(&ptr);
            if (!graphics_freeimage(handle)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_PUTIMAGE) {
            // _PUTIMAGE [(dx1,dy1)[-(dx2,dy2)]][, [source][, [dest]][, (sx1,sy1)[-(sx2,sy2)]]]
            // The older BASIKA form _PUTIMAGE (x,y), source, (sx1,sy1)-(sx2,sy2)
            // (source rectangle in the third place) is also accepted.
            if (!graphics_is_active()) init_graphics();
            double ix1 = 0, iy1 = 0, ix2 = 0, iy2 = 0;
            int has_dest = 0, has_dest2 = 0, has_src = 0, has_src2 = 0;
            int source = graphics_get_source(), destination = graphics_get_dest();
            double sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
            int point = parse_graphics_point(&ptr, 0, 0, &ix1, &iy1);
            if (point < 0) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
            if (point == 1) {
                has_dest = 1;
                const char *saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_MINUS) {
                    if (parse_graphics_point(&ptr, ix1, iy1, &ix2, &iy2) != 1) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                    has_dest2 = 1;
                } else {
                    ptr = saved;
                }
            }
            int slot = 0; // 0 source, 1 destination, 2 source rectangle
            while (next_graphics_comma(&ptr) && slot < 3) {
                const char *probe = ptr;
                double px, py;
                int is_point = graphics_arg_present(&ptr) && parse_graphics_point(&probe, 0, 0, &px, &py) == 1;
                if (slot == 2 || (slot == 1 && is_point)) {
                    if (!is_point) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                    parse_graphics_point(&ptr, 0, 0, &sx1, &sy1);
                    has_src = 1;
                    const char *saved = ptr;
                    if (get_next_token(&ptr).type == TOKEN_MINUS) {
                        if (parse_graphics_point(&ptr, sx1, sy1, &sx2, &sy2) != 1) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                        has_src2 = 1;
                    } else {
                        ptr = saved;
                    }
                    break;
                }
                if (graphics_arg_present(&ptr)) {
                    int handle = evaluate_int_text(&ptr);
                    if (slot == 0) source = handle; else destination = handle;
                }
                slot++;
            }
            if (!graphics_putimage_ex(has_dest, round_to_int(ix1), round_to_int(iy1), has_dest2,
                                      round_to_int(ix2), round_to_int(iy2), source, destination,
                                      has_src, round_to_int(sx1), round_to_int(sy1),
                                      has_src2, round_to_int(sx2), round_to_int(sy2))) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_SCREENSHOT) {
            char filename[256] = "";
            if (!parse_string_expression(&ptr, filename, sizeof(filename))) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            if (!graphics_is_active()) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            } else {
                if (!graphics_save_screenshot(filename)) {
                    report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                }
            }
        } else if (t.type == TOKEN_PRINTSTRING) {
            /* _PRINTSTRING (x, y), text$ */
            Token lparen = get_next_token(&ptr);
            if (lparen.type != TOKEN_LPAREN) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int px = evaluate_int_text(&ptr);
            Token comma1 = get_next_token(&ptr);
            if (comma1.type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int py = evaluate_int_text(&ptr);
            Token rparen = get_next_token(&ptr);
            if (rparen.type != TOKEN_RPAREN) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            Token comma2 = get_next_token(&ptr);
            if (comma2.type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            char text[512] = "";
            parse_string_expression(&ptr, text, sizeof(text));
            if (!graphics_is_active()) {
                init_graphics();
            }
            graphics_printstring(px, py, text);
        } else if (t.type == TOKEN_FONT) {
            /* _FONT handle% */
            int handle = evaluate_int_text(&ptr);
            if (!graphics_is_active()) init_graphics();
            if (!graphics_setfont(handle)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_FREEFONT) {
            /* _FREEFONT handle% */
            int handle = evaluate_int_text(&ptr);
            if (!graphics_freefont(handle)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else {
            // Token was not a recognized command or valid assignment
            report_runtime_error(ERR_SYNTAX_ERROR);
        }
    }
    *ptr_addr = ptr;
}

void interpret_line(const char *input, int is_direct, int *last_line_num, int source_line_number) {
    current_source_line_number = source_line_number;
    const char *ptr = input;
    interpret_line_at_ptr(&ptr, is_direct, last_line_num);
}

static BlockIfFrame block_if_stack[MAX_BLOCK_IF_DEPTH];
static int block_if_depth = 0;
static SelectCaseFrame select_case_stack[MAX_SELECT_CASE_DEPTH];
static int select_case_depth = 0;

/* The main program runs the SUBs it calls inline. Entering one records the
 * stack depths in its frame; leaving it, even from inside a loop, block IF
 * or SELECT CASE (EXIT SUB), drops what the SUB left open. */
static Statement *enter_inline_sub(ProcedureDef *proc, TokenStream *ts, Statement *exec_stmt) {
    int depth = call_stack_depth;
    int bases[5] = {for_ptr, while_ptr, do_ptr, block_if_depth, select_case_depth};
    Statement *start = execute_sub_call(proc, ts, exec_stmt);
    if (call_stack_depth > depth) {
        CallFrame *frame = &call_stack[call_stack_depth - 1];
        frame->for_base = bases[0];
        frame->while_base = bases[1];
        frame->do_base = bases[2];
        frame->block_if_base = bases[3];
        frame->select_base = bases[4];
    }
    return start;
}

static void leave_inline_sub(const CallFrame *frame) {
    if (for_ptr > frame->for_base) for_ptr = frame->for_base;
    if (while_ptr > frame->while_base) while_ptr = frame->while_base;
    if (do_ptr > frame->do_base) do_ptr = frame->do_base;
    if (block_if_depth > frame->block_if_base) block_if_depth = frame->block_if_base;
    release_select_frames(select_case_stack, &select_case_depth, frame->select_base);
}

/* Where CONT resumes after STOP or Ctrl+C, valid until the program is edited. */
static int cont_valid = 0;
static Statement *cont_stmt = NULL;
static const char *cont_resume_ptr = NULL;
static int cont_resume_ts_pos = -1;
static unsigned int cont_generation = 0;
/* Program edit generation at the last procedure/TYPE scan. */
static unsigned int scanned_generation = 0;

static void execute_statements(Statement *curr, const char *resume_ptr, int resume_ts_pos);


/* ---- OPTION _EXPLICIT ----
 * As in QB64, every variable must be declared before the program runs: by
 * DIM, STATIC, COMMON or CONST in its scope, DIM SHARED or COMMON SHARED or a
 * module-level CONST for every scope, SHARED inside a procedure, or as a
 * parameter or the FUNCTION's own name. Names are compared without their
 * type suffix. */
typedef struct {
    char name[64];
    int scope;       /* 0 for the module, otherwise 1 + the procedure's index */
    int everywhere;  /* DIM SHARED, COMMON SHARED or a module-level CONST */
} ExplicitName;

static ExplicitName *explicit_names = NULL;
static int explicit_name_count = 0, explicit_name_capacity = 0;

static void explicit_base_name(const char *name, char *out, size_t size) {
    size_t n = 0;
    for (; name[n] && name[n] != '.' && n + 1 < size; n++) out[n] = (char)toupper((unsigned char)name[n]);
    while (n > 0 && strchr("$%&!#~", out[n - 1])) n--;
    out[n] = '\0';
}

static void explicit_declare(const char *name, int scope, int everywhere) {
    if (explicit_name_count == explicit_name_capacity) {
        int capacity = explicit_name_capacity ? explicit_name_capacity * 2 : 64;
        ExplicitName *grown = realloc(explicit_names, (size_t)capacity * sizeof(*grown));
        if (!grown) return;
        explicit_names = grown;
        explicit_name_capacity = capacity;
    }
    ExplicitName *entry = &explicit_names[explicit_name_count++];
    explicit_base_name(name, entry->name, sizeof(entry->name));
    entry->scope = scope;
    entry->everywhere = everywhere;
}

static int explicit_is_declared(const char *name, int scope) {
    char base[64];
    explicit_base_name(name, base, sizeof(base));
    for (int i = 0; i < explicit_name_count; i++) {
        const ExplicitName *entry = &explicit_names[i];
        if ((entry->scope == scope || entry->everywhere) && strcmp(entry->name, base) == 0) return 1;
    }
    return 0;
}

/* Names that are not variables: built-ins Basika reads as identifiers. */
static int explicit_is_builtin_name(const char *name, TokenType statement) {
    static const char *const always[] = {"ERR", "ERL", "ERDEV", "CSRLIN", "POS", "POINT", "LPRINT", "IS", "FREEFILE", "FREE"};
    for (size_t i = 0; i < sizeof(always) / sizeof(always[0]); i++) {
        if (strcasecmp(name, always[i]) == 0) return 1;
    }
    if (statement == TOKEN_LINE && (strcasecmp(name, "B") == 0 || strcasecmp(name, "BF") == 0)) return 1;
    if (statement == TOKEN_OPEN) {
        static const char *const modes[] = {"OUTPUT", "APPEND", "BINARY", "RANDOM", "RW", "RWB"};
        for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
            if (strcasecmp(name, modes[i]) == 0) return 1;
        }
    }
    return 0;
}

static int explicit_is_declaring_statement(const Token *first) {
    return first->type == TOKEN_DIM || first->type == TOKEN_STATIC || first->type == TOKEN_SHARED ||
           first->type == TOKEN_COMMON || first->type == TOKEN_CONST;
}

/* The statement of a line that begins at token start ends at *end, at a colon
 * or before THEN / ELSE. Returns where the next one begins. */
static int explicit_next_statement(const Statement *stmt, int start, int *end) {
    int depth = 0, i = start;
    for (; i < stmt->token_count; i++) {
        TokenType type = stmt->tokens[i].type;
        if (type == TOKEN_LPAREN) depth++;
        else if (type == TOKEN_RPAREN) depth--;
        else if (depth <= 0 && (type == TOKEN_COLON || (i > start && (type == TOKEN_THEN || type == TOKEN_ELSE)))) break;
    }
    *end = i;
    return i < stmt->token_count && stmt->tokens[i].type == TOKEN_COLON ? i + 1 : i;
}

/* In DIM, STATIC, SHARED, COMMON and CONST, the names at the start of each
 * comma-separated item are declarations, and AS clauses name types. */
static void explicit_scan_declarations(const Statement *stmt, int start, int end, int scope, int check,
                                       const char **undefined) {
    const Token *first = &stmt->tokens[start];
    int everywhere = 0, i = start + 1;
    if (i < end && stmt->tokens[i].type == TOKEN_SHARED) {
        if (scope == 0) everywhere = 1;
        i++;
    }
    if (first->type == TOKEN_CONST && scope == 0) everywhere = 1;
    // QB64's DIM AS type name, name...: skip the leading type.
    if (i < end && stmt->tokens[i].type == TOKEN_AS) {
        i++;
        if (i < end && stmt->tokens[i].type == TOKEN_IDENTIFIER && strcasecmp(stmt->tokens[i].text, "_UNSIGNED") == 0) i++;
        if (i < end) i++; // the type name
        if (i + 1 < end && stmt->tokens[i].type == TOKEN_STAR) i += 2; // STRING * length
    }
    int item_start = 1, depth = 0, in_type = 0;
    for (; i < end; i++) {
        const Token *token = &stmt->tokens[i];
        if (token->type == TOKEN_LPAREN) depth++;
        else if (token->type == TOKEN_RPAREN) depth--;
        if (depth == 0 && token->type == TOKEN_COMMA) {
            item_start = 1;
            in_type = 0;
            continue;
        }
        if (depth == 0 && token->type == TOKEN_AS) {
            in_type = 1;
            continue;
        }
        if (token->type != TOKEN_IDENTIFIER || in_type) {
            if (token->type != TOKEN_LPAREN) item_start = 0;
            continue;
        }
        if (item_start && depth == 0) {
            if (!check) explicit_declare(token->text, scope, everywhere);
        } else if (check && !*undefined && !explicit_is_declared(token->text, scope) &&
                   !find_procedure(token->text) && !qb64_function_id(token->text) &&
                   !qb64_file_function_id(token->text) && !qb64_string_function_id(token->text)) {
            *undefined = token->text;
        }
        item_start = 0;
    }
}

/* Returns 0 (with the error reported at its line) if a variable is used
 * that was never declared. */
static int check_explicit_declarations(int arrays_only) {
    explicit_name_count = 0;
    // Pass 1: every declaration, by scope.
    int scope = 0;
    for (Statement *stmt = get_head(); stmt; stmt = stmt->next) {
        if (stmt->token_count == 0) continue;
        TokenType t0 = stmt->tokens[0].type;
        if (t0 == TOKEN_SUB || t0 == TOKEN_FUNCTION) {
            ProcedureDef *proc = find_procedure_by_header(stmt);
            if (proc) {
                scope = 1 + (int)(proc - registered_procs);
                for (int p = 0; p < proc->param_count; p++) explicit_declare(proc->params[p].name, scope, 0);
                if (proc->is_function) explicit_declare(proc->name, scope, 0);
            }
            continue;
        }
        if (t0 == TOKEN_END && stmt->token_count > 1 &&
            (stmt->tokens[1].type == TOKEN_SUB || stmt->tokens[1].type == TOKEN_FUNCTION)) {
            scope = 0;
            continue;
        }
        if (t0 == TOKEN_DEF && stmt->token_count > 1 && stmt->tokens[1].type == TOKEN_IDENTIFIER) {
            explicit_declare(stmt->tokens[1].text, 0, 1); // DEF FNname
            continue;
        }
        for (int start = 0, end = 0, next; start < stmt->token_count; start = next) {
            next = explicit_next_statement(stmt, start, &end);
            int s = start;
            if (stmt->tokens[s].type == TOKEN_THEN || stmt->tokens[s].type == TOKEN_ELSE) s++;
            if (s < end && explicit_is_declaring_statement(&stmt->tokens[s])) {
                explicit_scan_declarations(stmt, s, end, scope, 0, NULL);
            } else if (s + 2 < end && stmt->tokens[s].type == TOKEN_FOR &&
                       stmt->tokens[s + 1].type == TOKEN_IDENTIFIER && stmt->tokens[s + 2].type == TOKEN_AS) {
                explicit_declare(stmt->tokens[s + 1].text, scope, 0); // FOR i AS type = ...
            }
        }
    }

    // Pass 2: every variable used.
    scope = 0;
    int in_type_block = 0;
    for (Statement *stmt = get_head(); stmt; stmt = stmt->next) {
        if (stmt->token_count == 0) continue;
        const Token *first = &stmt->tokens[0];
        if (first->type == TOKEN_TYPE) { in_type_block = 1; continue; }
        if (first->type == TOKEN_END && stmt->token_count > 1) {
            TokenType what = stmt->tokens[1].type;
            if (what == TOKEN_TYPE) { in_type_block = 0; continue; }
            if (what == TOKEN_SUB || what == TOKEN_FUNCTION) { scope = 0; continue; }
        }
        if (in_type_block) continue;
        if (first->type == TOKEN_SUB || first->type == TOKEN_FUNCTION) {
            ProcedureDef *proc = find_procedure_by_header(stmt);
            if (proc) scope = 1 + (int)(proc - registered_procs);
            continue;
        }
        if (first->type == TOKEN_DECLARE || first->type == TOKEN_DATA || first->type == TOKEN_DEF ||
            first->type == TOKEN_OPTION || first->type == TOKEN_DEFINT || first->type == TOKEN_DEFLNG ||
            first->type == TOKEN_DEFSTR || first->type == TOKEN_DEFSNG || first->type == TOKEN_DEFDBL ||
            (first->type == TOKEN_IDENTIFIER && first->text[0] == '$')) {
            continue;
        }
        const char *undefined = NULL;
        for (int start = 0, end = 0, next; start < stmt->token_count && !undefined; start = next) {
            next = explicit_next_statement(stmt, start, &end);
            int s = start;
            if (stmt->tokens[s].type == TOKEN_THEN || stmt->tokens[s].type == TOKEN_ELSE) s++;
            if (s >= end) continue;
            const Token *statement = &stmt->tokens[s];
            if (explicit_is_declaring_statement(statement)) {
                explicit_scan_declarations(stmt, s, end, scope, 1, &undefined);
                continue;
            }
            int after_as = 0;
            for (int i = s; i < end && !undefined; i++) {
                const Token *token = &stmt->tokens[i];
                if (token->type == TOKEN_AS) { after_as = 1; continue; }
                if (token->type == TOKEN_COMMA) after_as = 0;
                // In FOR i AS type = a TO b, the expressions follow the "=".
                if (token->type == TOKEN_EQUALS && after_as) after_as = 0;
                if (token->type != TOKEN_IDENTIFIER || after_as) continue;
                if (arrays_only && (i + 1 >= end || stmt->tokens[i + 1].type != TOKEN_LPAREN)) continue;
                const char *name = token->text;
                // A label where the line starts ("Name:"), or one named by GOTO and the like.
                if (i == 0 && stmt->label[0]) continue;
                if (name[0] == '_' || find_label(name) || find_procedure(name) ||
                    explicit_is_builtin_name(name, statement->type) ||
                    explicit_is_declared(name, scope)) {
                    continue;
                }
                undefined = name;
            }
        }
        if (undefined) {
            current_executing_line = stmt->line_number;
            current_source_line_number = stmt->source_line_number;
            current_has_explicit_line_number = stmt->has_explicit_line_number;
            explicit_base_name(undefined, undefined_variable_name, sizeof(undefined_variable_name));
            report_runtime_error(ERR_VARIABLE_NOT_DEFINED);
            return 0;
        }
    }
    return 1;
}

/* OPTION _EXPLICIT checks every variable; OPTION _EXPLICITARRAY alone
 * checks only arrays. Returns 2, 1, or 0 when neither is used. */
static int program_explicit_option(void) {
    int option = 0;
    for (Statement *stmt = get_head(); stmt; stmt = stmt->next) {
        if (stmt->token_count > 1 && stmt->tokens[0].type == TOKEN_OPTION &&
            stmt->tokens[1].type == TOKEN_IDENTIFIER) {
            if (strcasecmp(stmt->tokens[1].text, "_EXPLICIT") == 0) return 2;
            if (strcasecmp(stmt->tokens[1].text, "_EXPLICITARRAY") == 0) option = 1;
        }
    }
    return option;
}

static void run_program_from(Statement *start);

void run_program() {
    run_program_from(get_head());
}

/* RUN [line]: clears variables and runs from the start or from that line. */
static void run_program_from(Statement *start) {
    clear_variables(1); // Keep registry for RUN to maintain pre-tokenized indices
    graphics_release_program_resources();
    stop_running = 0;
    runtime_error_occurred = 0;
    if (!scan_user_types() || runtime_error_occurred) {
        return;
    }
    gosub_ptr = 0;
    while_ptr = 0;
    for_ptr = 0;
    do_ptr = 0;
    call_stack_depth = 0;
    frames_being_built = 0;
    scan_procedures();
    scanned_generation = program_edit_generation();
    int explicit_option = program_explicit_option();
    if (explicit_option && !check_explicit_declarations(explicit_option == 1)) return;
    // $RESIZE is a metacommand: it applies wherever it appears in the program.
    int resize_set = 0;
    for (Statement *s = get_head(); s; s = s->next) {
        resize_set |= apply_resize_metacommand(s->raw_command, 0);
    }
    if (!resize_set) graphics_set_resize(0, RESIZE_SCALE_NONE);
    // Open the audio device now if the program makes sound, rather than
    // pausing at its first PLAY or SOUND.
    for (Statement *s = get_head(); s && audio_is_enabled(); s = s->next) {
        int uses_sound = 0;
        for (int i = 0; i < s->token_count && !uses_sound; i++) {
            uses_sound = s->tokens[i].type == TOKEN_PLAY || s->tokens[i].type == TOKEN_SOUND;
        }
        if (uses_sound) {
            audio_prepare();
            break;
        }
    }
    print_col = 0;
    print_row = 0;
    clear_data_pointer();
    Statement *curr = start;
    const char *resume_ptr = NULL;
    int resume_ts_pos = -1;
    error_stmt = NULL;
    error_ptr = NULL;
    error_next_ptr = NULL;

    for (int i = 0; i < MAX_TIMER_SLOTS; i++) {
        timers[i].pending = 0;
        timers[i].event_active = 0;
    }
    for (int i = 0; i < 32; i++) {
        key_event_pending[i] = 0;
        key_event_active[i] = 0;
    }
    key_event_pending_count = 0;
    for (int i = 0; i < 8; i++) {
        strig_event_pending[i] = 0;
        strig_event_active[i] = 0;
    }
    pen_event_pending = 0;
    pen_event_active = 0;
    device_event_pending_count = 0;
    for (int i = 1; i < 16; i++) {
        reset_file_field_state(i);
    }

    block_if_depth = 0;
    release_select_frames(select_case_stack, &select_case_depth, 0);
    common_variable_count = 0;
    execute_statements(curr, resume_ptr, resume_ts_pos);
    if (pending_chain_path[0] && !runtime_error_occurred) {
        char path[512];
        snprintf(path, sizeof(path), "%s", pending_chain_path);
        pending_chain_path[0] = '\0';
        if (load_program_file(path)) {
            run_program_from(get_head());
        } else {
            report_runtime_error(ERR_FILE_NOT_FOUND);
        }
    }
    release_chain_values();
}

/* Records where CONT should resume and reports "Break in N". */
static void note_break(Statement *exec_stmt, const TokenStream *ts, int jumped,
                       Statement *next_stmt, const char *next_ptr, int next_ts_pos) {
    break_requested = 0;
    if (jumped) {
        cont_stmt = next_stmt;
        cont_resume_ptr = next_ptr;
        cont_resume_ts_pos = next_ts_pos;
    } else if (ts->pos < exec_stmt->token_count) {
        cont_stmt = exec_stmt;
        cont_resume_ptr = NULL;
        cont_resume_ts_pos = ts->pos;
    } else {
        cont_stmt = exec_stmt->next;
        cont_resume_ptr = NULL;
        cont_resume_ts_pos = -1;
    }
    // A break inside a direct-mode line cannot be continued.
    cont_valid = exec_stmt->line_number != DIRECT_LINE_NUMBER;
    cont_generation = program_edit_generation();
    char message[64];
    if (exec_stmt->line_number == DIRECT_LINE_NUMBER) {
        snprintf(message, sizeof(message), "Break\n");
    } else {
        int display_line = exec_stmt->has_explicit_line_number || exec_stmt->source_line_number <= 0
            ? exec_stmt->line_number : exec_stmt->source_line_number;
        snprintf(message, sizeof(message), "Break in %d\n", display_line);
    }
    basic_output(message);
}

/* Runs statements from curr (optionally mid-line) until the program ends,
 * stops on an error, or breaks; used by RUN, CONT and direct-mode lines. */
static void execute_statements(Statement *curr, const char *resume_ptr, int resume_ts_pos) {
    int poll_counter = 0;
    while (curr && !stop_running) {

        int stmt_token_count = curr->token_count;
        TokenType stmt_first_type = stmt_token_count > 0 ? curr->tokens[0].type : TOKEN_EOF;
        if (stmt_token_count >= 2 && stmt_first_type == TOKEN_TYPE) {
            Statement *end_type = curr->next;
            while (end_type && !(end_type->token_count >= 2 &&
                   end_type->tokens[0].type == TOKEN_END &&
                   end_type->tokens[1].type == TOKEN_TYPE)) {
                end_type = end_type->next;
            }
            curr = end_type ? end_type->next : NULL;
            resume_ptr = NULL;
            resume_ts_pos = -1;
            continue;
        }

        if (call_stack_depth == 0 &&
            (stmt_first_type == TOKEN_SUB || stmt_first_type == TOKEN_FUNCTION)) {
            ProcedureDef *p = find_procedure_by_header(curr);
            if (p && p->end_stmt) {
                curr = p->end_stmt->next;
                resume_ptr = NULL;
                resume_ts_pos = -1;
                continue;
            }
        }

        if (++poll_counter >= 1000) {
            handle_events();
            if (!graphics_is_active()) fflush(stdout);
            poll_counter = 0;
        }

        Statement *exec_stmt = curr;
        current_executing_line = exec_stmt->line_number;
        current_source_line_number = exec_stmt->source_line_number;
        current_has_explicit_line_number = exec_stmt->has_explicit_line_number;
        // Running any program line ends the chance to CONT an earlier break.
        if (exec_stmt->line_number != DIRECT_LINE_NUMBER) cont_valid = 0;
        
        int start_pos = 0;
        if (resume_ptr || resume_ts_pos >= 0) {
            if (resume_ts_pos >= 0 && resume_ts_pos <= exec_stmt->token_count) {
                start_pos = resume_ts_pos;
            } else {
                while (start_pos < exec_stmt->token_count && exec_stmt->tokens[start_pos].start_ptr < resume_ptr) {
                    start_pos++;
                }
            }
        }
        TokenStream ts = {exec_stmt->tokens, start_pos, exec_stmt};
        resume_ptr = NULL;
        resume_ts_pos = -1;
        
        int jumped = 0;
        while (ts.pos < exec_stmt->token_count && !stop_running && !jumped) {
            Token *current_token = &ts.tokens[ts.pos++];
            const Token *t = current_token;

            switch (t->type) {
            case TOKEN_EOF:
            case TOKEN_COLON:
                continue;

            case TOKEN_IF: {
                Num cond_num;
                if (!evaluate_num_tok_cached(&ts, &cond_num)) cond_num = evaluate_num_tok(&ts);
                /* Truthiness only, so the Num need not become a double. */
                int cond = cond_num.kind < NUM_SINGLE ? cond_num.i != 0 : cond_num.d != 0;
                // Consume THEN or GOTO token
                if (ts.pos < exec_stmt->token_count && (ts.tokens[ts.pos].type == TOKEN_THEN || ts.tokens[ts.pos].type == TOKEN_GOTO)) {
                    ts.pos++;
                }

                if (is_block_if_header(exec_stmt)) {
                    if (block_if_depth >= MAX_BLOCK_IF_DEPTH) {
                        report_runtime_error(ERR_OUT_OF_MEMORY);
                        break;
                    }
                    block_if_stack[block_if_depth++] = (BlockIfFrame){
                        .branch_taken = cond,
                        .else_seen = 0
                    };
                    if (!cond) {
                        Statement *branch = find_next_block_if_clause(exec_stmt, NULL);
                        if (!branch) {
                            report_runtime_error(ERR_SYNTAX_ERROR);
                            break;
                        }
                        curr = branch;
                        resume_ts_pos = 0;
                        jumped = 1;
                        break;
                    }
                    continue;
                }

                if (cond) continue;
                
                // Find ELSE or end of line
                int depth = 0;
                while (ts.pos < exec_stmt->token_count) {
                    if (ts.tokens[ts.pos].type == TOKEN_EOF) break;
                    Token skip = ts.tokens[ts.pos++];
                    if (skip.type == TOKEN_IF) depth++;
                    else if (skip.type == TOKEN_ELSE) {
                        if (depth == 0) break;
                        depth--;
                    }
                }
                continue;
            }

            case TOKEN_ELSEIF: {
                if (!is_block_elseif_header(exec_stmt) || block_if_depth == 0) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                BlockIfFrame *frame = &block_if_stack[block_if_depth - 1];
                if (frame->branch_taken || frame->else_seen) {
                    Statement *end_if = find_block_if_end(exec_stmt, NULL);
                    if (!end_if) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    curr = end_if;
                    resume_ts_pos = 0;
                    jumped = 1;
                    break;
                }
                double cond = evaluate_expression_tok(&ts);
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_THEN) ts.pos++;
                if (cond != 0) {
                    frame->branch_taken = 1;
                    continue;
                }
                Statement *branch = find_next_block_if_clause(exec_stmt, NULL);
                if (!branch) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                curr = branch;
                resume_ts_pos = 0;
                jumped = 1;
                break;
            }

            case TOKEN_ELSE: {
                if (!is_block_else(exec_stmt)) {
                    ts.pos = exec_stmt->token_count;
                    continue;
                }
                if (block_if_depth == 0) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                BlockIfFrame *frame = &block_if_stack[block_if_depth - 1];
                if (frame->branch_taken || frame->else_seen) {
                    Statement *end_if = find_block_if_end(exec_stmt, NULL);
                    if (!end_if) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    curr = end_if;
                    resume_ts_pos = 0;
                    jumped = 1;
                    break;
                }
                frame->branch_taken = 1;
                frame->else_seen = 1;
                continue;
            }

            case TOKEN_SELECT: {
                if (!is_select_case_header(exec_stmt) || ts.pos >= exec_stmt->token_count ||
                    ts.tokens[ts.pos].type != TOKEN_CASE) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                ts.pos++; // consume CASE
                SelectCaseFrame *frame = push_select_frame(select_case_stack, &select_case_depth,
                                                           exec_stmt, ts.pos);
                if (!frame) {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    break;
                }
                frame->is_string = is_string_token(&ts.tokens[ts.pos]);
                if (frame->is_string) {
                    if (!parse_string_expression_tok_heap(&ts, &frame->str_val)) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                } else {
                    frame->num_val = evaluate_expression_tok(&ts);
                }
                continue;
            }

            case TOKEN_CASE: {
                if (select_case_depth == 0) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                SelectCaseFrame *frame = &select_case_stack[select_case_depth - 1];
                if (frame->branch_taken) {
                    Statement *end_sel = find_select_case_end(exec_stmt, NULL);
                    if (!end_sel) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    curr = end_sel;
                    resume_ts_pos = 0;
                    jumped = 1;
                    break;
                }
                if (is_case_else(exec_stmt)) {
                    frame->branch_taken = 1;
                    /* Skip only ELSE so statements after "CASE ELSE:" still run. */
                    if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_ELSE) ts.pos++;
                    continue;
                }
                int matched = evaluate_case_clause_tok(&ts, exec_stmt, frame);
                if (matched) {
                    frame->branch_taken = 1;
                    continue;
                }
                Statement *branch = find_next_case_clause(exec_stmt, NULL);
                if (!branch) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                curr = branch;
                resume_ts_pos = 0;
                jumped = 1;
                break;
            }

            // Handle FOR loop
            case TOKEN_FOR: {
                Token *var_token = &ts.tokens[ts.pos++];
                if (!for_counter_type_tok(&ts, var_token, exec_stmt->token_count)) break;
                Token eq = ts.tokens[ts.pos++];
                if (eq.type != TOKEN_EQUALS) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                Num start = evaluate_num_tok(&ts);
                ts.pos++; // skip TO
                Num end = evaluate_num_tok(&ts);
                Num step = num_i(1, NUM_INTEGER);
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_STEP) {
                    ts.pos++;
                    step = evaluate_num_tok(&ts);
                }
                
                int idx = resolve_token_variable(var_token);
                ForLoop loop;
                int runs = for_loop_begin(&loop, idx, start, end, step);
                if (runs < 0) break;

                // The body is skipped when the start is already past the limit.
                if (!runs) {
                    int end_ts_pos = ts.pos; // Start searching from current position
                    Statement *target_stmt = skip_for_block(exec_stmt, end_ts_pos, &end_ts_pos);
                    ForLoop *repeat = NULL;
                    int finished = target_stmt ? finish_skipped_next(target_stmt, &end_ts_pos, &repeat) : 0;
                    if (finished < 0) break;
                    if (finished > 0) {
                        curr = repeat->start_stmt;
                        resume_ptr = NULL;
                        resume_ts_pos = repeat->start_ts_pos;
                        jumped = 1;
                        break;
                    }

                    if (target_stmt && end_ts_pos < target_stmt->token_count) {
                        curr = target_stmt;
                        resume_ptr = curr->tokens[end_ts_pos].start_ptr;
                    } else if (target_stmt) {
                        curr = target_stmt->next; // a NEXT list ended the line
                        resume_ptr = NULL;
                    } else {
                        report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                    }
                    jumped = 1;
                    break; // Exit the command loop for this line
                }

                if (for_ptr < 16) {
                    for_stack[for_ptr] = loop;
                    for_stack[for_ptr].start_stmt = exec_stmt;
                    for_stack[for_ptr].start_ts_pos = ts.pos;
                    for_stack[for_ptr].block_depth = block_if_depth;
                    for_stack[for_ptr].select_depth = select_case_depth;
                    for_ptr++;
                } else {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                }
                continue;
            }
            
            case TOKEN_WHILE: {
                const char *command_start_ptr = t->start_ptr;
                double val = evaluate_expression_tok(&ts);
                const char *cond_start = command_start_ptr;
                if (val != 0) {
                    // Push only if this WHILE is not already the current active loop
                    if (while_ptr == 0 || while_stack[while_ptr-1].stmt != exec_stmt || while_stack[while_ptr-1].ptr != cond_start) {
                        if (while_ptr < 16) {
                            while_stack[while_ptr].stmt = exec_stmt;
                            while_stack[while_ptr].ptr = cond_start;
                            while_ptr++;
                        } else {
                            report_runtime_error(ERR_OUT_OF_MEMORY);
                        }
                    }
                } else {
                    // Condition is false; pop the loop from stack if we were in it
                    if (while_ptr > 0 && while_stack[while_ptr-1].stmt == exec_stmt && while_stack[while_ptr-1].ptr == cond_start) {
                        while_ptr--;
                    }
                    int end_ts_pos = ts.pos; // Start searching from current position
                    Statement *target_stmt = skip_to_matching_token(exec_stmt, end_ts_pos, TOKEN_WHILE, TOKEN_WEND, &end_ts_pos);

                    if (target_stmt) {
                        curr = target_stmt;
                        if (end_ts_pos < curr->token_count) {
                            resume_ptr = curr->tokens[end_ts_pos].start_ptr;
                        } else {
                            resume_ptr = NULL; // WEND was last token on line, advance to next line
                        }
                    } else {
                        report_runtime_error(ERR_WEND_WITHOUT_WHILE);
                    }
                    jumped = 1;
                    break; // Exit command loop for the WHILE line
                }
                continue;
            }

            // Handle WEND
            case TOKEN_WEND:
                if (while_ptr > 0) {
                    curr = while_stack[while_ptr-1].stmt;
                    resume_ptr = while_stack[while_ptr-1].ptr;
                    jumped = 1;
                    break;
                } else {
                    report_runtime_error(ERR_WEND_WITHOUT_WHILE);
                    break;
                }

            // Handle DO [WHILE|UNTIL cond] ... LOOP [WHILE|UNTIL cond]
            case TOKEN_DO: {
                const char *command_start_ptr = t->start_ptr;
                int has_top_cond = (ts.pos < exec_stmt->token_count &&
                                     (ts.tokens[ts.pos].type == TOKEN_WHILE || ts.tokens[ts.pos].type == TOKEN_UNTIL));
                int should_continue = 1;
                if (has_top_cond) {
                    TokenType cond_type = ts.tokens[ts.pos++].type;
                    double cond_val = evaluate_expression_tok(&ts);
                    should_continue = (cond_type == TOKEN_WHILE) ? (cond_val != 0) : (cond_val == 0);
                }
                if (should_continue) {
                    // Push only if this DO is not already the current active loop
                    if (do_ptr == 0 || do_stack[do_ptr-1].stmt != exec_stmt || do_stack[do_ptr-1].ptr != command_start_ptr) {
                        if (do_ptr < 16) {
                            do_stack[do_ptr].stmt = exec_stmt;
                            do_stack[do_ptr].ptr = command_start_ptr;
                            do_stack[do_ptr].block_depth = block_if_depth;
                            do_stack[do_ptr].select_depth = select_case_depth;
                            do_ptr++;
                        } else {
                            report_runtime_error(ERR_OUT_OF_MEMORY);
                        }
                    }
                } else {
                    // Top condition is false; pop the loop from stack if we were in it
                    if (do_ptr > 0 && do_stack[do_ptr-1].stmt == exec_stmt && do_stack[do_ptr-1].ptr == command_start_ptr) {
                        do_ptr--;
                    }
                    int end_ts_pos = ts.pos; // Start searching from current position
                    Statement *target_stmt = skip_do_block(exec_stmt, end_ts_pos, &end_ts_pos);

                    if (target_stmt) {
                        curr = target_stmt;
                        if (end_ts_pos < curr->token_count) {
                            resume_ptr = curr->tokens[end_ts_pos].start_ptr;
                        } else {
                            resume_ptr = NULL; // LOOP was last token on line, advance to next line
                        }
                    } else {
                        report_runtime_error(ERR_LOOP_WITHOUT_DO);
                    }
                    jumped = 1;
                    break; // Exit command loop for the DO line
                }
                continue;
            }

            // Handle LOOP [WHILE|UNTIL cond]
            case TOKEN_LOOP: {
                if (do_ptr > 0) {
                    int has_bottom_cond = (ts.pos < exec_stmt->token_count &&
                                            (ts.tokens[ts.pos].type == TOKEN_WHILE || ts.tokens[ts.pos].type == TOKEN_UNTIL));
                    int should_repeat = 1;
                    if (has_bottom_cond) {
                        TokenType cond_type = ts.tokens[ts.pos++].type;
                        double cond_val = evaluate_expression_tok(&ts);
                        should_repeat = (cond_type == TOKEN_WHILE) ? (cond_val != 0) : (cond_val == 0);
                    }
                    if (should_repeat) {
                        curr = do_stack[do_ptr-1].stmt;
                        resume_ptr = do_stack[do_ptr-1].ptr;
                        jumped = 1;
                        break;
                    } else {
                        do_ptr--;
                        continue;
                    }
                } else {
                    report_runtime_error(ERR_LOOP_WITHOUT_DO);
                    break;
                }
            }

            // Handle ON GOTO/GOSUB
            case TOKEN_ON: {
                const char *command_start_ptr = t->start_ptr;
                Token next_on = ts.tokens[ts.pos];
                if (next_on.type == TOKEN_ERR || next_on.type == TOKEN_PEN) {
                    const char *temp_ptr = command_start_ptr;
                    interpret_statement_text(exec_stmt, &temp_ptr);
                    // We need to advance ts.pos to match where interpret_line_at_ptr left off
                    while(ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type != TOKEN_COLON && ts.tokens[ts.pos].type != TOKEN_EOF) ts.pos++;
                    continue;
                } else if (next_on.type == TOKEN_KEY || next_on.type == TOKEN_TIMER || next_on.type == TOKEN_STRIG) {
                    ts.pos++;
                    if (ts.pos >= exec_stmt->token_count || ts.tokens[ts.pos].type != TOKEN_LPAREN) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    ts.pos++;
                    double n_val = evaluate_expression_tok(&ts);
                    double n_val2 = 0;
                    int timer_two_args = 0;
                    if (next_on.type == TOKEN_TIMER && ts.pos < exec_stmt->token_count &&
                        ts.tokens[ts.pos].type == TOKEN_COMMA) {
                        // ON TIMER(t, n) GOSUB: t from _FREETIMER, n seconds.
                        ts.pos++;
                        n_val2 = evaluate_expression_tok(&ts);
                        timer_two_args = 1;
                    }
                    if (ts.pos >= exec_stmt->token_count || ts.tokens[ts.pos].type != TOKEN_RPAREN) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    ts.pos++;
                    if (ts.pos >= exec_stmt->token_count || ts.tokens[ts.pos].type != TOKEN_GOSUB) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    ts.pos++;
                    if (ts.pos >= exec_stmt->token_count) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    int target_line = resolve_target_line(ts.tokens[ts.pos++]);
                    if (target_line == -1) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    if (next_on.type == TOKEN_KEY) {
                        int k = (int)n_val;
                        if (k >= 1 && k < 32) {
                            on_key_line[k] = target_line;
                        } else {
                            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                        }
                    } else if (next_on.type == TOKEN_TIMER) {
                        int slot = 0;
                        double interval = n_val;
                        if (timer_two_args) {
                            slot = (int)n_val;
                            interval = n_val2;
                        }
                        arm_timer(slot, interval, target_line);
                    } else if (next_on.type == TOKEN_STRIG) {
                        int s = (int)n_val;
                        if (s >= 0 && s < 8) {
                            on_strig_line[s] = target_line;
                        } else {
                            report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
                        }
                    }
                    continue;
                }

                int index = evaluate_int_tok(&ts);
                Token jump_type = ts.tokens[ts.pos++]; // GOTO or GOSUB
                int count = 1;
                while (count < index) {
                    ts.pos++; // skip number
                    Token sep = ts.tokens[ts.pos++];
                    if (sep.type != TOKEN_COMMA) { count = -1; break; }
                    count++;
                }
                if (count == index) {
                    Token target = ts.tokens[ts.pos++];
                    int target_line = resolve_target_line(target);
                    if (jump_type.type == TOKEN_GOSUB) {
                    if (gosub_ptr < 32) {
                            gosub_call_stack[gosub_ptr].stmt = exec_stmt;
                            // For return ptr, skip the rest of this command
                            while(ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type != TOKEN_COLON) ts.pos++;
                            gosub_call_stack[gosub_ptr].ptr = ts.pos < exec_stmt->token_count
                                ? exec_stmt->tokens[ts.pos].start_ptr
                                : exec_stmt->raw_command + strlen(exec_stmt->raw_command);
                            gosub_call_stack[gosub_ptr].event_type = EVENT_NONE;
                            gosub_call_stack[gosub_ptr].event_index = 0;
                            gosub_ptr++;
                            curr = find_line(target.int_val);
                            jumped = 1;
                            break;
                        }
                    } else {
                        curr = find_line(target_line);
                        if (!curr) report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
                        else jumped = 1;
                        break;
                    }
                } else {
                    // If index is out of bounds, BASIKA continues to the next statement
                    while (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type != TOKEN_COLON) ts.pos++;
                }
                continue;
            }

            // Handle GOSUB
            case TOKEN_GOSUB: {
                Token target = ts.tokens[ts.pos++];
                int target_line = resolve_target_line(target);
                if (gosub_ptr < 32) {
                    gosub_call_stack[gosub_ptr].stmt = exec_stmt;
                    gosub_call_stack[gosub_ptr].ptr = ts.pos < exec_stmt->token_count
                        ? exec_stmt->tokens[ts.pos].start_ptr
                        : exec_stmt->raw_command + strlen(exec_stmt->raw_command);
                    gosub_call_stack[gosub_ptr].event_type = EVENT_NONE;
                    gosub_call_stack[gosub_ptr].event_index = 0;
                    gosub_ptr++;
                    curr = find_line(target_line);
                    if (!curr) report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
                    else jumped = 1;
                    break;
                }
                else {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                    break;
                }
            }

            // Handle RETURN
            case TOKEN_RETURN:
                if (gosub_ptr > 0) {
                    gosub_ptr--;
                    curr = gosub_call_stack[gosub_ptr].stmt;
                    resume_ptr = gosub_call_stack[gosub_ptr].ptr;
                    
                    if (gosub_call_stack[gosub_ptr].event_type == EVENT_TIMER) {
                        int slot_idx = gosub_call_stack[gosub_ptr].event_index;
                        if (slot_idx >= 0 && slot_idx < MAX_TIMER_SLOTS) {
                            timers[slot_idx].event_active = 0;
                            timers[slot_idx].last_trigger_time = wall_time_seconds();
                        }
                    } else if (gosub_call_stack[gosub_ptr].event_type == EVENT_KEY) {
                        int k = gosub_call_stack[gosub_ptr].event_index;
                        if (k >= 1 && k < 32) key_event_active[k] = 0;
                    } else if (gosub_call_stack[gosub_ptr].event_type == EVENT_STRIG) {
                        int s = gosub_call_stack[gosub_ptr].event_index;
                        if (s >= 0 && s < 8) strig_event_active[s] = 0;
                    } else if (gosub_call_stack[gosub_ptr].event_type == EVENT_PEN) {
                        pen_event_active = 0;
                    }
                    if (graphics_is_active()) {
                        graphics_present_if_autodisplay();
                    }
                    
                    jumped = 1;
                    break;
                } else {
                    report_runtime_error(ERR_RETURN_WITHOUT_GOSUB);
                    break;
                }

            // Handle NEXT
            case TOKEN_NEXT: {
                ForLoop *repeat = NULL;
                if (run_next_list(&ts, exec_stmt, &repeat) > 0) {
                    /* Looping back to a FOR whose line ends with the header
                     * (the body is on the following lines) would re-enter the
                     * statement only to fall straight through its remainder;
                     * go to the first body statement directly. */
                    if (repeat->start_ts_pos >= repeat->start_stmt->token_count) {
                        curr = repeat->start_stmt->next;
                        resume_ptr = NULL;
                        resume_ts_pos = -1;
                    } else {
                        curr = repeat->start_stmt;
                        resume_ptr = NULL;
                        resume_ts_pos = repeat->start_ts_pos;
                    }
                    jumped = 1;
                }
                break;
            }

            // Handle GOTO
            case TOKEN_GOTO: {
                Token target = ts.tokens[ts.pos++];
                int target_line = resolve_target_line(target);
                Statement *target_stmt = find_line(target_line);
                if (!target_stmt) {
                    report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
                } else {
                    curr = target_stmt;
                    jumped = 1;
                }
                break;
            }

            case TOKEN_NUMBER: {
                // IF ... THEN 30, ELSE 30 and IF ... GOTO 30: a line number alone is a GOTO.
                TokenType before = ts.pos >= 2 ? ts.tokens[ts.pos - 2].type : TOKEN_EOF;
                if (before != TOKEN_THEN && before != TOKEN_ELSE && before != TOKEN_GOTO) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                Statement *target_stmt = find_line(t->int_val);
                if (!target_stmt) {
                    report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
                } else {
                    curr = target_stmt;
                    jumped = 1;
                }
                break;
            }

            case TOKEN_CALL: {
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_IDENTIFIER) {
                    Token sub_tok = ts.tokens[ts.pos++];
                    ProcedureDef *proc = find_procedure(sub_tok.text);
                    if (proc && !proc->is_function) {
                        curr = enter_inline_sub(proc, &ts, exec_stmt);
                        jumped = 1;
                        break;
                    } else {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                } else {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
            }

            case TOKEN_DECLARE:
                ts.pos = exec_stmt->token_count;
                continue;

            case TOKEN_SHARED:
            case TOKEN_STATIC:
                execute_shared_or_static(&ts, t->type == TOKEN_STATIC);
                continue;

            case TOKEN_STOP:
                // Pause like a Ctrl+C break; CONT resumes after this statement.
                break_requested = 1;
                stop_running = 1;
                continue;

            case TOKEN_EXIT: {
                if (ts.pos < exec_stmt->token_count) {
                    Token exit_type = ts.tokens[ts.pos++];
                    if (exit_type.type == TOKEN_FOR) {
                        // Continue after the NEXT that closes the innermost loop.
                        int base = call_stack_depth > 0 ? call_stack[call_stack_depth - 1].for_base : 0;
                        if (for_ptr <= base) {
                            report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                            break;
                        }
                        ForLoop *loop = &for_stack[--for_ptr];
                        if (block_if_depth > loop->block_depth) block_if_depth = loop->block_depth;
                        release_select_frames(select_case_stack, &select_case_depth, loop->select_depth);
                        int end_ts_pos = loop->start_ts_pos;
                        Statement *target_stmt = skip_for_block(loop->start_stmt, end_ts_pos, &end_ts_pos);
                        ForLoop *repeat = NULL;
                        int finished = target_stmt ? finish_skipped_next(target_stmt, &end_ts_pos, &repeat) : 0;
                        if (finished < 0) break;
                        if (finished > 0) {
                            curr = repeat->start_stmt;
                            resume_ptr = NULL;
                            resume_ts_pos = repeat->start_ts_pos;
                        } else if (target_stmt && end_ts_pos < target_stmt->token_count) {
                            curr = target_stmt;
                            resume_ptr = curr->tokens[end_ts_pos].start_ptr;
                        } else if (target_stmt) {
                            curr = target_stmt->next; // a NEXT list ended the line
                            resume_ptr = NULL;
                        } else {
                            report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                        }
                        jumped = 1;
                        break;
                    }
                    if (exit_type.type == TOKEN_DO) {
                        if (do_ptr > 0) {
                            do_ptr--;
                            if (block_if_depth > do_stack[do_ptr].block_depth) block_if_depth = do_stack[do_ptr].block_depth;
                            release_select_frames(select_case_stack, &select_case_depth, do_stack[do_ptr].select_depth);
                        }
                        int end_ts_pos = ts.pos;
                        Statement *target_stmt = skip_do_block(exec_stmt, end_ts_pos, &end_ts_pos);
                        if (target_stmt) {
                            curr = target_stmt;
                            if (end_ts_pos < curr->token_count) {
                                resume_ptr = curr->tokens[end_ts_pos].start_ptr;
                            } else {
                                resume_ptr = NULL;
                            }
                        } else {
                            report_runtime_error(ERR_LOOP_WITHOUT_DO);
                        }
                        jumped = 1;
                        break;
                    }
                    if (exit_type.type == TOKEN_SUB || exit_type.type == TOKEN_FUNCTION) {
                        if (call_stack_depth > 0) {
                            CallFrame *frame = &call_stack[call_stack_depth - 1];
                            leave_inline_sub(frame);
                            call_stack_depth = frame->return_depth;
                            if (frame->return_stmt && frame->return_token_idx < frame->return_stmt->token_count) {
                                curr = frame->return_stmt;
                                resume_ts_pos = frame->return_token_idx;
                            } else if (frame->return_stmt) {
                                curr = frame->return_stmt->next;
                                resume_ts_pos = -1;
                            }
                            resume_ptr = NULL;
                            jumped = 1;
                            break;
                        }
                    }
                }
                break;
            }

            case TOKEN_END:
                if (is_end_if(exec_stmt) && ts.pos == 1) {
                    if (block_if_depth == 0) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    block_if_depth--;
                    ts.pos++;
                    continue;
                }
                if (is_end_select(exec_stmt) && ts.pos == 1) {
                    if (select_case_depth == 0) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    select_case_depth--;
                    if (select_case_stack[select_case_depth].is_string) {
                        basic_string_release(&select_case_stack[select_case_depth].str_val);
                    }
                    ts.pos++;
                    continue;
                }
                if (ts.pos < exec_stmt->token_count && (ts.tokens[ts.pos].type == TOKEN_SUB || ts.tokens[ts.pos].type == TOKEN_FUNCTION)) {
                    if (call_stack_depth > 0) {
                        CallFrame *frame = &call_stack[call_stack_depth - 1];
                        leave_inline_sub(frame);
                        call_stack_depth = frame->return_depth;
                        if (frame->return_stmt && frame->return_token_idx < frame->return_stmt->token_count) {
                            curr = frame->return_stmt;
                            resume_ts_pos = frame->return_token_idx;
                        } else if (frame->return_stmt) {
                            curr = frame->return_stmt->next;
                            resume_ts_pos = -1;
                        }
                        resume_ptr = NULL;
                        jumped = 1;
                        break;
                    }
                    ts.pos = exec_stmt->token_count;
                    continue;
                }
                stop_running = 1;
                break;

            case TOKEN_RESUME: {
                Token rt = (ts.pos < exec_stmt->token_count)
                    ? ts.tokens[ts.pos++]
                    : (Token){.type = TOKEN_EOF};
                if (rt.type == TOKEN_NEXT) {
                    curr = error_stmt;
                    resume_ptr = error_next_ptr;
                } else if (rt.type == TOKEN_NUMBER && rt.int_val != 0) {
                    curr = find_line(rt.int_val);
                    resume_ptr = NULL;
                } else if (rt.type == TOKEN_IDENTIFIER) {
                    curr = find_line(resolve_target_line(rt));
                    resume_ptr = NULL;
                } else {
                    curr = error_stmt;
                    resume_ptr = exec_stmt->raw_command; // Start of line
                }
                if (!curr) {
                    report_runtime_error(ERR_CANT_RESUME);
                } else {
                    jumped = 1;
                }
                break;
            }

            case TOKEN_LET:
                current_token = &ts.tokens[ts.pos++]; // Skip LET to get variable identifier
                t = current_token;
                /* fall through */
            case TOKEN_IDENTIFIER: {
                // A metacommand ($RESIZE:ON, $NOPREFIX) takes the whole line and was
                // applied before the program ran; it is not a "$RESIZE:" label.
                if (t->text[0] == '$') {
                    if (strcasecmp(t->text, "$RESIZE") == 0 && !apply_resize_metacommand(t->start_ptr, 1)) {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                        break;
                    }
                    ts.pos = exec_stmt->token_count;
                    continue;
                }
                // "Name:" is a line label only at the start of a line; after THEN or
                // another statement it calls SUB Name.
                if (current_token == ts.tokens && ts.tokens[ts.pos].type == TOKEN_COLON) {
                    ts.pos++; // Skip colon
                    continue;
                }
                // A SUB name is never a variable: "Name (a) * 2, b" calls it rather
                // than reading Name(a) as an array element first. The arguments
                // follow the name, which need not start the line (it may follow
                // THEN, ELSE or a colon).
                /* With no procedures defined the lookup cannot match, so the
                 * (cached) call is skipped, as in the expression paths. */
                ProcedureDef *proc = proc_count > 0 ? find_procedure_tok(current_token) : NULL;
                if (proc && !proc->is_function) {
                    ts.pos = (int)(current_token - ts.tokens) + 1;
                    curr = enter_inline_sub(proc, &ts, exec_stmt);
                    jumped = 1;
                    break;
                }
                // _CLIPBOARD$ = text$ sets the clipboard rather than a variable.
                if (t->text[0] == '_' && strcasecmp(t->text, "_CLIPBOARD$") == 0 &&
                    ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_EQUALS) {
                    ts.pos++;
                    BasicString text = {0};
                    if (parse_string_expression_tok_heap(&ts, &text) && !runtime_error_occurred) {
                        basic_string_append(&text, "", 1);
                        graphics_clipboard_set((const char *)text.data);
                    }
                    basic_string_release(&text);
                    continue;
                }
                // _UPRINTSTRING (x, y), ... is a statement, not an array element.
                if (t->text[0] == '_' && strcasecmp(t->text, "_UPRINTSTRING") == 0) goto identifier_statement;
                // Direct handling of assignments
                /* Renaming to the FUNCTION's own name only happens inside a
                 * call; at module scope name_string_function_result is a
                 * no-op, so skip the call entirely. */
                if (call_stack_depth > 0) name_string_function_result(current_token);
                int idx = resolve_token_variable(current_token);
                int array_idx = -1;
                int is_member = user_type_count > 0
                    ? resolve_user_type_reference_tok(&ts, current_token, &idx,
                                                      &array_idx)
                    : 0;
                if (is_member < 0) break;
                if (!is_member) {
                    UserTypeInstance record_instance;
                    if (user_type_count > 0) {
                        int raw_index = find_existing_variable_raw_name(t->text);
                        if (raw_index >= 0 &&
                            get_user_type_instance_for_root(raw_index, &record_instance)) {
                            idx = raw_index;
                        }
                    }
                    /* parse_array_index_tok only acts on a "("; testing for
                     * one first keeps scalar assignments call-free. */
                    if (ts.pos < exec_stmt->token_count &&
                        ts.tokens[ts.pos].type == TOKEN_LPAREN) {
                        array_idx = parse_array_index_tok(&ts, idx);
                    }
                }
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_EQUALS) {
                    ts.pos++;
                    UserTypeInstance record_instance;
                    if (user_type_count > 0 &&
                        get_user_type_instance_for_root(idx, &record_instance)) {
                        assign_user_type_value_tok(&ts, idx, array_idx);
                    } else if (is_member
                            ? is_string_var(get_variable_ptr(idx)->name)
                            : is_string_identifier_tok(current_token)) {
                        BasicString value = {0};
                        if (parse_string_expression_tok_heap(&ts, &value)) {
                            assign_string_variable_move(idx, array_idx, &value);
                        }
                        basic_string_release(&value);
                    } else {
                        Num value;
                        if (!evaluate_num_tok_cached(&ts, &value)) value = evaluate_num_tok(&ts);
                        // An error while evaluating aborts the assignment, as in QBasic.
                        if (!runtime_error_occurred) set_numeric_variable_num(idx, array_idx, value);
                    }
                    continue;
                }
                // If not an assignment, fall back to interpret_line_at_ptr
            identifier_statement:
                ts.pos--; // Put identifier back
                /* fall through */
            }

            default: {
                // Fallback for complex commands
                const char *command_start_ptr = t->start_ptr;
                const char *temp_ptr = command_start_ptr;
                interpret_statement_text(exec_stmt, &temp_ptr);
                // Advance TokenStream to match the processed string
                while(ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].start_ptr < temp_ptr) ts.pos++;
                break;
            }
            }

            maybe_queue_timer_event();
            /* Each dispatcher early-outs on its own counter; the combined
             * test makes the common case (no traps pending) one branch
             * instead of three calls. The dispatchers cannot change jumped
             * (the caller owns it), but they can report errors, so the
             * runtime_error_occurred checks between them stay. */
            if (!jumped && !runtime_error_occurred &&
                (timer_trap_count | key_event_pending_count | device_event_pending_count)) {
                if (dispatch_pending_timer_event(exec_stmt, &ts, &curr, &resume_ptr)) {
                    jumped = 1;
                    break;
                }
                if (!runtime_error_occurred &&
                    dispatch_pending_key_event(exec_stmt, &ts, &curr, &resume_ptr)) {
                    jumped = 1;
                    break;
                }
                if (!runtime_error_occurred &&
                    dispatch_pending_device_event(exec_stmt, &ts, &curr, &resume_ptr)) {
                    jumped = 1;
                    break;
                }
            }
        }

        if (runtime_error_occurred) {
            error_stmt = exec_stmt;
            error_ptr = exec_stmt->raw_command;

            // For RESUME NEXT to work correctly with the optimized loop, we find
            // the start of the next command on the same line using the tokens.
            int next_idx = ts.pos;
            while (next_idx < exec_stmt->token_count && 
                   exec_stmt->tokens[next_idx].type != TOKEN_COLON && 
                   exec_stmt->tokens[next_idx].type != TOKEN_EOF) {
                next_idx++;
            }
            if (next_idx < exec_stmt->token_count && exec_stmt->tokens[next_idx].type == TOKEN_COLON) {
                error_next_ptr = exec_stmt->tokens[next_idx + 1].start_ptr;
            } else {
                error_next_ptr = exec_stmt->raw_command + strlen(exec_stmt->raw_command);
            }
        }

        if (stop_running && break_requested && !runtime_error_occurred) {
            note_break(exec_stmt, &ts, jumped, curr, resume_ptr, resume_ts_pos);
        }

        if (!stop_running && !jumped) {
            /* The program list is sorted, so the successor of the statement
             * just executed is the next line; avoid rescanning from head. */
            curr = (exec_stmt->line_number == current_executing_line)
                ? exec_stmt->next : find_next_statement(current_executing_line);
        }

        if (runtime_error_occurred && on_error_goto_line > 0) {
            Statement *trap = find_line(on_error_goto_line);
            if (trap) {
                frames_being_built = 0; // the failed statement's calls were abandoned
                curr = trap;
                resume_ptr = NULL;
                runtime_error_occurred = 0;
                stop_running = 0;
                last_runtime_error_msg[0] = '\0';
                continue;
            }
        }
    }
}

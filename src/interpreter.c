#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <glob.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <zlib.h>
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

static Variable vars[1024]; 
static int var_count = 0;
static int var_hash_table[2048]; // Hash table for O(1) variable lookup

typedef struct {
    Token *tokens;
    int pos;
} TokenStream;

static int print_col = 0;
static int internal_argc = 0;
static char **internal_argv = NULL;
static char internal_command_line[1024] = "";

static unsigned char basika_memory[65536];

static int last_expression_is_double = 0;

static char default_type_map[26] = {
    '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!',
    '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!'
};

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

static unsigned int rnd_seed = 1;
static double last_rnd_value = 0.0;
static int on_error_goto_line = 0;
static int runtime_error_occurred = 0;
static int last_runtime_error_code = 0;
static int last_runtime_error_line = 0;
static int current_executing_line = 0;
static char last_runtime_error_msg[256] = "";
static Statement *error_stmt = NULL;
static const char *error_ptr = NULL;
static const char *error_next_ptr = NULL;

typedef struct {
    char name[32];
    char param_name[32];
    char expression[256];
} UserFunction;

static UserFunction user_functions[64];
static int user_function_count = 0;

void set_args(int argc, char **argv) {
    // Initialize hash table
    for (int i = 0; i < 2048; i++) var_hash_table[i] = -1;
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
static int parse_array_index(const char **input, int var_idx);
static int parse_array_index_tok(TokenStream *ts, int var_idx);
static void set_string_variable(int idx, int array_idx, const char *value);
static void set_numeric_variable(int idx, int array_idx, double val);
static double relational_expression_tok(TokenStream *ts);
static double logical_not_tok(TokenStream *ts);
static double bitwise_and_tok(TokenStream *ts);
static double bitwise_or_tok(TokenStream *ts);
double evaluate_expression(const char **input);
double evaluate_expression_tok(TokenStream *ts);

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
                    current_stmt = find_next_statement(current_stmt->line_number);
                    *out_ts_pos = 0; // Start from beginning of next statement
                    return current_stmt;
                }
            }
        }
        // Reached end of current statement, move to next
        current_stmt = find_next_statement(current_stmt->line_number);
        if (current_stmt) {
            current_ts_pos = 0; // Start from beginning of next statement
        }
    }
    return NULL; // Matching end token not found
}

// Helper to skip tokens until a matching NEXT token for a specific FOR variable is found
// Returns the statement *after* the NEXT token, and the token position within that statement.
// If not found, returns NULL for statement.
static Statement* skip_for_block(Statement *start_stmt, int start_ts_pos, int for_var_idx, int *out_ts_pos) {
    Statement *current_stmt = start_stmt;
    int current_ts_pos = start_ts_pos;
    int nest_depth = 1;

    while (nest_depth > 0 && current_stmt) {
        while (current_ts_pos < current_stmt->token_count) {
            Token t = current_stmt->tokens[current_ts_pos++];
            if (t.type == TOKEN_FOR) {
                nest_depth++;
            } else if (t.type == TOKEN_NEXT) {
                if (current_ts_pos < current_stmt->token_count && current_stmt->tokens[current_ts_pos].type == TOKEN_IDENTIFIER) {
                    int next_var_idx = find_variable(current_stmt->tokens[current_ts_pos].text);
                    if (next_var_idx == for_var_idx) {
                        nest_depth--;
                        current_ts_pos++; // Consume the variable name
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
                    current_stmt = find_next_statement(current_stmt->line_number);
                    *out_ts_pos = 0;
                    return current_stmt;
                }
            }
        }
        current_stmt = find_next_statement(current_stmt->line_number);
        if (current_stmt) {
            current_ts_pos = 0;
        }
    }
    return NULL;
}

static const char* get_error_message(RuntimeError code) {
    switch (code) {
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
        case ERR_CANT_RESUME: return "Can't RESUME";
        case ERR_RESUME_WITHOUT_ERROR: return "RESUME without error";
        case ERR_FOR_WITHOUT_NEXT: return "FOR without NEXT";
        case ERR_WHILE_WITHOUT_WEND: return "WHILE without WEND";
        case ERR_WEND_WITHOUT_WHILE: return "WEND without WHILE";
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
            snprintf(full_msg, sizeof(full_msg), "%s in %d\n", msg, current_executing_line);
            basic_output(full_msg);
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
static int is_string_var(const char *name);
static int parse_array_index(const char **input, int var_idx);
static int parse_array_index_tok(TokenStream *ts, int var_idx);
static void set_string_variable(int idx, int array_idx, const char *value);
static void set_numeric_variable(int idx, int array_idx, double val);
double evaluate_expression(const char **input);
double evaluate_expression_tok(TokenStream *ts);

static void clear_data_pointer(void) {
    data_stmt = NULL;
    data_ptr = NULL;
}

static int is_data_statement(Statement *stmt) {
    if (!stmt) return 0;
    const char *p = stmt->raw_command;
    Token t = get_next_token(&p);
    return t.type == TOKEN_DATA;
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
        data_ptr = data_stmt->raw_command;
        get_next_token(&data_ptr);
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
    data_ptr = data_stmt->raw_command;
    get_next_token(&data_ptr);
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

static void restore_data_to_line(int line) {
    if (line <= 0) {
        reset_data_pointer();
        return;
    }
    Statement *stmt = find_line(line);
    if (!stmt) {
        reset_data_pointer();
        return;
    }
    data_stmt = stmt;
    data_ptr = data_stmt->raw_command;
    Token t = get_next_token(&data_ptr);
    if (t.type != TOKEN_DATA) {
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
        parse_string_expression(&data_ptr, out, out_size);
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
        set_numeric_variable(var_idx, array_idx, atof(item));
    }
    return 1;
}

static void set_numeric_variable(int idx, int array_idx, double val) {
    const char *name = vars[idx].name;
    size_t len = strlen(name);
    char suffix = (len > 0) ? name[len - 1] : '!';

    if (suffix == '%') {
        if (val < -32768.5 || val > 32767.5) {
            report_runtime_error(ERR_OVERFLOW);
            return;
        }
        val = round(val);
    } else if (suffix == '!') {
        val = (double)((float)val);
    }

    if (array_idx >= 0 && vars[idx].array && array_idx < vars[idx].array_size) {
        vars[idx].array[array_idx] = val;
    } else {
        vars[idx].value = val;
    }
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
            if (c >= 'A' && c <= 'Z') default_type_map[c - 'A'] = type_char;
        }
        
        saved = *ptr;
        if (get_next_token(ptr).type != TOKEN_COMMA) {
            *ptr = saved;
            break;
        }
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
    *input = saved;
    reset_data_pointer();
    return 1;
}

static int parse_randomize(const char **input) {
    randomize_seed(input);
    return 1;
}

static int is_string_var(const char *name) {
    int len = (int)strlen(name);
    if (len == 0) return 0;
    char last = name[len - 1];
    if (last == '$') return 1;
    if (last == '%' || last == '!' || last == '#') return 0;
    int first = toupper((unsigned char)name[0]);
    if (first >= 'A' && first <= 'Z') {
        return (default_type_map[first - 'A'] == '$');
    }
    return 0;
}

// Forward declaration for is_string_token (used in parse_string_expression_tok)
static int is_string_token(Token t);

static int deflate_string(const char *input, char *output, int output_size) {
    uLong source_len = (uLong)strlen(input);
    Bytef compressed[BASIC_STRING_MAX];
    uLong compressed_len = sizeof(compressed);
    if (compress2(compressed, &compressed_len, (const Bytef *)input, source_len, Z_DEFAULT_COMPRESSION) != Z_OK ||
        compressed_len * 2 + 1 >= (uLong)output_size) {
        output[0] = '\0';
        return 0;
    }
    static const char hex[] = "0123456789ABCDEF";
    for (uLong i = 0; i < compressed_len; i++) {
        output[i * 2] = hex[compressed[i] >> 4];
        output[i * 2 + 1] = hex[compressed[i] & 15];
    }
    output[compressed_len * 2] = '\0';
    return (int)(compressed_len * 2);
}

static int inflate_string(const char *input, char *output, int output_size) {
    size_t encoded_len = strlen(input);
    if (encoded_len == 0 || (encoded_len % 2) != 0 || encoded_len / 2 > BASIC_STRING_MAX / 2) {
        output[0] = '\0';
        return 0;
    }
    Bytef compressed[BASIC_STRING_MAX / 2];
    for (size_t i = 0; i < encoded_len; i += 2) {
        unsigned int byte = 0;
        if (sscanf(input + i, "%2x", &byte) != 1) {
            output[0] = '\0';
            return 0;
        }
        compressed[i / 2] = (Bytef)byte;
    }
    uLong decompressed_len = (uLong)(output_size - 1);
    int result = uncompress((Bytef *)output, &decompressed_len, compressed, (uLong)(encoded_len / 2));
    if (result != Z_OK || decompressed_len >= (uLong)output_size) {
        output[0] = '\0';
        return 0;
    }
    output[decompressed_len] = '\0';
    return (int)decompressed_len;
}

static unsigned int hash_name(const char *name) {
    unsigned int hash = 5381;
    int c;
    while ((c = *name++)) hash = ((hash << 5) + hash) + (unsigned int)c;
    return hash % 2048;
}

int find_variable(const char *name) {
    char normalized[64];
    int i;
    for (i = 0; i < 63 && name[i]; i++) {
        normalized[i] = (char)toupper((unsigned char)name[i]);
    }
    normalized[i] = '\0';
    int len = i;

    if (len > 0) {
        char last = normalized[len - 1];
        if (last != '$' && last != '%' && last != '!' && last != '#') {
            char suffix = default_type_map[normalized[0] - 'A'];
            if (suffix != '\0') {
                normalized[len] = suffix;
                normalized[len+1] = '\0';
            }
        }
    }

    unsigned int h = hash_name(normalized);
    int cached_idx = var_hash_table[h];
    if (cached_idx != -1 && strcmp(vars[cached_idx].name, normalized) == 0) {
        return cached_idx;
    }

    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, normalized) == 0) {
            var_hash_table[h] = i;
            return i;
        }
    }
    if (var_count < 1024) {
        var_hash_table[h] = var_count;
        memset(&vars[var_count], 0, sizeof(Variable));
        strncpy(vars[var_count].name, normalized, 31);
        vars[var_count].name[31] = '\0';
        return var_count++;
    }
    return -1;
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
    EVENT_STRIG
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

static int on_timer_line = 0;
static double timer_interval = 0.0;
static int timer_state = 0; // 0=OFF, 1=ON, 2=STOP
static int timer_pending = 0;
static int timer_event_active = 0;
static double timer_last_trigger_time = 0.0;

static int on_strig_line[8];
static int strig_state = 0; // 0=OFF, 1=ON, 2=STOP
static int strig_event_pending[8];
static int strig_event_active[8];
static int strig_button_pressed_since[8];
static int strig_button_currently_pressed[8];

static double wall_time_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

void basika_trigger_key_event(int key_idx) {
    if (key_idx >= 1 && key_idx < 32) {
        if (key_state[key_idx] == 1 || key_state[key_idx] == 2) {
            key_event_pending[key_idx] = 1;
        }
    }
}

static int dispatch_pending_timer_event(Statement *exec_stmt, TokenStream *ts, Statement **curr, const char **resume_ptr) {
    if (timer_state == 0 || timer_pending == 0 || timer_event_active || on_timer_line == 0) return 0;

    Statement *target = find_line(on_timer_line);
    if (!target) {
        report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
        timer_pending = 0;
        timer_state = 0;
        return 1;
    }
    if (gosub_ptr >= 32) {
        report_runtime_error(ERR_OUT_OF_MEMORY);
        timer_pending = 0;
        return 1;
    }

    gosub_call_stack[gosub_ptr].stmt = exec_stmt;
    if (ts->pos < exec_stmt->token_count) {
        gosub_call_stack[gosub_ptr].ptr = exec_stmt->tokens[ts->pos].start_ptr;
    } else {
        gosub_call_stack[gosub_ptr].ptr = exec_stmt->raw_command + strlen(exec_stmt->raw_command);
    }
    gosub_call_stack[gosub_ptr].event_type = EVENT_TIMER;
    gosub_call_stack[gosub_ptr].event_index = 0;
    gosub_ptr++;

    timer_pending = 0;
    timer_event_active = 1;
    *curr = target;
    *resume_ptr = NULL;
    return 1;
}

static int dispatch_pending_key_event(Statement *exec_stmt, TokenStream *ts, Statement **curr, const char **resume_ptr) {
    for (int k = 1; k < 32; k++) {
        if (!key_event_pending[k]) continue;
        if ((key_state[k] != 1 && key_state[k] != 2) || key_event_active[k] || on_key_line[k] == 0) continue;

        Statement *target = find_line(on_key_line[k]);
        if (!target) {
            report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
            key_event_pending[k] = 0;
            return 1;
        }
        if (gosub_ptr >= 32) {
            report_runtime_error(ERR_OUT_OF_MEMORY);
            key_event_pending[k] = 0;
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
        key_event_active[k] = 1;
        *curr = target;
        *resume_ptr = NULL;
        return 1;
    }
    return 0;
}

static void maybe_queue_timer_event(void) {
    if (timer_state == 0 || on_timer_line == 0 || timer_interval <= 0.0 || timer_pending || timer_event_active) return;

    if (!find_line(on_timer_line)) {
        report_runtime_error(ERR_UNDEFINED_LINE_NUMBER);
        timer_state = 0;
        timer_pending = 0;
        return;
    }

    double now = wall_time_seconds();
    if (now - timer_last_trigger_time >= timer_interval) {
        timer_pending = 1;
        timer_last_trigger_time = now;
    }
}

void basika_trigger_strig_event(int strig_idx, int pressed) {
    if (strig_idx >= 0 && strig_idx < 8) {
        if (pressed) {
            strig_button_pressed_since[strig_idx] = 1;
            strig_button_currently_pressed[strig_idx] = 1;
            if (strig_state == 1 || strig_state == 2) {
                strig_event_pending[strig_idx] = 1;
            }
        } else {
            strig_button_currently_pressed[strig_idx] = 0;
        }
    }
}

typedef struct {
    int var_idx;
    double end_val;
    double step_val;
    Statement *start_stmt;
    const char *start_ptr;
} ForLoop;

static ForLoop for_stack[16];
static int for_ptr = 0;

typedef struct {
    Statement *stmt;
    const char *ptr;
} WhileLoop;
static WhileLoop while_stack[16];
static int while_ptr = 0;

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
        int val = indices[i];
        if (val < option_base || val > max_subscript) {
            report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
            return -1;
        }
        int offset = (option_base == 0) ? val : (val - 1);
        linear += offset * multiplier;
        
        int dim_size = (option_base == 0) ? (max_subscript + 1) : max_subscript;
        multiplier *= dim_size;
    }
    return linear;
}

static void ensure_array_dimensioned(int idx, int num_dims) {
    if (vars[idx].array || vars[idx].s_array) return; // already dimensioned
    
    // Auto-dimension to 10 for each dimension
    int total_size = 1;
    for (int i = 0; i < num_dims; i++) {
        int dim_size = (option_base == 0) ? 11 : 10;
        total_size *= dim_size;
    }
    
    if (vars[idx].name[strlen(vars[idx].name)-1] == '$') {
        vars[idx].s_array = calloc(total_size, sizeof(char*));
    } else {
        vars[idx].array = malloc(total_size * sizeof(double));
        for (int i = 0; i < total_size; i++) vars[idx].array[i] = 0;
    }
    vars[idx].array_size = total_size;
    vars[idx].num_dims = num_dims;
    for (int i = 0; i < num_dims; i++) vars[idx].dims[i] = 10;
    
    arrays_dimensioned = 1;
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
    indices[0] = (int)evaluate_expression(input);
    num_indices = 1;

    while (num_indices < 3) {
        const char *sep_saved = *input;
        Token sep = get_next_token(input);
        if (sep.type != TOKEN_COMMA) {
            *input = sep_saved;
            break;
        }
        indices[num_indices++] = (int)evaluate_expression(input);
    }

    Token close = get_next_token(input);
    if (close.type != TOKEN_RPAREN) {
        *input = saved;
        return -1;
    }

    // Auto-dimension if not already dimensioned
    ensure_array_dimensioned(var_idx, num_indices);

    return calc_linear_index(&vars[var_idx], indices, num_indices);
}

static int parse_array_index_tok(TokenStream *ts, int var_idx) {
    if (ts->tokens[ts->pos].type != TOKEN_LPAREN) return -1;
    ts->pos++;

    int indices[3] = {0};
    int num_indices = 0;
    indices[0] = (int)evaluate_expression_tok(ts);
    num_indices = 1;

    while (num_indices < 3) {
        if (ts->tokens[ts->pos].type != TOKEN_COMMA) break;
        ts->pos++;
        indices[num_indices++] = (int)evaluate_expression_tok(ts);
    }

    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;

    ensure_array_dimensioned(var_idx, num_indices);
    return calc_linear_index(&vars[var_idx], indices, num_indices);
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

    if (array_idx >= 0) {
        if (vars[idx].s_array && array_idx >= 0 && array_idx < vars[idx].array_size && vars[idx].s_array[array_idx]) {
            src = vars[idx].s_array[array_idx];
        }
    } else {
        if (vars[idx].s_value) src = vars[idx].s_value;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static int get_string_variable_raw(int idx, int array_idx, char *dest, int dest_size) {
    int fnum = 0;
    FieldBinding binding;
    if (get_field_binding_for_var(idx, array_idx, &fnum, &binding)) {
        FileFieldState *state = &file_field_state[fnum];
        if (state->buffer && binding.offset >= 0 && binding.offset < state->size) {
            int copy_len = binding.len;
            if (binding.offset + copy_len > state->size) copy_len = state->size - binding.offset;
            if (copy_len < 0) copy_len = 0;
            if (copy_len >= dest_size) copy_len = dest_size - 1;
            memcpy(dest, state->buffer + binding.offset, (size_t)copy_len);
            dest[copy_len] = '\0';
            return copy_len;
        }
    }

    const char *src = "";
    if (array_idx >= 0) {
        if (vars[idx].s_array && array_idx >= 0 && array_idx < vars[idx].array_size && vars[idx].s_array[array_idx]) {
            src = vars[idx].s_array[array_idx];
        }
    } else {
        if (vars[idx].s_value) src = vars[idx].s_value;
    }
    int len = (int)strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, src, (size_t)len);
    dest[len] = '\0';
    return len;
}

static void append_string_value(char *dest, const char *src, int dest_size) {
    size_t used = strlen(dest);
    if (used + 1 >= (size_t)dest_size) return;
    strncat(dest, src, dest_size - 1 - used);
}

static int parse_string_expression_tok(TokenStream *ts, char *out, int out_size) {
    out[0] = '\0';
    int first = 1;

    while (1) {
        char term[BASIC_STRING_MAX] = "";
        Token t = ts->tokens[ts->pos];

        if (t.type == TOKEN_CHR || t.type == TOKEN_TAB) {
            ts->pos++; // chr/tab
            ts->pos++; // (
            double arg = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (t.type == TOKEN_CHR) {
                term[0] = (unsigned char)((int)arg & 0xFF);
                term[1] = '\0';
            } else {
                int w = (int)arg; if (w < 0) w = 0; if (w >= 255) w = 255;
                memset(term, ' ', w); term[w] = '\0';
            }
        } else if (t.type == TOKEN_LEFT || t.type == TOKEN_RIGHT || t.type == TOKEN_MID) {
            TokenType ft = t.type;
            ts->pos++; // func
            ts->pos++; // (
            char base[BASIC_STRING_MAX] = "";
            parse_string_expression_tok(ts, base, sizeof(base));
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++; // ,
            int n1 = (int)evaluate_expression_tok(ts);
            int n2 = -1;
            if (ft == TOKEN_MID) {
                if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
                    ts->pos++;
                    n2 = (int)evaluate_expression_tok(ts);
                }
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            int slen = (int)strlen(base);
            if (ft == TOKEN_LEFT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                strncpy(term, base, cnt); term[cnt] = '\0';
            } else if (ft == TOKEN_RIGHT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                strncpy(term, base + (slen - cnt > 0 ? slen - cnt : 0), cnt); term[cnt] = '\0';
            } else { // MID
                int start = n1 - 1; if (start < 0) start = 0;
                if (start >= slen) term[0] = '\0';
                else {
                    int cnt = (n2 == -1) ? (slen - start) : n2;
                    if (cnt < 0) cnt = 0; if (start + cnt > slen) cnt = slen - start;
                    strncpy(term, base + start, cnt); term[cnt] = '\0';
                }
            }
        } else if (t.type == TOKEN_SPACE || t.type == TOKEN_SPC) {
            ts->pos++; // space/spc
            ts->pos++; // (
            int n = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (n < 0) n = 0; if (n > 255) n = 255;
            memset(term, ' ', n); term[n] = '\0';
        } else if (t.type == TOKEN_STRING_FUNC) {
            ts->pos++; // string$
            ts->pos++; // (
            int n = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++; // ,
            char c = ' ';
            char arg_buf[BASIC_STRING_MAX] = "";
            int is_str = is_string_token(ts->tokens[ts->pos]);
            if (is_str) {
                parse_string_expression_tok(ts, arg_buf, sizeof(arg_buf));
                if (arg_buf[0]) c = arg_buf[0];
            } else {
                c = (char)evaluate_expression_tok(ts);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (n < 0) n = 0; if (n > 255) n = 255;
            memset(term, c, n); term[n] = '\0';
        } else if (t.type == TOKEN_DEFLATE || t.type == TOKEN_INFLATE) {
            ts->pos++; // function
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) ts->pos++;
            char input[BASIC_STRING_MAX] = "";
            parse_string_expression_tok(ts, input, sizeof(input));
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (t.type == TOKEN_DEFLATE) {
                deflate_string(input, term, sizeof(term));
            } else {
                inflate_string(input, term, sizeof(term));
            }
        } else if (t.type == TOKEN_COMMANDS) {
            ts->pos++;
            strncpy(term, internal_command_line, sizeof(term) - 1);
            term[sizeof(term) - 1] = '\0';
        } else if (t.type == TOKEN_ARGVS) {
            ts->pos++; // argv$
            ts->pos++; // (
            int idx = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (idx >= 0 && idx < internal_argc && internal_argv) {
                strncpy(term, internal_argv[idx], sizeof(term) - 1);
                term[sizeof(term) - 1] = '\0';
            }
        } else if (t.type == TOKEN_GETS) {
            ts->pos++; // get$
            ts->pos++; // (
            int fnum = -1;
            if (ts->tokens[ts->pos].type == TOKEN_HASH) ts->pos++;
            fnum = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++; // ,
            int rec = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++; // ,
            int len = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
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
            strncpy(term, buf, sizeof(term) - 1);
            term[sizeof(term) - 1] = '\0';
        } else if (t.type == TOKEN_INKEY) {
            ts->pos++;
            if (graphics_is_active()) {
                update_graphics();
                int c = get_graphics_char();
                if (c) {
                    term[0] = (char)c;
                    term[1] = '\0';
                }
            } else {
                int c = get_stdin_char();
                if (c) {
                    term[0] = (char)c;
                    term[1] = '\0';
                }
            }
        } else if (t.type == TOKEN_UCASE || t.type == TOKEN_LCASE || t.type == TOKEN_TRIM || t.type == TOKEN_LTRIM || t.type == TOKEN_RTRIM) {
            TokenType ft = t.type;
            ts->pos++; // func
            ts->pos++; // (
            char base[BASIC_STRING_MAX] = "";
            parse_string_expression_tok(ts, base, sizeof(base));
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (ft == TOKEN_UCASE) {
                for (int i = 0; base[i]; i++) base[i] = toupper((unsigned char)base[i]);
            } else if (ft == TOKEN_LCASE) {
                for (int i = 0; base[i]; i++) base[i] = tolower((unsigned char)base[i]);
            } else if (ft == TOKEN_TRIM) {
                int start = 0, end = (int)strlen(base) - 1;
                while (start <= end && isspace((unsigned char)base[start])) start++;
                while (end >= start && isspace((unsigned char)base[end])) end--;
                int len = (end >= start) ? (end - start + 1) : 0;
                memmove(base, base + start, len);
                base[len] = '\0';
            } else if (ft == TOKEN_LTRIM) {
                int start = 0;
                while (base[start] && isspace((unsigned char)base[start])) start++;
                if (start > 0) {
                    int len = (int)strlen(base + start);
                    memmove(base, base + start, len + 1);
                }
            } else if (ft == TOKEN_RTRIM) {
                int end = (int)strlen(base) - 1;
                while (end >= 0 && isspace((unsigned char)base[end])) end--;
                base[end + 1] = '\0';
            }
            strncpy(term, base, sizeof(term) - 1);
            term[sizeof(term) - 1] = '\0';
        } else if (t.type == TOKEN_HEX || t.type == TOKEN_OCT) {
            ts->pos++; // func
            ts->pos++; // (
            double val = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            int intval = (int)val;
            if (t.type == TOKEN_HEX) snprintf(term, sizeof(term), "%X", intval);
            else snprintf(term, sizeof(term), "%o", intval);
        } else if (t.type == TOKEN_TIME || t.type == TOKEN_DATE) {
            ts->pos++;
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            if (t.type == TOKEN_TIME) {
                strftime(term, sizeof(term), "%H:%M:%S", timeinfo);
            } else {
                strftime(term, sizeof(term), "%m-%d-%Y", timeinfo);
            }
        } else if (t.type == TOKEN_ENVIRON) {
            ts->pos++; // environ$
            ts->pos++; // (
            char arg_val[BASIC_STRING_MAX] = "";
            int is_str = is_string_token(ts->tokens[ts->pos]);
            if (is_str) {
                parse_string_expression_tok(ts, arg_val, sizeof(arg_val));
                char *ev = getenv(arg_val);
                if (ev) strncpy(term, ev, sizeof(term)-1);
            } else {
                double val = evaluate_expression_tok(ts);
                int idx = (int)val;
                extern char **environ;
                if (environ && idx > 0) {
                    int count = 1;
                    for (char **e = environ; *e; e++, count++) {
                        if (count == idx) {
                            strncpy(term, *e, sizeof(term)-1);
                            break;
                        }
                    }
                }
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
        } else if (t.type == TOKEN_STR) {
            ts->pos++; // str$
            ts->pos++; // (
            double val = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (val >= 0) snprintf(term, sizeof(term), " %g", val);
            else snprintf(term, sizeof(term), "%g", val);
        } else if (t.type == TOKEN_MKI || t.type == TOKEN_MKS || t.type == TOKEN_MKD) {
            ts->pos++; // func
            ts->pos++; // (
            double val = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
            if (t.type == TOKEN_MKI) {
                int ival = (int)val;
                term[0] = (char)(ival & 0xFF);
                term[1] = (char)((ival >> 8) & 0xFF);
                term[2] = '\0';
            } else if (t.type == TOKEN_MKS) {
                float fval = (float)val;
                memcpy(term, &fval, 4);
                term[4] = '\0';
            } else {
                double dval = val;
                memcpy(term, &dval, 8);
                term[8] = '\0';
            }
        } else if (t.type == TOKEN_STRING) {
            strncpy(term, t.text, sizeof(term) - 1);
            ts->pos++;
        } else if (t.type == TOKEN_IDENTIFIER && is_string_var(t.text)) {
            int idx = t.var_idx;
            if (idx == -1) idx = find_variable(t.text);
            ts->pos++;
            int array_idx = parse_array_index_tok(ts, idx);
            char temp[BASIC_STRING_MAX] = "";
            get_string_variable_value(idx, array_idx, temp, sizeof(temp));
            strncpy(term, temp, sizeof(term) - 1);
        } else {
            if (first) {
                double val = evaluate_expression_tok(ts);
                snprintf(out, out_size, "%g", val);
                return 0;
            }
            break;
        }

        append_string_value(out, term, out_size);
        first = 0;

        if (ts->tokens[ts->pos].type != TOKEN_PLUS) {
            break;
        }
        ts->pos++; // consume '+'
    }
    return 1;
}

static int parse_string_expression(const char **input, char *out, int out_size) {
    out[0] = '\0';
    int first = 1;

    while (1) {
        char term[BASIC_STRING_MAX] = "";
        const char *saved = *input;
        Token t = get_next_token(input);

        if (t.type == TOKEN_CHR || t.type == TOKEN_TAB) {
            get_next_token(input); // (
            double arg = evaluate_expression(input);
            get_next_token(input); // )
            if (t.type == TOKEN_CHR) {
                term[0] = (unsigned char)((int)arg & 0xFF);
                term[1] = '\0';
            } else {
                int w = (int)arg; if (w < 0) w = 0; if (w >= 255) w = 255;
                memset(term, ' ', w); term[w] = '\0';
            }
        } else if (t.type == TOKEN_LEFT || t.type == TOKEN_RIGHT || t.type == TOKEN_MID) {
            TokenType ft = t.type;
            get_next_token(input); // (
            char base[BASIC_STRING_MAX] = "";
            parse_string_expression(input, base, sizeof(base));
            get_next_token(input); // ,
            int n1 = (int)evaluate_expression(input);
            int n2 = -1;
            if (ft == TOKEN_MID) {
                const char *comma_saved = *input;
                if (get_next_token(input).type == TOKEN_COMMA) n2 = (int)evaluate_expression(input);
                else *input = comma_saved;
            }
            get_next_token(input); // )
            int slen = (int)strlen(base);
            if (ft == TOKEN_LEFT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                strncpy(term, base, cnt); term[cnt] = '\0';
            } else if (ft == TOKEN_RIGHT) {
                int cnt = (n1 < 0) ? 0 : (n1 > slen ? slen : n1);
                strncpy(term, base + (slen - cnt > 0 ? slen - cnt : 0), cnt); term[cnt] = '\0';
            } else { // MID
                int start = n1 - 1; if (start < 0) start = 0;
                if (start >= slen) term[0] = '\0';
                else {
                    int cnt = (n2 == -1) ? (slen - start) : n2;
                    if (cnt < 0) cnt = 0; if (start + cnt > slen) cnt = slen - start;
                    strncpy(term, base + start, cnt); term[cnt] = '\0';
                }
            }
        } else if (t.type == TOKEN_SPACE || t.type == TOKEN_SPC) {
            get_next_token(input); // (
            int n = (int)evaluate_expression(input);
            get_next_token(input); // )
            if (n < 0) n = 0; if (n > 255) n = 255;
            memset(term, ' ', n); term[n] = '\0';
        } else if (t.type == TOKEN_STRING_FUNC) {
            get_next_token(input); // (
            int n = (int)evaluate_expression(input);
            get_next_token(input); // ,
            char c = ' ';
            char arg_buf[BASIC_STRING_MAX] = "";
            const char *saved_arg = *input;
            if (parse_string_expression(input, arg_buf, sizeof(arg_buf))) {
                if (arg_buf[0]) c = arg_buf[0];
            } else {
                *input = saved_arg;
                c = (char)evaluate_expression(input);
            }
            get_next_token(input); // )
            if (n < 0) n = 0; if (n > 255) n = 255;
            memset(term, c, n); term[n] = '\0';
        } else if (t.type == TOKEN_DEFLATE || t.type == TOKEN_INFLATE) {
            get_next_token(input); // (
            char source[BASIC_STRING_MAX] = "";
            parse_string_expression(input, source, sizeof(source));
            get_next_token(input); // )
            if (t.type == TOKEN_DEFLATE) {
                deflate_string(source, term, sizeof(term));
            } else {
                inflate_string(source, term, sizeof(term));
            }
        } else if (t.type == TOKEN_COMMANDS) {
            strncpy(term, internal_command_line, sizeof(term) - 1);
            term[sizeof(term) - 1] = '\0';
        } else if (t.type == TOKEN_ARGVS) {
            get_next_token(input); // (
            int idx = (int)evaluate_expression(input);
            get_next_token(input); // )
            if (idx >= 0 && idx < internal_argc && internal_argv) {
                strncpy(term, internal_argv[idx], sizeof(term) - 1);
                term[sizeof(term) - 1] = '\0';
            }
        } else if (t.type == TOKEN_GETS) {
            get_next_token(input); // (
            int fnum = -1;
            const char *saved_num = *input;
            Token hash_tok = get_next_token(input);
            if (hash_tok.type == TOKEN_HASH) {
                fnum = (int)evaluate_expression(input);
            } else {
                *input = saved_num;
                fnum = (int)evaluate_expression(input);
            }
            get_next_token(input); // ,
            int rec = (int)evaluate_expression(input);
            get_next_token(input); // ,
            int len = (int)evaluate_expression(input);
            get_next_token(input); // )
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
            strncpy(term, buf, sizeof(term) - 1);
            term[sizeof(term) - 1] = '\0';
        } else if (t.type == TOKEN_INKEY) {
            if (graphics_is_active()) {
                update_graphics();
                int c = get_graphics_char();
                if (c) {
                    term[0] = (char)c;
                    term[1] = '\0';
                }
            } else {
                int c = get_stdin_char();
                if (c) {
                    term[0] = (char)c;
                    term[1] = '\0';
                }
            }
        } else if (t.type == TOKEN_UCASE || t.type == TOKEN_LCASE || t.type == TOKEN_TRIM || t.type == TOKEN_LTRIM || t.type == TOKEN_RTRIM) {
            TokenType ft = t.type;
            get_next_token(input); // (
            char base[BASIC_STRING_MAX] = "";
            parse_string_expression(input, base, sizeof(base));
            get_next_token(input); // )
            if (ft == TOKEN_UCASE) {
                for (int i = 0; base[i]; i++) base[i] = toupper((unsigned char)base[i]);
            } else if (ft == TOKEN_LCASE) {
                for (int i = 0; base[i]; i++) base[i] = tolower((unsigned char)base[i]);
            } else if (ft == TOKEN_TRIM) {
                int start = 0, end = (int)strlen(base) - 1;
                while (start <= end && isspace((unsigned char)base[start])) start++;
                while (end >= start && isspace((unsigned char)base[end])) end--;
                int len = (end >= start) ? (end - start + 1) : 0;
                memmove(base, base + start, len);
                base[len] = '\0';
            } else if (ft == TOKEN_LTRIM) {
                int start = 0;
                while (base[start] && isspace((unsigned char)base[start])) start++;
                if (start > 0) {
                    int len = (int)strlen(base + start);
                    memmove(base, base + start, len + 1);
                }
            } else if (ft == TOKEN_RTRIM) {
                int end = (int)strlen(base) - 1;
                while (end >= 0 && isspace((unsigned char)base[end])) end--;
                base[end + 1] = '\0';
            }
            strncpy(term, base, sizeof(term) - 1);
            term[sizeof(term) - 1] = '\0';
        } else if (t.type == TOKEN_HEX || t.type == TOKEN_OCT) {
            get_next_token(input); // (
            double val = evaluate_expression(input);
            get_next_token(input); // )
            int intval = (int)val;
            if (t.type == TOKEN_HEX) snprintf(term, sizeof(term), "%X", intval);
            else snprintf(term, sizeof(term), "%o", intval);
        } else if (t.type == TOKEN_TIME || t.type == TOKEN_DATE) {
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            if (t.type == TOKEN_TIME) {
                strftime(term, sizeof(term), "%H:%M:%S", timeinfo);
            } else {
                strftime(term, sizeof(term), "%m-%d-%Y", timeinfo);
            }
        } else if (t.type == TOKEN_ENVIRON) {
            get_next_token(input); // (
            char arg_val[BASIC_STRING_MAX] = "";
            int is_str = parse_string_expression(input, arg_val, sizeof(arg_val));
            if (is_str) {
                char *ev = getenv(arg_val);
                if (ev) strncpy(term, ev, sizeof(term)-1);
            } else {
                int idx = atoi(arg_val);
                extern char **environ;
                if (environ && idx > 0) {
                    int count = 1;
                    for (char **e = environ; *e; e++, count++) {
                        if (count == idx) {
                            strncpy(term, *e, sizeof(term)-1);
                            break;
                        }
                    }
                }
            }
            get_next_token(input); // )
        } else if (t.type == TOKEN_STR) {
            get_next_token(input); // (
            double val = evaluate_expression(input);
            get_next_token(input); // )
            if (val >= 0) snprintf(term, sizeof(term), " %g", val);
            else snprintf(term, sizeof(term), "%g", val);
        } else if (t.type == TOKEN_MKI || t.type == TOKEN_MKS || t.type == TOKEN_MKD) {
            get_next_token(input); // (
            double val = evaluate_expression(input);
            get_next_token(input); // )
            if (t.type == TOKEN_MKI) {
                int ival = (int)val;
                term[0] = (char)(ival & 0xFF);
                term[1] = (char)((ival >> 8) & 0xFF);
                term[2] = '\0';
            } else if (t.type == TOKEN_MKS) {
                float fval = (float)val;
                memcpy(term, &fval, 4);
                term[4] = '\0';
            } else {
                double dval = val;
                memcpy(term, &dval, 8);
                term[8] = '\0';
            }
        } else if (t.type == TOKEN_STRING) {
            strncpy(term, t.text, sizeof(term) - 1);
        } else if (t.type == TOKEN_IDENTIFIER && is_string_var(t.text)) {
            int idx = find_variable(t.text);
            int array_idx = parse_array_index(input, idx);
            char temp[BASIC_STRING_MAX] = "";
            get_string_variable_value(idx, array_idx, temp, sizeof(temp));
            strncpy(term, temp, sizeof(term) - 1);
        } else {
            if (first) {
                *input = saved;
                double val = evaluate_expression(input);
                snprintf(out, out_size, "%g", val);
                return 0;
            }
            *input = saved;
            break;
        }

        append_string_value(out, term, out_size);
        first = 0;

        const char *sep_saved = *input;
        if (get_next_token(input).type != TOKEN_PLUS) {
            *input = sep_saved;
            break;
        }
    }
    return 1;
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
        if (vars[idx].s_array[array_idx]) free(vars[idx].s_array[array_idx]);
        vars[idx].s_array[array_idx] = strdup(value);
    } else {
        if (vars[idx].s_value) free(vars[idx].s_value);
        vars[idx].s_value = strdup(value);
    }
}

static void set_string_variable(int idx, int array_idx, const char *value) {
    set_string_variable_with_align(idx, array_idx, value, 0);
}

static void trim_string(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) *end-- = '\0';
}

static void assign_input_value(int idx, int array_idx, int is_string, const char *value) {
    if (is_string) {
        if (array_idx >= 0 && vars[idx].s_array && array_idx < vars[idx].array_size) {
            if (vars[idx].s_array[array_idx]) free(vars[idx].s_array[array_idx]);
            vars[idx].s_array[array_idx] = strdup(value);
        } else {
            if (vars[idx].s_value) free(vars[idx].s_value);
            vars[idx].s_value = strdup(value);
        }
    } else {
        set_numeric_variable(idx, array_idx, atof(value));
    }
}

static void apply_basika_using(const char *fmt, double val, char *out, int out_size) {
    if (strchr(fmt, '%')) {
        snprintf(out, out_size, fmt, val);
        return;
    }
    int hashes_before = 0, hashes_after = 0, has_dot = 0;
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '#') { if (has_dot) hashes_after++; else hashes_before++; }
        else if (fmt[i] == '.') has_dot = 1;
    }
    if (has_dot) snprintf(out, out_size, "%*.*f", hashes_before + hashes_after + 1, hashes_after, val);
    else if (hashes_before > 0) snprintf(out, out_size, "%*d", hashes_before, (int)val);
    else snprintf(out, out_size, "%g", val);
}

static void apply_basika_using_str(const char *fmt, const char *val, char *out, int out_size) {
    if (strchr(fmt, '%')) {
        snprintf(out, out_size, fmt, val);
        return;
    }
    if (strcmp(fmt, "!") == 0) {
        snprintf(out, out_size, "%.1s", val);
    } else if (strcmp(fmt, "&") == 0) {
        snprintf(out, out_size, "%s", val);
    } else {
        snprintf(out, out_size, "%s", val);
    }
}

static void execute_assignment(const char **input, Token var_token) {
    int idx = find_variable(var_token.text);
    int array_idx = parse_array_index(input, idx);
    const char *saved = *input;
    Token eq = get_next_token(input);
    if (eq.type != TOKEN_EQUALS) {
        *input = saved;
        report_runtime_error(ERR_SYNTAX_ERROR);
        return;
    }

    if (is_string_var(var_token.text)) {
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
        set_numeric_variable(idx, array_idx, evaluate_expression(input));
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
                free(vars[i].s_array[j]);
            }
            free(vars[i].s_array);
            vars[i].s_array = NULL;
        }
        if (vars[i].s_value) {
            free(vars[i].s_value);
            vars[i].s_value = NULL;
        }
        vars[i].array_size = 0;
        vars[i].num_dims = 0;
        vars[i].value = 0;
        if (!keep_registry) vars[i].name[0] = '\0';
    }
    if (!keep_registry) {
        var_count = 0;
        for (int i = 0; i < 2048; i++) var_hash_table[i] = -1;
    }

    user_function_count = 0;
    option_base = 0;
    option_base_set = 0;
    arrays_dimensioned = 0;
    for (int i = 0; i < 26; i++) default_type_map[i] = '!';
}

void basic_output(const char *text) { // Made non-static
    if (!graphics_is_active()) {
        printf("%s", text);
    }
    graphics_print(text);
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n' || text[i] == '\r') {
            print_col = 0;
        } else {
            print_col++;
        }
    }
}

// Forward declarations for recursive descent expression parser
double evaluate_expression(const char **input);
static double primary(const char **input);

static int is_string_token(Token t) {
    if (t.type == TOKEN_STRING) return 1;
    if (t.type == TOKEN_IDENTIFIER && is_string_var(t.text)) return 1;
    if (t.text[0] != '\0' && t.text[strlen(t.text)-1] == '$') return 1;
    return (t.type == TOKEN_CHR || t.type == TOKEN_LEFT || t.type == TOKEN_RIGHT ||
            t.type == TOKEN_MID || t.type == TOKEN_UCASE || t.type == TOKEN_LCASE ||
            t.type == TOKEN_TRIM || t.type == TOKEN_LTRIM || t.type == TOKEN_RTRIM ||
            t.type == TOKEN_STR || t.type == TOKEN_HEX || t.type == TOKEN_OCT ||
            t.type == TOKEN_MKI || t.type == TOKEN_MKS || t.type == TOKEN_MKD ||
            t.type == TOKEN_STRING_FUNC || t.type == TOKEN_DEFLATE || t.type == TOKEN_INFLATE || t.type == TOKEN_INKEY || t.type == TOKEN_GETS || t.type == TOKEN_ENVIRON ||
            t.type == TOKEN_TIME || t.type == TOKEN_DATE || t.type == TOKEN_TAB ||
            t.type == TOKEN_SPACE || t.type == TOKEN_SPC ||
            t.type == TOKEN_ARGVS || t.type == TOKEN_COMMANDS);
}

static double primary_tok(TokenStream *ts) {
    Token t = ts->tokens[ts->pos++];
    if (t.type == TOKEN_NUMBER) {
        if (t.is_double) last_expression_is_double = 1;
        return t.double_val;
    }
    if (t.type == TOKEN_ARGC) return (double)internal_argc;
    if (t.type == TOKEN_IDENTIFIER) {
        if (strcasecmp(t.text, "ERR") == 0) return (double)last_runtime_error_code;
        if (strcasecmp(t.text, "ERL") == 0) return (double)last_runtime_error_line;

        if (strncasecmp(t.text, "FN", 2) == 0) {
            if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
                ts->pos++; // (
                for (int i = 0; i < user_function_count; i++) {
                    if (strcasecmp(user_functions[i].name, t.text) == 0) {
                        double arg_val = evaluate_expression_tok(ts);
                        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++; // )
                        
                        int p_idx = find_variable(user_functions[i].param_name);
                        double old_val = vars[p_idx].value;
                        vars[p_idx].value = arg_val;
                        const char *expr_ptr = user_functions[i].expression;
                        double result = evaluate_expression(&expr_ptr);
                        vars[p_idx].value = old_val;
                        return result;
                    }
                }
            }
        }

        int idx = t.var_idx;
        if (idx == -1) idx = find_variable(t.text);
        
        if (idx != -1) {
            const char *v_name = vars[idx].name;
            if (v_name[strlen(v_name)-1] == '#') last_expression_is_double = 1;
        }

        int array_idx = parse_array_index_tok(ts, idx);
        if (array_idx >= 0) {
             return (vars[idx].array && array_idx < vars[idx].array_size) ? vars[idx].array[array_idx] : 0;
        }
        return (idx != -1) ? vars[idx].value : 0;
    }
    if (t.type == TOKEN_LPAREN) {
        double val = evaluate_expression_tok(ts);
        if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        return val;
    }

    if (t.type == TOKEN_LOADFONT) {
        /* _LOADFONT(filename$, size%) -> handle */
        if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
            ts->pos++;
            char buf[BASIC_STRING_MAX] = "";
            parse_string_expression_tok(ts, buf, sizeof(buf));
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) ts->pos++;
            int size = (int)evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (!graphics_is_active()) init_graphics();
            fprintf(stderr, "DEBUG _LOADFONT: buf='%s' size=%d\n", buf, size);
            int result = graphics_loadfont(buf, size);
            fprintf(stderr, "DEBUG _LOADFONT: result=%d\n", result);
            return (double)result;
        }
        return 0;
    }
    if (t.type == TOKEN_LOADIMAGE) {
        /* _LOADIMAGE(filename$, mode&) -> handle */
        if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
            ts->pos++;
            char buf[BASIC_STRING_MAX] = "";
            parse_string_expression_tok(ts, buf, sizeof(buf));
            int mode = 32;
            if (ts->tokens[ts->pos].type == TOKEN_COMMA) {
                ts->pos++;
                mode = (int)evaluate_expression_tok(ts);
            }
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
            if (!graphics_is_active()) init_graphics();
            return (double)graphics_loadimage(buf, mode);
        }
        return 0;
    }
    if ((t.type >= TOKEN_ABS && t.type <= TOKEN_SGN) || t.type == TOKEN_EOF_FUNC || t.type == TOKEN_TIMER || t.type == TOKEN_KEY || t.type == TOKEN_STRIG || t.type == TOKEN_ASC || t.type == TOKEN_LEN || t.type == TOKEN_INSTR || t.type == TOKEN_VAL || t.type == TOKEN_PEEK || t.type == TOKEN_VARPTR || t.type == TOKEN_LOF || t.type == TOKEN_LOC || t.type == TOKEN_CVI || t.type == TOKEN_CVS || t.type == TOKEN_CVD || t.type == TOKEN_PRINTWIDTH) {
        TokenType ft = t.type;
        int has_arg = 0;
        double arg = 0;

        if (ts->tokens[ts->pos].type == TOKEN_LPAREN) {
            ts->pos++;
            has_arg = 1;
            if (ft == TOKEN_VARPTR) {
                Token var_tok = ts->tokens[ts->pos++];
                int var_idx = -1;
                int array_idx = -1;
                if (var_tok.type == TOKEN_IDENTIFIER) {
                    var_idx = var_tok.var_idx;
                    if (var_idx == -1) var_idx = find_variable(var_tok.text);
                    array_idx = parse_array_index_tok(ts, var_idx);
                }
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                if (var_idx < 0) return 0;
                if (array_idx >= 0) return 61440.0 + var_idx * 256.0 + array_idx;
                return 61440.0 + var_idx * 256.0;
            }
            if (ft == TOKEN_CVI || ft == TOKEN_CVS || ft == TOKEN_CVD) {
                char buf[BASIC_STRING_MAX] = "";
                int len = 0;
                Token arg_tok = ts->tokens[ts->pos];
                if (arg_tok.type == TOKEN_IDENTIFIER && is_string_var(arg_tok.text)) {
                    int idx = arg_tok.var_idx;
                    if (idx == -1) idx = find_variable(arg_tok.text);
                    ts->pos++;
                    int array_idx = parse_array_index_tok(ts, idx);
                    len = get_string_variable_raw(idx, array_idx, buf, sizeof(buf));
                    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                } else {
                    parse_string_expression_tok(ts, buf, sizeof(buf));
                    len = (int)strlen(buf);
                    if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                }
                if (ft == TOKEN_CVI) {
                    int16_t val = 0;
                    int cpy = (len > 2) ? 2 : len;
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
                char buf[BASIC_STRING_MAX] = "";
                parse_string_expression_tok(ts, buf, sizeof(buf));
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                if (ft == TOKEN_ASC) return buf[0] ? (double)(unsigned char)buf[0] : 0;
                if (ft == TOKEN_LEN) return (double)strlen(buf);
                if (ft == TOKEN_VAL) return atof(buf);
                if (ft == TOKEN_PRINTWIDTH) return (double)graphics_printwidth(buf);
            }
            if (ft == TOKEN_INSTR) {
                char s1[BASIC_STRING_MAX] = "";
                char s2[BASIC_STRING_MAX] = "";
                int start = 1;
                Token next = ts->tokens[ts->pos];
                if (next.type != TOKEN_STRING && next.type != TOKEN_IDENTIFIER) {
                    start = (int)evaluate_expression_tok(ts);
                    ts->pos++; // comma
                }
                parse_string_expression_tok(ts, s1, sizeof(s1));
                ts->pos++; // comma
                parse_string_expression_tok(ts, s2, sizeof(s2));
                if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
                if (start < 1) return 0;
                char *pos = strstr(s1 + start - 1, s2);
                return pos ? (double)(pos - s1 + 1) : 0;
            }
            arg = evaluate_expression_tok(ts);
            if (ts->tokens[ts->pos].type == TOKEN_RPAREN) ts->pos++;
        }

        switch (ft) {
            case TOKEN_ABS: return fabs(arg);
            case TOKEN_SQR: return sqrt(arg);
            case TOKEN_SIN: return sin(arg);
            case TOKEN_COS: return cos(arg);
            case TOKEN_TAN: return tan(arg);
            case TOKEN_ATN: return atan(arg);
            case TOKEN_EXP: return exp(arg);
            case TOKEN_LOG: return log(arg);
            case TOKEN_INT: return floor(arg);
            case TOKEN_FIX: return (arg >= 0) ? floor(arg) : ceil(arg);
            case TOKEN_SGN: return (arg > 0) - (arg < 0);
            case TOKEN_EOF_FUNC: {
                int fnum = (int)arg;
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                int c = fgetc(file_handles[fnum]);
                if (c == EOF) return 1;
                ungetc(c, file_handles[fnum]);
                return 0;
            }
            case TOKEN_LOF: {
                int fnum = (int)arg;
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
                int fnum = (int)arg;
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                return (double)ftell(file_handles[fnum]);
            }
            case TOKEN_TIMER: return (double)clock() / CLOCKS_PER_SEC;
            case TOKEN_KEY: return (double)get_graphics_key();
            case TOKEN_STRIG: {
                int s = (int)arg;
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
                int addr = (int)arg;
                if (addr < 0 || addr >= 65536) return 0;
                return basika_memory[addr];
            }
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

    return 0;
}

static double unary_tok(TokenStream *ts) {
    if (ts->tokens[ts->pos].type == TOKEN_PLUS) {
        ts->pos++;
        return unary_tok(ts);
    }
    if (ts->tokens[ts->pos].type == TOKEN_MINUS) {
        ts->pos++;
        return -unary_tok(ts);
    }
    return primary_tok(ts);
}

static double term_tok(TokenStream *ts) {
    double val = unary_tok(ts);
    while (1) {
        Token t = ts->tokens[ts->pos];
        if (t.type == TOKEN_STAR) { ts->pos++; val *= unary_tok(ts); }
        else if (t.type == TOKEN_SLASH) { ts->pos++; double d = unary_tok(ts); if (d != 0) val /= d; }
        else if (t.type == TOKEN_IDIV) { ts->pos++; double d = unary_tok(ts); if (d != 0) val = (long)(val / d); }
        else if (t.type == TOKEN_MOD) { ts->pos++; double d = unary_tok(ts); if (d != 0) val = (long)val % (long)d; }
        else break;
    }
    return val;
}

static double arithmetic_expression_tok(TokenStream *ts) {
    double val = term_tok(ts);
    while (1) {
        Token t = ts->tokens[ts->pos];
        if (t.type == TOKEN_PLUS) { ts->pos++; val += term_tok(ts); }
        else if (t.type == TOKEN_MINUS) { ts->pos++; val -= term_tok(ts); }
        else break;
    }
    return val;
}

static double relational_expression_tok(TokenStream *ts) {
    double val = arithmetic_expression_tok(ts);
    while (1) {
        Token t = ts->tokens[ts->pos];
        if (t.type == TOKEN_EQUALS) {
            ts->pos++;
            val = (val == arithmetic_expression_tok(ts)) ? -1.0 : 0.0;
        }
        else if (t.type == TOKEN_LESS) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_GREATER) {
                ts->pos++;
                val = (val != arithmetic_expression_tok(ts)) ? -1.0 : 0.0;
            } else if (ts->tokens[ts->pos].type == TOKEN_EQUALS) {
                ts->pos++;
                val = (val <= arithmetic_expression_tok(ts)) ? -1.0 : 0.0;
            } else {
                val = (val < arithmetic_expression_tok(ts)) ? -1.0 : 0.0;
            }
        }
        else if (t.type == TOKEN_GREATER) {
            ts->pos++;
            if (ts->tokens[ts->pos].type == TOKEN_EQUALS) {
                ts->pos++;
                val = (val >= arithmetic_expression_tok(ts)) ? -1.0 : 0.0;
            } else {
                val = (val > arithmetic_expression_tok(ts)) ? -1.0 : 0.0;
            }
        }
        else break;
    }
    return val;
}

static double logical_not_tok(TokenStream *ts) {
    if (ts->tokens[ts->pos].type == TOKEN_NOT) {
        ts->pos++;
        return (double)(~(short)logical_not_tok(ts));
    }
    return relational_expression_tok(ts);
}

static double bitwise_and_tok(TokenStream *ts) {
    double val = logical_not_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_AND) {
        ts->pos++;
        val = (double)((short)val & (short)logical_not_tok(ts));
    }
    return val;
}

static double bitwise_or_tok(TokenStream *ts) {
    double val = bitwise_and_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_OR) {
        ts->pos++;
        val = (double)((short)val | (short)bitwise_and_tok(ts));
    }
    return val;
}

double evaluate_expression_tok(TokenStream *ts) {
    if (ts->tokens[ts->pos].type != TOKEN_EOF && is_string_token(ts->tokens[ts->pos])) {
        const char *p = ts->tokens[ts->pos].start_ptr;
        double val = evaluate_expression(&p);
        // Advance token position to match string pointer
        while (ts->tokens[ts->pos].type != TOKEN_EOF && ts->tokens[ts->pos].start_ptr < p) {
            ts->pos++;
        }
        return val;
    }

    double val = bitwise_or_tok(ts);
    while (ts->tokens[ts->pos].type == TOKEN_XOR) {
        ts->pos++;
        val = (double)((short)val ^ (short)bitwise_or_tok(ts));
    }
    return val;
}

static double primary(const char **input) {
    Token t = get_next_token(input);
    if (t.type == TOKEN_NUMBER) {
        if (t.is_double) last_expression_is_double = 1;
        return t.double_val;
    }
    if (t.type == TOKEN_ARGC) return (double)internal_argc;
    if (t.type == TOKEN_IDENTIFIER) {
        if (strcasecmp(t.text, "ERR") == 0) return (double)last_runtime_error_code;
        if (strcasecmp(t.text, "ERL") == 0) return (double)last_runtime_error_line;

        if (strncasecmp(t.text, "FN", 2) == 0) {
            const char *lp_ptr = *input;
            if (get_next_token(&lp_ptr).type == TOKEN_LPAREN) {
                for (int i = 0; i < user_function_count; i++) {
                    if (strcasecmp(user_functions[i].name, t.text) == 0) {
                        *input = lp_ptr;
                        double arg_val = evaluate_expression(input);
                        get_next_token(input); // consume ')'
                        
                        int p_idx = find_variable(user_functions[i].param_name);
                        double old_val = vars[p_idx].value;
                        vars[p_idx].value = arg_val;
                        const char *expr_ptr = user_functions[i].expression;
                        double result = evaluate_expression(&expr_ptr);
                        vars[p_idx].value = old_val;
                        return result;
                    }
                }
            }
        }
        int idx = find_variable(t.text);
        if (idx != -1) {
            const char *v_name = vars[idx].name;
            if (v_name[strlen(v_name)-1] == '#') last_expression_is_double = 1;
        }
        const char *look_ptr = *input; // Peek to see if it's an array access
        Token next = get_next_token(&look_ptr);
        if (next.type == TOKEN_LPAREN) {
            int array_idx = parse_array_index(input, idx);
            if (vars[idx].s_array && array_idx >= 0 && array_idx < vars[idx].array_size) {
                // This is a string array, evaluate_expression currently only handles doubles.
                // We return 0 here as fallback. String expressions are handled in statements.
                return 0;
            }
            if (vars[idx].array && array_idx >= 0 && array_idx < vars[idx].array_size) {
                return vars[idx].array[array_idx];
            }
            return 0;
        }
        return (idx != -1) ? vars[idx].value : 0;
    }
    if (t.type == TOKEN_LPAREN) {
        double val = evaluate_expression(input);
        get_next_token(input); // consume ')'
        return val;
    }
    if (t.type == TOKEN_LOADFONT) {
        /* _LOADFONT(filename$, size%) -> handle */
        Token next = get_next_token(input);
        if (next.type == TOKEN_LPAREN) {
            char buf[BASIC_STRING_MAX] = "";
            parse_string_expression(input, buf, sizeof(buf));
            get_next_token(input); // consume comma
            int size = (int)evaluate_expression(input);
            get_next_token(input); // consume ')'
            if (!graphics_is_active()) init_graphics();
            fprintf(stderr, "DEBUG _LOADFONT(2): buf='%s' size=%d\n", buf, size);
            int result2 = graphics_loadfont(buf, size);
            fprintf(stderr, "DEBUG _LOADFONT(2): result=%d\n", result2);
            return (double)result2;
        }
        return 0;
    }
    if (t.type == TOKEN_LOADIMAGE) {
        /* _LOADIMAGE(filename$, mode&) -> handle */
        Token next = get_next_token(input);
        if (next.type == TOKEN_LPAREN) {
            char buf[BASIC_STRING_MAX] = "";
            parse_string_expression(input, buf, sizeof(buf));
            int mode = 32;
            Token separator = get_next_token(input);
            if (separator.type == TOKEN_COMMA) mode = (int)evaluate_expression(input);
            get_next_token(input); // consume ')'
            if (!graphics_is_active()) init_graphics();
            return (double)graphics_loadimage(buf, mode);
        }
        return 0;
    }
    if ((t.type >= TOKEN_ABS && t.type <= TOKEN_SGN) || t.type == TOKEN_EOF_FUNC || t.type == TOKEN_TIMER || t.type == TOKEN_KEY || t.type == TOKEN_STRIG || t.type == TOKEN_ASC || t.type == TOKEN_LEN || t.type == TOKEN_INSTR || t.type == TOKEN_VAL || t.type == TOKEN_PEEK || t.type == TOKEN_VARPTR || t.type == TOKEN_LOF || t.type == TOKEN_LOC || t.type == TOKEN_CVI || t.type == TOKEN_CVS || t.type == TOKEN_CVD || t.type == TOKEN_PRINTWIDTH) {
        TokenType ft = t.type;
        const char *saved = *input;
        Token next = get_next_token(input);
        double arg = 0;
        int has_arg = 0;
        if (next.type == TOKEN_LPAREN) {
            has_arg = 1;
            if (ft == TOKEN_VARPTR) {
                Token var_tok = get_next_token(input);
                int var_idx = -1;
                int array_idx = -1;
                if (var_tok.type == TOKEN_IDENTIFIER) {
                    var_idx = find_variable(var_tok.text);
                    array_idx = parse_array_index(input, var_idx);
                }
                get_next_token(input); // consume ')'
                if (var_idx < 0) return 0;
                if (array_idx >= 0) return 61440.0 + var_idx * 256.0 + array_idx;
                return 61440.0 + var_idx * 256.0;
            }
            if (ft == TOKEN_CVI || ft == TOKEN_CVS || ft == TOKEN_CVD) {
                char buf[BASIC_STRING_MAX] = "";
                int len = 0;
                const char *saved_tok = *input;
                Token arg_tok = get_next_token(input);
                if (arg_tok.type == TOKEN_IDENTIFIER && is_string_var(arg_tok.text)) {
                    int idx = find_variable(arg_tok.text);
                    int array_idx = parse_array_index(input, idx);
                    len = get_string_variable_raw(idx, array_idx, buf, sizeof(buf));
                    get_next_token(input); // consume ')'
                } else {
                    *input = saved_tok;
                    parse_string_expression(input, buf, sizeof(buf));
                    len = (int)strlen(buf);
                    get_next_token(input); // consume ')'
                }
                if (ft == TOKEN_CVI) {
                    int16_t val = 0;
                    int cpy = (len > 2) ? 2 : len;
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
                char buf[BASIC_STRING_MAX] = "";
                parse_string_expression(input, buf, sizeof(buf));
                get_next_token(input); // consume ')'
                if (ft == TOKEN_ASC) return buf[0] ? (double)(unsigned char)buf[0] : 0;
                if (ft == TOKEN_LEN) return (double)strlen(buf);
                if (ft == TOKEN_VAL) return atof(buf);
                if (ft == TOKEN_PRINTWIDTH) return (double)graphics_printwidth(buf);
            }
            if (ft == TOKEN_INSTR) {
                char s1[BASIC_STRING_MAX] = "";
                char s2[BASIC_STRING_MAX] = "";
                int start = 1;
                const char *saved_instr = *input;
                Token first_arg = get_next_token(input);
                if (first_arg.type != TOKEN_STRING && first_arg.type != TOKEN_IDENTIFIER) {
                    *input = saved_instr;
                    start = (int)evaluate_expression(input);
                    get_next_token(input); // consume comma
                } else {
                    *input = saved_instr;
                }
                parse_string_expression(input, s1, sizeof(s1));
                get_next_token(input); // consume comma
                parse_string_expression(input, s2, sizeof(s2));
                get_next_token(input); // consume ')'
                if (start < 1) return 0;
                char *pos = strstr(s1 + start - 1, s2);
                return pos ? (double)(pos - s1 + 1) : 0;
            }
            arg = evaluate_expression(input);
            get_next_token(input); // consume ')'
        } else {
            *input = saved;
        }
        switch((int)ft) {
            case TOKEN_ABS: return fabs(arg);
            case TOKEN_SQR: return sqrt(arg);
            case TOKEN_SIN: return sin(arg);
            case TOKEN_COS: return cos(arg);
            case TOKEN_TAN: return tan(arg);
            case TOKEN_ATN: return atan(arg);
            case TOKEN_EXP: return exp(arg);
            case TOKEN_LOG: return log(arg);
            case TOKEN_INT: return floor(arg);
            case TOKEN_FIX: return (double)(long)arg;
            case TOKEN_SGN: return (arg > 0) - (arg < 0);
            case TOKEN_EOF_FUNC: {
                int fnum = (int)arg;
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                int c = fgetc(file_handles[fnum]);
                    if (c == EOF) return 1;
                    ungetc(c, file_handles[fnum]);
                    return 0;
                }
            case TOKEN_LOF: {
                int fnum = (int)arg;
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
                int fnum = (int)arg;
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                    return 0;
                }
                return (double)ftell(file_handles[fnum]);
            }
            case TOKEN_TIMER: return (double)clock() / CLOCKS_PER_SEC;
            case TOKEN_KEY: return (double)get_graphics_key();
            case TOKEN_STRIG: {
                int s = (int)arg;
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
                int addr = (int)arg;
                if (addr < 0 || addr >= 65536) return 0;
                return basika_memory[addr];
            }
            case TOKEN_RND:
                if (has_arg && arg < 0) {
                    // RND with negative argument seeds the generator deterministically
                    rnd_seed = (unsigned int)(-(long)arg);
                    return basika_rnd_next();
                }
                if (has_arg && arg == 0) return last_rnd_value;
                return basika_rnd_next();
            default: return 0;
        }
    }
    return 0;
}

static double power_op(const char **input) {
    double val = primary(input);
    const char *saved = *input;
    Token t = get_next_token(input);
    if (t.type == TOKEN_POWER) return pow(val, power_op(input));
    *input = saved;
    return val;
}

static double unary(const char **input) {
    const char *saved = *input;
    Token t = get_next_token(input);
    if (t.type == TOKEN_PLUS) return unary(input);
    if (t.type == TOKEN_MINUS) return -unary(input);
    *input = saved;
    return power_op(input);
}

static double term(const char **input) {
    double val = unary(input);
    while (1) {
        const char *saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_STAR) val *= unary(input);
        else if (t.type == TOKEN_SLASH) { double d = unary(input); if (d != 0) val /= d; }
        else if (t.type == TOKEN_MOD) { double d = unary(input); if (d != 0) val = (long)val % (long)d; }
        else if (t.type == TOKEN_IDIV) { double d = unary(input); if (d != 0) val = (long)(val / d); }
        else { *input = saved; break; }
    }
    return val;
}

static double arithmetic_expression(const char **input) {
    double val = term(input);
    while (1) {
        const char *saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_PLUS) val += term(input);
        else if (t.type == TOKEN_MINUS) val -= term(input);
        else { *input = saved; break; }
    }
    return val;
}

static double relational_expression(const char **input) {
    double val = arithmetic_expression(input);
    while (1) {
        const char *saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_EQUALS) val = (val == arithmetic_expression(input)) ? -1.0 : 0.0;
        else if (t.type == TOKEN_LESS) {
            const char *s2 = *input;
            Token t2 = get_next_token(input);
            if (t2.type == TOKEN_GREATER) val = (val != arithmetic_expression(input)) ? -1.0 : 0.0;
            else if (t2.type == TOKEN_EQUALS) val = (val <= arithmetic_expression(input)) ? -1.0 : 0.0;
            else { *input = s2; val = (val < arithmetic_expression(input)) ? -1.0 : 0.0; }
        }
        else if (t.type == TOKEN_GREATER) {
            const char *s2 = *input;
            Token t2 = get_next_token(input);
            if (t2.type == TOKEN_EQUALS) val = (val >= arithmetic_expression(input)) ? -1.0 : 0.0;
            else { *input = s2; val = (val > arithmetic_expression(input)) ? -1.0 : 0.0; }
        }
        else { *input = saved; break; }
    }
    return val;
}

static double logical_not_expression(const char **input) {
    const char *saved = *input;
    Token t = get_next_token(input);
    if (t.type == TOKEN_NOT) {
        return (double)(~(short)logical_not_expression(input));
    }
    *input = saved;
    return relational_expression(input);
}

static double bitwise_and_expression(const char **input) {
    double val = logical_not_expression(input);
    while (1) {
        const char *saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_AND) val = (double)((short)val & (short)logical_not_expression(input));
        else { *input = saved; break; }
    }
    return val;
}

static double bitwise_or_expression(const char **input) {
    double val = bitwise_and_expression(input);
    while (1) {
        const char *saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_OR) val = (double)((short)val | (short)bitwise_and_expression(input));
        else { *input = saved; break; }
    }
    return val;
}

double evaluate_expression(const char **input) {
    const char *peek_ptr = *input;
    Token peek_tok = get_next_token(&peek_ptr);
    if (is_string_token(peek_tok)) {
        char s1[BASIC_STRING_MAX], s2[BASIC_STRING_MAX];
        parse_string_expression(input, s1, sizeof(s1));
        const char *op_saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_EQUALS || t.type == TOKEN_LESS || t.type == TOKEN_GREATER) {
            int op1 = t.type;
            const char *saved2 = *input;
            Token t2 = get_next_token(input);
            int op2 = -1;
            if (t2.type == TOKEN_EQUALS || t2.type == TOKEN_GREATER) op2 = t2.type;
            else *input = saved2;

            parse_string_expression(input, s2, sizeof(s2));
            int res = strcmp(s1, s2);
            if (op1 == TOKEN_EQUALS) return res == 0 ? -1.0 : 0.0;
            if (op1 == TOKEN_LESS) {
                if (op2 == TOKEN_GREATER) return res != 0 ? -1.0 : 0.0;
                if (op2 == TOKEN_EQUALS) return res <= 0 ? -1.0 : 0.0;
                return res < 0 ? -1.0 : 0.0;
            }
            if (op1 == TOKEN_GREATER) {
                if (op2 == TOKEN_EQUALS) return res >= 0 ? -1.0 : 0.0;
                return res > 0 ? -1.0 : 0.0;
            }
        }
        *input = op_saved;
        return 0;
    }

    double val = bitwise_or_expression(input);
    while (1) {
        const char *saved = *input;
        Token t = get_next_token(input);
        if (t.type == TOKEN_XOR) val = (double)((short)val ^ (short)bitwise_or_expression(input));
        else { *input = saved; break; }
    }
    return val;
}

void interpret_line_at_ptr(const char **ptr_addr, int is_direct, int *last_line_num) {
    const char *ptr = *ptr_addr;
    Token t = get_next_token(&ptr);

    if (t.type == TOKEN_EOF || t.type == TOKEN_COLON) {
        *ptr_addr = ptr;
        return;
    }

    if (is_direct) {
        if (t.type == TOKEN_NUMBER) {
            if (last_line_num) *last_line_num = t.int_val;
            add_line(t.int_val, ptr);
            return;
        } else if (last_line_num) {
            *last_line_num += 10;
            add_line(*last_line_num, *ptr_addr);
            return;
        }
    }

    {
        if (t.type == TOKEN_ON) {
            const char *saved = ptr;
            Token next = get_next_token(&ptr);
            if (next.type == TOKEN_ERR || (next.type == TOKEN_IDENTIFIER && strcasecmp(next.text, "ERROR") == 0)) {
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
            } else if (next.type == TOKEN_KEY || next.type == TOKEN_TIMER || next.type == TOKEN_STRIG) {
                Token lparen = get_next_token(&ptr);
                double n_val = 0;
                if (lparen.type == TOKEN_LPAREN) {
                    n_val = evaluate_expression(&ptr);
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
                    timer_interval = n_val;
                    on_timer_line = target_line;
                    timer_pending = 0;
                    timer_event_active = 0;
                    timer_last_trigger_time = wall_time_seconds();
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
                int k = (int)evaluate_expression(&ptr);
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
                            key_event_pending[k] = 0;
                        }
                        *ptr_addr = ptr;
                        return;
                    } else if (mode.type == TOKEN_IDENTIFIER && strcasecmp(mode.text, "STOP") == 0) {
                        if (k >= 1 && k < 32) key_state[k] = 2;
                        *ptr_addr = ptr;
                        return;
                    }
                }
            } else if (first.type == TOKEN_ON || first.type == TOKEN_OFF || (first.type == TOKEN_IDENTIFIER && strcasecmp(first.text, "STOP") == 0)) {
                *ptr_addr = ptr;
                return;
            }
            ptr = saved_key;
        } else if (t.type == TOKEN_TIMER) {
            Token mode = get_next_token(&ptr);
            if (mode.type == TOKEN_ON) {
                timer_state = 1;
                timer_pending = 0;
                timer_event_active = 0;
                timer_last_trigger_time = wall_time_seconds();
                *ptr_addr = ptr;
                return;
            } else if (mode.type == TOKEN_OFF) {
                timer_state = 0;
                timer_pending = 0;
                timer_event_active = 0;
                *ptr_addr = ptr;
                return;
            } else if (mode.type == TOKEN_IDENTIFIER && strcasecmp(mode.text, "STOP") == 0) {
                timer_state = 2;
                *ptr_addr = ptr;
                return;
            }
        } else if (t.type == TOKEN_STRIG) {
            Token mode = get_next_token(&ptr);
            if (mode.type == TOKEN_ON) {
                strig_state = 1;
                *ptr_addr = ptr;
                return;
            } else if (mode.type == TOKEN_OFF) {
                strig_state = 0;
                for (int i = 0; i < 8; i++) strig_event_pending[i] = 0;
                *ptr_addr = ptr;
                return;
            } else if (mode.type == TOKEN_IDENTIFIER && strcasecmp(mode.text, "STOP") == 0) {
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
                fnum = (int)evaluate_expression(&ptr);
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
            int last_was_numeric = 0;
            char using_fmt[256] = "";
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

                if (next.type == TOKEN_EOF || next.type == TOKEN_COLON) {
                    if (!has_args) {
                        ptr = item_saved;
                        if (fnum != -1 && file_handles[fnum]) fprintf(file_handles[fnum], "\n");
                        else basic_output("\n");
                    }
                    break;
                }
                has_args = 1;
                char val_buf[512] = "";
                if (is_string_token(next)) {
                    ptr = item_saved;
                    char value[BASIC_STRING_MAX] = "";
                    parse_string_expression(&ptr, value, sizeof(value));
                    if (using_mode) apply_basika_using_str(using_fmt, value, val_buf, sizeof(val_buf));
                    else strcpy(val_buf, value);
                    last_was_numeric = 0;
                } else {
                    ptr = item_saved;
                    last_expression_is_double = 0;
                    double val = evaluate_expression(&ptr); 
                    int prec = last_expression_is_double ? 16 : 7;

                    if (using_mode) apply_basika_using(using_fmt, val, val_buf, sizeof(val_buf));
                    else {
                        if (val >= 0) snprintf(val_buf, sizeof(val_buf), " %.*g ", prec, val);
                        else snprintf(val_buf, sizeof(val_buf), "%.*g ", prec, val);
                    }
                    last_was_numeric = 1;
                }
                if (fnum != -1 && file_handles[fnum]) {
                    fprintf(file_handles[fnum], "%s", val_buf);
                    for (int k = 0; val_buf[k]; k++) {
                        if (val_buf[k] == '\n' || val_buf[k] == '\r') print_col = 0;
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
                    } else if (last_was_numeric && !using_mode) {
                        const char *peek_ptr = ptr;
                        Token peek = get_next_token(&peek_ptr);
                        if (peek.type != TOKEN_EOF && peek.type != TOKEN_COLON && !is_string_token(peek)) {
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
                    if (fnum != -1 && file_handles[fnum]) {
                        fprintf(file_handles[fnum], "\n");
                        print_col = 0;
                    } else {
                        basic_output("\n");
                    }
                    break;
                }
            }
        } else if (t.type == TOKEN_LET || t.type == TOKEN_IDENTIFIER) {
            Token var_token = (t.type == TOKEN_LET) ? get_next_token(&ptr) : t;
            execute_assignment(&ptr, var_token);
        } else if (t.type == TOKEN_LIST && is_direct) {
            list_program();
        } else if (t.type == TOKEN_RUN && is_direct) {
            run_program();
        } else if (t.type == TOKEN_NEW && is_direct) {
            clear_program();
            clear_variables(0); // Full reset for NEW
            clear_data_pointer();
            print_col = 0;
        } else if (t.type == TOKEN_BEEP) {
            basic_output("\a");
        } else if (t.type == TOKEN_SOUND) {
            double frequency = evaluate_expression(&ptr);
            Token sep = get_next_token(&ptr);
            if (sep.type == TOKEN_COMMA) {
                double duration = evaluate_expression(&ptr);
                audio_sound(frequency, duration);
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
                    ptr += len;
                }
            }
        } else if (t.type == TOKEN_DEFINT) {
            parse_def_range(&ptr, '%');
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
            const char *saved = ptr;
            Token cmd_tok = get_next_token(&ptr);
            if (cmd_tok.type != TOKEN_EOF && cmd_tok.type != TOKEN_COLON) {
                ptr = saved;
                parse_string_expression(&ptr, cmd, sizeof(cmd));
                fflush(stdout);
                if (cmd[0] != '\0') system(cmd);
            } else {
                const char *shell_cmd = getenv("SHELL");
                fflush(stdout);
                system(shell_cmd ? shell_cmd : "sh");
            }
        } else if (t.type == TOKEN_ENVIRON) {
            char cmd[256] = "";
            parse_string_expression(&ptr, cmd, sizeof(cmd));
            if (cmd[0]) {
                putenv(strdup(cmd));
            }
        } else if (t.type == TOKEN_POKE) {
            int addr = (int)evaluate_expression(&ptr);
            const char *saved_comma = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                int val = (int)evaluate_expression(&ptr);
                // Ensure address is within bounds before poking
                if (addr >= 0 && addr < 65536) basika_memory[addr] = (unsigned char)(val & 0xFF);
            } else {
                ptr = saved_comma;
            }
        } else if ((t.type == TOKEN_SYSTEM || t.type == TOKEN_QUIT) && is_direct) {
            exit(0);
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

            if (is_str1 != is_str2) {
                report_runtime_error(ERR_TYPE_MISMATCH);
            } else if (!is_str1) {
                const char *n1 = vars[idx1].name;
                const char *n2 = vars[idx2].name;
                if (n1[strlen(n1)-1] != n2[strlen(n2)-1]) {
                    report_runtime_error(ERR_TYPE_MISMATCH);
                    return;
                }
            }
            if (runtime_error_occurred) return;

            if (a_idx1 == -1 && a_idx2 == -1) {
                // Swapping entire variables/arrays
                double tmp_val = vars[idx1].value; vars[idx1].value = vars[idx2].value; vars[idx2].value = tmp_val;
                char *tmp_sval = vars[idx1].s_value; vars[idx1].s_value = vars[idx2].s_value; vars[idx2].s_value = tmp_sval;
                double *tmp_arr = vars[idx1].array; vars[idx1].array = vars[idx2].array; vars[idx2].array = tmp_arr;
                char **tmp_sarr = vars[idx1].s_array; vars[idx1].s_array = vars[idx2].s_array; vars[idx2].s_array = tmp_sarr;
                int tmp_size = vars[idx1].array_size; vars[idx1].array_size = vars[idx2].array_size; vars[idx2].array_size = tmp_size;
                int tmp_dims = vars[idx1].num_dims; vars[idx1].num_dims = vars[idx2].num_dims; vars[idx2].num_dims = tmp_dims;
                for (int i = 0; i < 3; i++) {
                    int td = vars[idx1].dims[i]; vars[idx1].dims[i] = vars[idx2].dims[i]; vars[idx2].dims[i] = td;
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
                    double val1 = (a_idx1 >= 0 && vars[idx1].array) ? vars[idx1].array[a_idx1] : vars[idx1].value;
                    double val2 = (a_idx2 >= 0 && vars[idx2].array) ? vars[idx2].array[a_idx2] : vars[idx2].value;
                    
                    if (a_idx1 >= 0 && vars[idx1].array) vars[idx1].array[a_idx1] = val2;
                    else vars[idx1].value = val2;
                    
                    if (a_idx2 >= 0 && vars[idx2].array) vars[idx2].array[a_idx2] = val1;
                    else vars[idx2].value = val1;
                }
            }
        } else if (t.type == TOKEN_OPEN) {
            char path_buf[256] = "";
            parse_string_expression(&ptr, path_buf, sizeof(path_buf));
            get_next_token(&ptr); // FOR
            Token mode = get_next_token(&ptr);
            get_next_token(&ptr); // AS
            get_next_token(&ptr); // #
            int fnum = (int)evaluate_expression(&ptr);
            if (fnum >= 1 && fnum < 16) {
                const char *m = mode.text;
                const char *mode_str = "r";
                if (strcasecmp(m, "OUTPUT") == 0) mode_str = "w";
                else if (strcasecmp(m, "INPUT") == 0) mode_str = "r";
                else if (strcasecmp(m, "RANDOM") == 0) mode_str = "w+";
                else if (strcasecmp(m, "RWB") == 0 || strcasecmp(m, "RW") == 0) mode_str = "w+";
                reset_file_field_state(fnum);
                file_handles[fnum] = fopen(path_buf, mode_str);
                if (!file_handles[fnum]) {
                    report_runtime_error(ERR_FILE_NOT_FOUND);
                }
            }
        } else if (t.type == TOKEN_FIELD) {
            Token hash = get_next_token(&ptr);
            if (hash.type != TOKEN_HASH) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int fnum = (int)evaluate_expression(&ptr);
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

                int seg_len = (int)evaluate_expression(&ptr);
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
            const char *saved = ptr;
            Token hash = get_next_token(&ptr);
            if (hash.type == TOKEN_HASH) {
                int fnum = (int)evaluate_expression(&ptr);
                if (fnum >= 1 && fnum < 16 && file_handles[fnum]) {
                    fclose(file_handles[fnum]);
                    file_handles[fnum] = NULL;
                    reset_file_field_state(fnum);
                }
            } else {
                ptr = saved;
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
                if (deleted_any) {
                    basic_output("Deleted\n");
                } else if (glob_result == GLOB_NOMATCH) {
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
            int color = 7; 
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
                            if (!no_draw) draw_line(orig_x, orig_y, x, y, color, 0);
                            if (return_pos) { x = orig_x; y = orig_y; }
                            set_graphics_cursor(x, y);
                            continue;
                        }
                        break;
                    }
                    case 'A': angle = val % 4; break;
                    case 'T': if (toupper((unsigned char)*c) == 'A') { c++; ta = strtol(c, (char**)&c, 10); } break;
                    case 'S': scale = val; break;
                    case 'C': color = val; break;
                    case 'P': {
                        int p_color = val;
                        int b_color = color;
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
                    if (!no_draw) draw_line(orig_x, orig_y, x, y, color, 0);
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
                if (is_string_var(var.text)) {
                    // erase string variable or string array element
                    if (array_idx >= 0 && vars[idx].s_array && array_idx < vars[idx].array_size) {
                        if (vars[idx].s_array[array_idx]) free(vars[idx].s_array[array_idx]);
                        vars[idx].s_array[array_idx] = NULL;
                    } else if (vars[idx].s_array) {
                        // ERASE S$ with no index on a string array: release all memory
                        for (int i = 0; i < vars[idx].array_size; i++) {
                            if (vars[idx].s_array[i]) {
                                free(vars[idx].s_array[i]);
                            }
                        }
                        free(vars[idx].s_array);
                        vars[idx].s_array = NULL;
                        vars[idx].array_size = 0;
                        vars[idx].num_dims = 0;
                    } else if (array_idx == -1) { // Scalar string variable
                        if (vars[idx].s_value) free(vars[idx].s_value);
                        vars[idx].s_value = NULL;
                    }
                } else {
                    // numeric variable or array
                    if (array_idx >= 0 && vars[idx].array && array_idx < vars[idx].array_size) {
                        vars[idx].array[array_idx] = 0;
                    } else if (vars[idx].array) {
                        // ERASE A with no index: release numeric array memory
                        free(vars[idx].array);
                        vars[idx].array = NULL;
                        vars[idx].array_size = 0;
                        vars[idx].num_dims = 0;
                    } else if (array_idx == -1) { // Scalar numeric variable
                        vars[idx].value = 0;
                    }
                }
            }
            
        } else if (t.type == TOKEN_COLOR) {
            int color_value = (int)evaluate_expression(&ptr);
            const char *saved = ptr;
            Token sep = get_next_token(&ptr);
            if (sep.type == TOKEN_COMMA) {
                (void)evaluate_expression(&ptr); // optional background color ignored for now
            } else {
                ptr = saved;
            }
            set_text_color(color_value); // Set foreground color
        } else if (t.type == TOKEN_INPUT) {
            const char *saved = ptr;
            int suppress_question = 0;
            Token prompt_tok = get_next_token(&ptr);
            if (prompt_tok.type == TOKEN_STRING) {
                if (graphics_is_active()) graphics_print(prompt_tok.text);
                else printf("%s", prompt_tok.text);
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
                fnum = (int)evaluate_expression(&ptr);
                const char *comma_ptr = ptr;
                Token sep = get_next_token(&comma_ptr);
                if (sep.type == TOKEN_COMMA) ptr = comma_ptr;
            } else {
                ptr = saved;
            }

            int input_var_count = 0;
            int var_indices[16];
            int var_array_idx[16];
            int var_is_str[16];
            while (input_var_count < 16) {
                const char *var_saved = ptr;
                Token var = get_next_token(&ptr);
                if (var.type != TOKEN_IDENTIFIER) {
                    ptr = var_saved;
                    break;
                }
                var_indices[input_var_count] = find_variable(var.text);
                var_array_idx[input_var_count] = parse_array_index(&ptr, var_indices[input_var_count]);
                var_is_str[input_var_count] = (var.text[strlen(var.text) - 1] == '$');
                input_var_count++;
                const char *sep_saved = ptr;
                Token sep = get_next_token(&ptr);
                if (sep.type != TOKEN_COMMA && sep.type != TOKEN_SEMICOLON) {
                    ptr = sep_saved;
                    break;
                }
            }
            if (input_var_count == 0) return;

            char line[256] = "";
            if (fnum != -1) {
                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                } else if (fgets(line, sizeof(line), file_handles[fnum])) {
                    line[strcspn(line, "\r\n")] = 0;
                }
            } else {
                if (!suppress_question) basic_output("? ");
                fflush(stdout);
                if (graphics_is_active()) {
                    graphics_readline(line, sizeof(line));
                } else if (fgets(line, sizeof(line), stdin)) {
                    line[strcspn(line, "\r\n")] = 0;
                }
            }

            char *saveptr = NULL;
            char *value_token = strtok_r(line, ",;", &saveptr);
            for (int i = 0; i < input_var_count; i++) {
                char value[BASIC_STRING_MAX] = "";
                if (value_token) {
                    strncpy(value, value_token, sizeof(value) - 1);
                    value[sizeof(value) - 1] = '\0';
                    trim_string(value);
                    value_token = strtok_r(NULL, ",;", &saveptr);
                }
                assign_input_value(var_indices[i], var_array_idx[i], var_is_str[i], value);
            }
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
            if (base.type != TOKEN_BASE) {
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
            // Process multiple DIM declarations (e.g., DIM A(2,2), B(3,3), C(2))
            while (1) {
                Token var = get_next_token(&ptr);
                if (var.type != TOKEN_IDENTIFIER) break;
                
                Token lparen = get_next_token(&ptr);
                if (lparen.type != TOKEN_LPAREN) break;
                
                // Parse dimensions (can be multiple, separated by commas)
                int dims[3] = {0};
                int num_dims = 0;
                dims[0] = (int)evaluate_expression(&ptr);
                num_dims = 1;
                
                // Check for additional dimensions
                const char *saved = ptr;
                Token sep = get_next_token(&ptr);
                while (sep.type == TOKEN_COMMA && num_dims < 3) {
                    dims[num_dims] = (int)evaluate_expression(&ptr);
                    num_dims++;
                    saved = ptr;
                    sep = get_next_token(&ptr);
                }
                ptr = saved;
                Token rparen = get_next_token(&ptr);
                if (rparen.type != TOKEN_RPAREN) break;
                
                int idx = find_variable(var.text);
                
                // Calculate total size and check if any dim is invalid
                int total_size = 1;
                int dim_err = 0;
                for (int i = 0; i < num_dims; i++) {
                    int dim_size = (option_base == 0) ? (dims[i] + 1) : dims[i];
                    if (dim_size <= 0) {
                        report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
                        dim_err = 1;
                        break;
                    }
                    total_size *= dim_size;
                }
                if (dim_err) break;
                
                if (is_string_var(var.text)) {
                    if (vars[idx].s_array) {
                        report_runtime_error(ERR_DUPLICATE_DEFINITION);
                        break;
                    }
                    vars[idx].s_array = calloc(total_size, sizeof(char*));
                } else {
                    if (vars[idx].array) {
                        report_runtime_error(ERR_DUPLICATE_DEFINITION);
                        break;
                    }
                    vars[idx].array = malloc(total_size * sizeof(double));
                    for(int i=0; i<total_size; i++) vars[idx].array[i] = 0;
                }
                vars[idx].array_size = total_size;
                vars[idx].num_dims = num_dims;
                for(int i=0; i<num_dims; i++) vars[idx].dims[i] = dims[i];
                
                arrays_dimensioned = 1;
                
                // Check for more arrays on the same DIM statement
                saved = ptr;
                Token comma = get_next_token(&ptr);
                if (comma.type != TOKEN_COMMA) {
                    ptr = saved;
                    break;
                }
            }
        } else if (t.type == TOKEN_PSET) {
            double x, y;
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                x = evaluate_expression(&ptr);
                if (get_next_token(&ptr).type == TOKEN_COMMA) {
                    y = evaluate_expression(&ptr);
                } else {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
                if (get_next_token(&ptr).type != TOKEN_RPAREN) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
            } else {
                ptr = saved;
                x = evaluate_expression(&ptr);
                if (get_next_token(&ptr).type == TOKEN_COMMA) {
                    y = evaluate_expression(&ptr);
                } else {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    return;
                }
            }
            int col = 7;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                col = (int)evaluate_expression(&ptr);
            } else {
                ptr = saved;
            }
            set_pixel(x, y, col);
            update_graphics();
        } else if (t.type == TOKEN_LINE) {
            double x1, y1, x2, y2;
            int has_first = 0;
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                x1 = evaluate_expression(&ptr);
                if (get_next_token(&ptr).type == TOKEN_COMMA) y1 = evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
                has_first = 1;
            } else {
                ptr = saved;
                // Check if it starts with - (relative line)
                if (get_next_token(&ptr).type == TOKEN_MINUS) {
                    double dummy_x, dummy_y;
                    get_graphics_cursor(&dummy_x, &dummy_y);
                    x1 = dummy_x; y1 = dummy_y;
                    has_first = 1;
                } else {
                    ptr = saved;
                }
            }
            
            if (has_first) {
                if (get_next_token(&ptr).type != TOKEN_MINUS) {
                    // If it was (x1,y1) but no - next, maybe it's just (x1,y1)-(x2,y2) 
                    // and evaluate_expression consumed more? No, evaluate_expression is clean.
                }
            } else {
                // Should be (x1,y1)-(x2,y2) or x1,y1-x2,y2
                x1 = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                y1 = evaluate_expression(&ptr);
                get_next_token(&ptr); // minus
            }

            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                x2 = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                y2 = evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
            } else {
                ptr = saved;
                x2 = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                y2 = evaluate_expression(&ptr);
            }

            int col = 3;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                col = (int)evaluate_expression(&ptr);
            } else {
                ptr = saved;
            }

            int fill = 0;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                Token flag = get_next_token(&ptr);
                if (strcasecmp(flag.text, "BF") == 0) fill = 2;
                else if (strcasecmp(flag.text, "B") == 0) fill = 1;
                else ptr = saved;
            } else {
                ptr = saved;
            }
            draw_line(x1, y1, x2, y2, col, fill);
        } else if (t.type == TOKEN_CIRCLE) { // CIRCLE (cx,cy),radius[,color]
            double cx, cy, radius;
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                cx = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                cy = evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
            } else {
                ptr = saved;
                cx = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                cy = evaluate_expression(&ptr);
            }
            get_next_token(&ptr); // comma
            radius = evaluate_expression(&ptr);

            int col = 3;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                col = (int)evaluate_expression(&ptr);
            } else {
                ptr = saved;
            }
            int fill = 0;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                fill = (int)evaluate_expression(&ptr);
            } else {
                ptr = saved;
            }
            draw_circle(cx, cy, radius, col, fill);
        } else if (t.type == TOKEN_PAINT) { // PAINT (x,y)[,color[,border]]
            double x, y;
            const char *saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_LPAREN) {
                x = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                y = evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
            } else {
                ptr = saved;
                x = evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                y = evaluate_expression(&ptr);
            }
            int col = 3;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                col = (int)evaluate_expression(&ptr);
            } else {
                ptr = saved;
            }
            int border = col;
            saved = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                border = (int)evaluate_expression(&ptr);
            } else {
                ptr = saved;
            }
            draw_paint(x, y, col, border);
        } else if (t.type == TOKEN_SCREEN) {
            const char *saved_screen = ptr;
            Token nxt = get_next_token(&ptr);
            if (nxt.type == TOKEN_NEWIMAGE) {
                Token lparen = get_next_token(&ptr);
                if (lparen.type == TOKEN_LPAREN) {
                    int w = (int)evaluate_expression(&ptr);
                    Token comma1 = get_next_token(&ptr);
                    if (comma1.type == TOKEN_COMMA) {
                        int h = (int)evaluate_expression(&ptr);
                        int colors = 256;
                        const char *saved_comma2 = ptr;
                        Token comma2 = get_next_token(&ptr);
                        if (comma2.type == TOKEN_COMMA) {
                            colors = (int)evaluate_expression(&ptr);
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
                        }
                    } else {
                        report_runtime_error(ERR_SYNTAX_ERROR);
                    }
                } else {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                }
            } else {
                ptr = saved_screen;
                int mode = (int)evaluate_expression(&ptr);
                init_graphics();
                set_screen_mode(mode); 
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
                int x1 = (int)evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                int y1 = (int)evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
                get_next_token(&ptr); // minus
                get_next_token(&ptr); // LPAREN
                int x2 = (int)evaluate_expression(&ptr);
                get_next_token(&ptr); // comma
                int y2 = (int)evaluate_expression(&ptr);
                get_next_token(&ptr); // RPAREN
                int col = -1, bound = -1;
                saved = ptr;
                if (get_next_token(&ptr).type == TOKEN_COMMA) {
                    saved = ptr;
                    nxt = get_next_token(&ptr);
                    if (nxt.type != TOKEN_COMMA) {
                        ptr = saved;
                        col = (int)evaluate_expression(&ptr);
                        saved = ptr;
                        nxt = get_next_token(&ptr);
                    }
                    if (nxt.type == TOKEN_COMMA) {
                        bound = (int)evaluate_expression(&ptr);
                    } else {
                        ptr = saved;
                    }
                } else {
                    ptr = saved;
                }
                graphics_set_view(use_screen, x1, y1, x2, y2, col, bound);
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
            int row = (int)evaluate_expression(&ptr);
            int col = print_col + 1;
            const char *saved_comma = ptr;
            if (get_next_token(&ptr).type == TOKEN_COMMA) {
                col = (int)evaluate_expression(&ptr);
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
            if (graphics_is_active()) graphics_present_now();
            print_col = 0;
        } else if (t.type == TOKEN_SLEEP) {
            int ms = (int)evaluate_expression(&ptr);
            graphics_sleep(ms);
        }
        else if (t.type == TOKEN_SEEK) {
            const char *saved = ptr;
            Token hash = get_next_token(&ptr);
            int fnum = -1;
            if (hash.type == TOKEN_HASH) {
                fnum = (int)evaluate_expression(&ptr);
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
                int x = evaluate_expression(&ptr);
                if (get_next_token(&ptr).type == TOKEN_COMMA) {
                    int y = evaluate_expression(&ptr);
                    get_next_token(&ptr);
                    get_next_token(&ptr);
                    Token var_tok = get_next_token(&ptr);
                    int idx = find_variable(var_tok.text);
                    if (idx != -1 && vars[idx].array && vars[idx].array_size >= 2) {
                        int w = (int)vars[idx].array[0];
                        int h = (int)vars[idx].array[1];
                        int action = 0;
                        const char *action_saved = ptr;
                        if (get_next_token(&ptr).type == TOKEN_COMMA) {
                            Token act_tok = get_next_token(&ptr);
                            if (act_tok.type == TOKEN_PSET || strcasecmp(act_tok.text, "PSET") == 0) action = 1;
                            else if (strcasecmp(act_tok.text, "PRESET") == 0) action = 2;
                            else if (strcasecmp(act_tok.text, "AND") == 0) action = 3;
                            else if (strcasecmp(act_tok.text, "OR") == 0) action = 4;
                            else if (strcasecmp(act_tok.text, "XOR") == 0) action = 0;
                        } else {
                            ptr = action_saved;
                        }
                        int k = 2;
                        for (int j = 0; j < h; j++) {
                            for (int i = 0; i < w; i++) {
                                if (k >= vars[idx].array_size) break;
                                int src_col = (int)vars[idx].array[k++];
                                int dst_col = get_pixel(x + i, y + j);
                                int final_col = src_col;
                                switch (action) {
                                    case 0: final_col = src_col ^ dst_col; break;
                                    case 1: final_col = src_col; break;
                                    case 2: final_col = ~src_col & 0x0F; break;
                                    case 3: final_col = src_col & dst_col; break;
                                    case 4: final_col = src_col | dst_col; break;
                                }
                                set_pixel(x + i, y + j, final_col);
                            }
                        }
                        if (graphics_is_active()) graphics_present_now();
                    }
                }
            } else if (next.type == TOKEN_HASH) {
                int fnum = (int)evaluate_expression(&ptr);
                Token sep = get_next_token(&ptr);
                if (sep.type != TOKEN_COMMA) { ptr = saved; }
                else {
                    double rec = evaluate_expression(&ptr);
                    Token mode_sep = get_next_token(&ptr);
                    if (mode_sep.type != TOKEN_COMMA) {
                        if (fnum < 1 || fnum >= 16 || !file_handles[fnum] || !file_field_state[fnum].buffer || file_field_state[fnum].size <= 0) {
                            report_runtime_error(ERR_BAD_FILE_NUMBER);
                        } else {
                            long offset = (long)((rec - 1) * file_field_state[fnum].size);
                            fseek(file_handles[fnum], offset, SEEK_SET);
                            fwrite(file_field_state[fnum].buffer, 1, file_field_state[fnum].size, file_handles[fnum]);
                            fflush(file_handles[fnum]);
                        }
                    } else {
                        int len = (int)evaluate_expression(&ptr);
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
                        } else {
                            ptr = saved;
                        }
                        if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                            report_runtime_error(ERR_BAD_FILE_NUMBER);
                        } else {
                            long offset = (long)((rec - 1) * len);
                            fseek(file_handles[fnum], offset, SEEK_SET);
                            char buf[BASIC_STRING_MAX];
                            memset(buf, ' ', len);
                            strncpy(buf, data, len);
                            fwrite(buf, 1, len, file_handles[fnum]);
                            fflush(file_handles[fnum]);
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
                int x1 = evaluate_expression(&ptr);
                get_next_token(&ptr);
                int y1 = evaluate_expression(&ptr);
                get_next_token(&ptr);
                get_next_token(&ptr);
                get_next_token(&ptr);
                int x2 = evaluate_expression(&ptr);
                get_next_token(&ptr);
                int y2 = evaluate_expression(&ptr);
                get_next_token(&ptr);
                get_next_token(&ptr);
                Token var_tok = get_next_token(&ptr);
                int idx = find_variable(var_tok.text);
                int w = abs(x2 - x1) + 1;
                int h = abs(y2 - y1) + 1;
                int min_x = (x1 < x2) ? x1 : x2;
                int min_y = (y1 < y2) ? y1 : y2;
                int total_size = 2 + w * h;
                if (idx != -1 && vars[idx].array && vars[idx].array_size >= total_size) {
                    vars[idx].array[0] = (double)w;
                    vars[idx].array[1] = (double)h;
                    int k = 2;
                    for (int j = 0; j < h; j++) {
                        for (int i = 0; i < w; i++) {
                            vars[idx].array[k++] = (double)get_pixel(min_x + i, min_y + j);
                        }
                    }
                } else {
                    report_runtime_error(ERR_SUBSCRIPT_OUT_OF_RANGE);
                }
            } else if (next.type == TOKEN_HASH) {
                int fnum = (int)evaluate_expression(&ptr);
                Token sep = get_next_token(&ptr);
                if (sep.type != TOKEN_COMMA) { ptr = saved; }
                else {
                    double rec = evaluate_expression(&ptr);
                    Token mode_sep = get_next_token(&ptr);
                    if (mode_sep.type != TOKEN_COMMA) {
                        if (fnum < 1 || fnum >= 16 || !file_handles[fnum] || !file_field_state[fnum].buffer || file_field_state[fnum].size <= 0) {
                            report_runtime_error(ERR_BAD_FILE_NUMBER);
                        } else {
                            long offset = (long)((rec - 1) * file_field_state[fnum].size);
                            fseek(file_handles[fnum], offset, SEEK_SET);
                            size_t r = fread(file_field_state[fnum].buffer, 1, file_field_state[fnum].size, file_handles[fnum]);
                            if (r < (size_t)file_field_state[fnum].size) {
                                for (size_t i = r; i < (size_t)file_field_state[fnum].size; i++) file_field_state[fnum].buffer[i] = ' ';
                            }
                            file_field_state[fnum].buffer[file_field_state[fnum].size] = '\0';
                        }
                    } else {
                        int len = (int)evaluate_expression(&ptr);
                        sep = get_next_token(&ptr);
                        if (sep.type != TOKEN_COMMA) { ptr = saved; }
                        else {
                            Token var = get_next_token(&ptr);
                            if (!(var.type == TOKEN_IDENTIFIER && is_string_var(var.text))) { ptr = saved; }
                            else {
                                int idx = find_variable(var.text);
                                int array_idx = parse_array_index(&ptr, idx);
                                if (fnum < 1 || fnum >= 16 || !file_handles[fnum]) {
                                    report_runtime_error(ERR_BAD_FILE_NUMBER);
                                } else {
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
                                    set_string_variable(idx, array_idx, buf);
                                }
                            }
                        }
                    }
                }
            } else {
                ptr = saved;
                report_runtime_error(ERR_SYNTAX_ERROR);
            }
        } else if (t.type == TOKEN_FREEIMAGE) {
            int handle = (int)evaluate_expression(&ptr);
            if (!graphics_freeimage(handle)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_PUTIMAGE) {
            int x1 = 0, y1 = 0, x2 = -1, y2 = -1;
            int sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
            int has_source = 0;
            Token next = get_next_token(&ptr);
            if (next.type == TOKEN_LPAREN) {
                x1 = (int)evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                y1 = (int)evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_RPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                Token separator = get_next_token(&ptr);
                if (separator.type == TOKEN_MINUS) {
                    if (get_next_token(&ptr).type != TOKEN_LPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                    x2 = (int)evaluate_expression(&ptr);
                    if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                    y2 = (int)evaluate_expression(&ptr);
                    if (get_next_token(&ptr).type != TOKEN_RPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                    if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                } else if (separator.type != TOKEN_COMMA) {
                    report_runtime_error(ERR_SYNTAX_ERROR); return;
                }
            } else if (next.type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR); return;
            }

            int handle = (int)evaluate_expression(&ptr);
            const char *after_handle = ptr;
            Token source_separator = get_next_token(&ptr);
            if (source_separator.type == TOKEN_COMMA) {
                if (get_next_token(&ptr).type != TOKEN_LPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                sx1 = (int)evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                sy1 = (int)evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_RPAREN || get_next_token(&ptr).type != TOKEN_MINUS || get_next_token(&ptr).type != TOKEN_LPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                sx2 = (int)evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_COMMA) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                sy2 = (int)evaluate_expression(&ptr);
                if (get_next_token(&ptr).type != TOKEN_RPAREN) { report_runtime_error(ERR_SYNTAX_ERROR); return; }
                has_source = 1;
            } else {
                ptr = after_handle;
            }
            if (!graphics_is_active()) init_graphics();
            if (!graphics_putimage(x1, y1, x2, y2, handle, sx1, sy1, sx2, sy2, has_source)) {
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
            int px = (int)evaluate_expression(&ptr);
            Token comma1 = get_next_token(&ptr);
            if (comma1.type != TOKEN_COMMA) {
                report_runtime_error(ERR_SYNTAX_ERROR);
                return;
            }
            int py = (int)evaluate_expression(&ptr);
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
            int handle = (int)evaluate_expression(&ptr);
            if (!graphics_is_active()) init_graphics();
            if (!graphics_setfont(handle)) {
                report_runtime_error(ERR_ILLEGAL_FUNCTION_CALL);
            }
        } else if (t.type == TOKEN_FREEFONT) {
            /* _FREEFONT handle% */
            int handle = (int)evaluate_expression(&ptr);
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

void interpret_line(const char *input, int is_direct, int *last_line_num) {
    const char *ptr = input;
    interpret_line_at_ptr(&ptr, is_direct, last_line_num);
}

void run_program() {
    clear_variables(1); // Keep registry for RUN to maintain pre-tokenized indices
    stop_running = 0;
    runtime_error_occurred = 0;
    gosub_ptr = 0;
    while_ptr = 0;
    for_ptr = 0;
    print_col = 0;
    clear_data_pointer();
    Statement *curr = get_head();
    const char *resume_ptr = NULL;
    error_stmt = NULL;
    error_ptr = NULL;
    error_next_ptr = NULL;

    timer_pending = 0;
    timer_event_active = 0;
    for (int i = 0; i < 32; i++) {
        key_event_pending[i] = 0;
        key_event_active[i] = 0;
    }
    for (int i = 0; i < 8; i++) {
        strig_event_pending[i] = 0;
        strig_event_active[i] = 0;
    }
    for (int i = 1; i < 16; i++) {
        reset_file_field_state(i);
    }

    int poll_counter = 0;
    while (curr && !stop_running) {
        
        if (++poll_counter >= 1000) {
            handle_events();
            if (!graphics_is_active()) fflush(stdout);
            poll_counter = 0;
        }

        Statement *exec_stmt = curr;
        current_executing_line = exec_stmt->line_number;
        
        int start_pos = 0;
        if (resume_ptr) {
            while (start_pos < exec_stmt->token_count && exec_stmt->tokens[start_pos].start_ptr < resume_ptr) {
                start_pos++;
            }
        }
        TokenStream ts = {exec_stmt->tokens, start_pos};
        resume_ptr = NULL;
        
        int jumped = 0;
        while (ts.pos < exec_stmt->token_count && !stop_running && !jumped) {
            Token t = ts.tokens[ts.pos++];

            switch (t.type) {
            case TOKEN_EOF:
            case TOKEN_COLON:
                continue;

            case TOKEN_IF: {
                double cond = evaluate_expression_tok(&ts);
                // Consume THEN or GOTO token
                if (ts.pos < exec_stmt->token_count && (ts.tokens[ts.pos].type == TOKEN_THEN || ts.tokens[ts.pos].type == TOKEN_GOTO)) {
                    ts.pos++;
                }
                
                if (cond != 0) continue;
                
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
            
            // Handle FOR loop
            case TOKEN_FOR: {
                Token var = ts.tokens[ts.pos++];
                Token eq = ts.tokens[ts.pos++];
                if (eq.type != TOKEN_EQUALS) {
                    report_runtime_error(ERR_SYNTAX_ERROR);
                    break;
                }
                double start = evaluate_expression_tok(&ts);
                ts.pos++; // skip TO
                double end = evaluate_expression_tok(&ts);
                double step = 1.0;
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_STEP) {
                    ts.pos++;
                    step = evaluate_expression_tok(&ts);
                }
                
                int idx = var.var_idx;
                if (idx == -1) idx = find_variable(var.text);
                vars[idx].value = start;

                // BASIKA check: If the loop should not execute at all
                int should_skip = 0;
                if (step > 0 && start > end) should_skip = 1;
                else if (step < 0 && start < end) should_skip = 1;

                if (should_skip) {
                    int end_ts_pos = ts.pos; // Start searching from current position
                    Statement *target_stmt = skip_for_block(exec_stmt, end_ts_pos, idx, &end_ts_pos);

                    if (target_stmt) {
                        curr = target_stmt;
                        if (end_ts_pos < curr->token_count) {
                            resume_ptr = curr->tokens[end_ts_pos].start_ptr;
                        } else {
                            resume_ptr = NULL; // NEXT was last token on line, advance to next line
                        }
                    } else {
                        report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                    }
                    jumped = 1;
                    break; // Exit the command loop for this line
                }

                if (for_ptr < 16) {
                    for_stack[for_ptr].var_idx = idx;
                    for_stack[for_ptr].end_val = end;
                    for_stack[for_ptr].step_val = step;
                    for_stack[for_ptr].start_stmt = exec_stmt;
                    // Sync ptr for the stack
                    const char *temp_ptr = exec_stmt->raw_command;
                    for(int k=0; k < ts.pos; k++) get_next_token(&temp_ptr);
                    for_stack[for_ptr].start_ptr = temp_ptr;
                    for_ptr++;
                } else {
                    report_runtime_error(ERR_OUT_OF_MEMORY);
                }
                continue;
            }
            
            case TOKEN_WHILE: {
                const char *command_start_ptr = t.start_ptr;
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

            // Handle ON GOTO/GOSUB
            case TOKEN_ON: {
                const char *command_start_ptr = t.start_ptr;
                Token next_on = ts.tokens[ts.pos];
                if (next_on.type == TOKEN_ERR || (next_on.type == TOKEN_IDENTIFIER && strcasecmp(next_on.text, "ERROR") == 0)) {
                    const char *temp_ptr = command_start_ptr;
                    interpret_line_at_ptr(&temp_ptr, 0, NULL);
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
                        timer_interval = n_val;
                        on_timer_line = target_line;
                        timer_pending = 0;
                        timer_event_active = 0;
                        timer_last_trigger_time = wall_time_seconds();
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

                int index = (int)evaluate_expression_tok(&ts);
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
                            const char *temp_ptr = exec_stmt->raw_command;
                            for(int k=0; k < ts.pos; k++) get_next_token(&temp_ptr);
                            gosub_call_stack[gosub_ptr].ptr = temp_ptr;
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
                    const char *temp_ptr = exec_stmt->raw_command;
                    for(int k=0; k < ts.pos; k++) get_next_token(&temp_ptr);
                    gosub_call_stack[gosub_ptr].ptr = temp_ptr;
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
                        timer_event_active = 0;
                        timer_last_trigger_time = wall_time_seconds();
                    } else if (gosub_call_stack[gosub_ptr].event_type == EVENT_KEY) {
                        int k = gosub_call_stack[gosub_ptr].event_index;
                        if (k >= 1 && k < 32) key_event_active[k] = 0;
                    } else if (gosub_call_stack[gosub_ptr].event_type == EVENT_STRIG) {
                        int s = gosub_call_stack[gosub_ptr].event_index;
                        if (s >= 0 && s < 8) strig_event_active[s] = 0;
                    }
                    if (graphics_is_active()) {
                        graphics_present_now();
                    }
                    
                    jumped = 1;
                    break;
                } else {
                    report_runtime_error(ERR_RETURN_WITHOUT_GOSUB);
                    break;
                }

            // Handle NEXT
            case TOKEN_NEXT: {
                Token next_var = (ts.pos < exec_stmt->token_count) ? ts.tokens[ts.pos] : (Token){TOKEN_EOF, "", 0, 0, 0, NULL, 0};
                int f = -1;
                if (next_var.type == TOKEN_IDENTIFIER) {
                    ts.pos++;
                    int target_idx = next_var.var_idx;
                    if (target_idx == -1) target_idx = find_variable(next_var.text);
                    for (int i = for_ptr - 1; i >= 0; i--) {
                        if (for_stack[i].var_idx == target_idx) {
                            f = i;
                            break;
                        }
                    }
                } else {
                    if (for_ptr > 0) f = for_ptr - 1;
                }
                if (f != -1) {
                    vars[for_stack[f].var_idx].value += for_stack[f].step_val;
                    double v = vars[for_stack[f].var_idx].value;
                    if ((for_stack[f].step_val > 0 && v <= for_stack[f].end_val) || (for_stack[f].step_val < 0 && v >= for_stack[f].end_val)) {
                        curr = for_stack[f].start_stmt;
                        resume_ptr = for_stack[f].start_ptr;
                        jumped = 1;
                        break;
                    } else {
                        for_ptr = f; // Pop the loop from the stack
                    }
                } else {
                    report_runtime_error(ERR_NEXT_WITHOUT_FOR);
                }
                break; // NEXT is a line-level control, break from inner loop to advance 'curr'
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

            case TOKEN_END:
                stop_running = 1;
                break;

            // Handle stray ELSE token (should not execute if we got here)
            case TOKEN_ELSE:
                // Skip this ELSE clause - it means condition was true or we just finished THEN part
                ts.pos = exec_stmt->token_count;
                continue;

            case TOKEN_RESUME: {
                Token rt = (ts.pos < exec_stmt->token_count) ? ts.tokens[ts.pos++] : (Token){TOKEN_EOF, "", 0, 0, 0, NULL, 0};
                if (rt.type == TOKEN_NEXT) {
                    curr = error_stmt;
                    resume_ptr = error_next_ptr;
                } else if (rt.type == TOKEN_NUMBER) {
                    curr = find_line(rt.int_val);
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
                t = ts.tokens[ts.pos++]; // Skip LET to get variable identifier
                /* fall through */
            case TOKEN_IDENTIFIER: {
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_COLON) {
                    ts.pos++; // Skip colon
                    continue;
                }
                // Direct handling of assignments
                int idx = t.var_idx;
                if (idx == -1) idx = find_variable(t.text);
                int array_idx = parse_array_index_tok(&ts, idx);
                if (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].type == TOKEN_EQUALS) {
                    ts.pos++;
                    if (is_string_var(t.text)) {
                        const char *temp_ptr = ts.tokens[ts.pos].start_ptr;
                        char value[BASIC_STRING_MAX] = "";
                        parse_string_expression(&temp_ptr, value, sizeof(value));
                        set_string_variable(idx, array_idx, value);
                        // Sync ts.pos
                        while (ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].start_ptr < temp_ptr) ts.pos++;
                    } else {
                        set_numeric_variable(idx, array_idx, evaluate_expression_tok(&ts));
                    }
                    continue;
                }
                // If not an assignment, fallback to interpret_line_at_ptr
                ts.pos--; // Put identifier back
                /* fall through */
            }

            default: {
                // Fallback for complex commands
                const char *command_start_ptr = t.start_ptr;
                const char *temp_ptr = command_start_ptr;
                interpret_line_at_ptr(&temp_ptr, 0, NULL);
                // Advance TokenStream to match the processed string
                while(ts.pos < exec_stmt->token_count && ts.tokens[ts.pos].start_ptr < temp_ptr) ts.pos++;
                break;
            }
            }

            maybe_queue_timer_event();
            if (!jumped && !runtime_error_occurred && dispatch_pending_timer_event(exec_stmt, &ts, &curr, &resume_ptr)) {
                jumped = 1;
                break;
            }
            if (!jumped && !runtime_error_occurred && dispatch_pending_key_event(exec_stmt, &ts, &curr, &resume_ptr)) {
                jumped = 1;
                break;
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

        if (!stop_running && !jumped) {
            curr = find_next_statement(current_executing_line);
        }

        if (runtime_error_occurred && on_error_goto_line > 0) {
            Statement *trap = find_line(on_error_goto_line);
            if (trap) {
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

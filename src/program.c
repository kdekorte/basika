#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "program.h"
#include "interpreter.h" // For basic_output
#include "lexer.h"

static Statement *head = NULL;
static Statement **line_index = NULL;
static int line_index_count = 0;
static int index_dirty = 1;
/* Bumped by every edit, so CONT can refuse to resume a changed program. */
static unsigned int edit_generation = 1;

unsigned int program_edit_generation(void) { return edit_generation; }

static void free_statement_expressions(Statement *stmt) {
    if (!stmt->compiled_expressions) return;
    for (int i = 0; i < stmt->token_count; i++) {
        CompiledExpression *expression = stmt->compiled_expressions[i];
        if (expression) {
            free(expression->instructions);
            free(expression);
        }
    }
    free(stmt->compiled_expressions);
    stmt->compiled_expressions = NULL;
}

/* update_line_index - Rebuild the sorted array of Statement pointers used
 * for binary search in find_line(). Only rebuilds when the index has been
 * marked dirty by an add/delete/clear operation */
static void update_line_index() {
    if (!index_dirty) return;
    if (line_index) {
        free(line_index);
        line_index = NULL;
    }
    line_index_count = 0;
    Statement *curr = head;
    while (curr) {
        line_index_count++;
        curr = curr->next;
    }
    if (line_index_count > 0) {
        line_index = malloc(sizeof(Statement*) * line_index_count);
        if (line_index) {
            curr = head;
            for (int i = 0; i < line_index_count; i++) {
                line_index[i] = curr;
                curr = curr->next;
            }
            index_dirty = 0;
        }
    } else {
        index_dirty = 0;
    }
}

/* add_line - Insert or replace a numbered BASIC line in the program.
 * Lines are kept in ascending order by line number. If a line with the
 * same number already exists its text and tokens are replaced. Also
 * extracts a label (e.g. "myLabel:") from the first token if present */
void add_line(int line_num, const char *text, int source_line_number, int has_explicit_line_number) {
    index_dirty = 1;
    edit_generation++;
    Statement **curr = &head;
    while (*curr && (*curr)->line_number < line_num) {
        curr = &((*curr)->next);
    }

    Statement *stmt = NULL;
    if (*curr && (*curr)->line_number == line_num) {
        free_statement_expressions(*curr);
        free((*curr)->tokens);
        (*curr)->tokens = NULL;
        strncpy((*curr)->raw_command, text, BASIC_LINE_MAX - 1);
        (*curr)->raw_command[BASIC_LINE_MAX - 1] = '\0';
        (*curr)->source_line_number = source_line_number;
        (*curr)->has_explicit_line_number = has_explicit_line_number;
        tokenize_line(*curr);
        compile_statement_expressions(*curr);
        stmt = *curr;
    } else {
        Statement *new_stmt = calloc(1, sizeof(Statement));
        if (!new_stmt) return;
        new_stmt->line_number = line_num;
        new_stmt->source_line_number = source_line_number;
        new_stmt->has_explicit_line_number = has_explicit_line_number;
        strncpy(new_stmt->raw_command, text, BASIC_LINE_MAX - 1);
        new_stmt->raw_command[BASIC_LINE_MAX - 1] = '\0';
        new_stmt->next = *curr;
        *curr = new_stmt;
        tokenize_line(new_stmt);
        compile_statement_expressions(new_stmt);
        stmt = new_stmt;
    }

    if (stmt->token_count > 1 && stmt->tokens[0].type == TOKEN_IDENTIFIER && stmt->tokens[1].type == TOKEN_COLON) {
        strncpy(stmt->label, stmt->tokens[0].text, 63);
        stmt->label[63] = '\0';
    } else {
        stmt->label[0] = '\0';
    }
}

/* find_label - Search the program for a statement whose label matches
 * the given string (case-insensitive). Returns NULL if not found */
Statement* find_label(const char *label) {
    Statement *curr = head;
    while (curr) {
        if (curr->label[0] != '\0') {
            if (strcasecmp(curr->label, label) == 0) return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

/* find_line - Look up a statement by line number using binary search on
 * the line index. Falls back to a linear scan if the index is empty */
Statement* find_line(int line_num) {
    update_line_index();
    if (line_index && line_index_count > 0) {
        int low = 0, high = line_index_count - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (line_index[mid]->line_number == line_num) return line_index[mid];
            if (line_index[mid]->line_number < line_num) low = mid + 1;
            else high = mid - 1;
        }
        return NULL;
    }
    Statement *curr = head;
    while (curr) {
        if (curr->line_number == line_num) return curr;
        curr = curr->next;
    }
    return NULL;
}

/* find_next_statement - Return the first statement with a line number
 * strictly greater than the given line number, or NULL if none */
Statement* find_next_statement(int line_num) {
    Statement *curr = head;
    while (curr) {
        if (curr->line_number > line_num) return curr;
        curr = curr->next;
    }
    return NULL;
}

/* list_program_range - Print the stored lines numbered first_line through
 * last_line via basic_output, implementing LIST [first][-[last]] */
void list_program_range(int first_line, int last_line) {
    Statement *curr = head;
    while (curr) {
        if (curr->line_number >= first_line && curr->line_number <= last_line) {
            const char *text = curr->raw_command;
            if (*text == ' ') text++; // stored text keeps the space after the number
            char out_buf[BASIC_LINE_MAX + 16]; // Enough for line number + command + newline
            snprintf(out_buf, sizeof(out_buf), "%d %s\n", curr->line_number, text);
            basic_output(out_buf);
        }
        curr = curr->next;
    }
}

/* list_program - Print all stored program lines in order */
void list_program() {
    list_program_range(0, 0x7FFFFFFF);
}

/* clear_program - Free all statements and reset the program storage,
 * implementing the NEW command */
void clear_program() {
    lexer_set_noprefix(0);
    index_dirty = 1;
    edit_generation++;
    Statement *curr = head;
    while (curr) {
        Statement *next = curr->next;
        free_statement_expressions(curr);
        if (curr->tokens) free(curr->tokens);
        free(curr);
        curr = next;
    }
    head = NULL;
}

/* delete_line - Remove a single line from the program by line number.
 * Frees the statement and its tokens */
void delete_line(int line_num) {
    index_dirty = 1;
    edit_generation++;
    Statement **curr = &head;
    while (*curr) {
        if ((*curr)->line_number == line_num) {
            Statement *to_remove = *curr;
            *curr = to_remove->next;
            free_statement_expressions(to_remove);
            if (to_remove->tokens) free(to_remove->tokens);
            free(to_remove);
            return;
        }
        curr = &((*curr)->next);
    }
}

/* get_head - Return the first statement in the program linked list */
Statement* get_head() { return head; }

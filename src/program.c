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
    Statement **curr = &head;
    while (*curr && (*curr)->line_number < line_num) {
        curr = &((*curr)->next);
    }

    Statement *stmt = NULL;
    if (*curr && (*curr)->line_number == line_num) {
        strncpy((*curr)->raw_command, text, 255);
        (*curr)->source_line_number = source_line_number;
        (*curr)->has_explicit_line_number = has_explicit_line_number;
        tokenize_line(*curr);
        stmt = *curr;
    } else {
        Statement *new_stmt = malloc(sizeof(Statement));
        if (!new_stmt) return;
        new_stmt->line_number = line_num;
        new_stmt->source_line_number = source_line_number;
        new_stmt->has_explicit_line_number = has_explicit_line_number;
        strncpy(new_stmt->raw_command, text, 255);
        new_stmt->next = *curr;
        *curr = new_stmt;
        tokenize_line(new_stmt);
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

/* list_program - Print all stored program lines in order via basic_output,
 * implementing the LIST command */
void list_program() {
    Statement *curr = head;
    while (curr) {
        char out_buf[256 + 16]; // Enough for line number + command + newline
        sprintf(out_buf, "%d %s\n", curr->line_number, curr->raw_command);
        basic_output(out_buf);
        curr = curr->next;
    }
}

/* clear_program - Free all statements and reset the program storage,
 * implementing the NEW command */
void clear_program() {
    index_dirty = 1;
    Statement *curr = head;
    while (curr) {
        Statement *next = curr->next;
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
    Statement **curr = &head;
    while (*curr) {
        if ((*curr)->line_number == line_num) {
            Statement *to_remove = *curr;
            *curr = to_remove->next;
            if (to_remove->tokens) free(to_remove->tokens);
            free(to_remove);
            return;
        }
        curr = &((*curr)->next);
    }
}

/* get_head - Return the first statement in the program linked list */
Statement* get_head() { return head; }

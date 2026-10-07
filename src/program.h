#ifndef PROGRAM_H
#define PROGRAM_H
#include "common.h"

void add_line(int line_num, const char *text, int source_line_number, int has_explicit_line_number);
void list_program();
void list_program_range(int first_line, int last_line);
unsigned int program_edit_generation(void);
Statement* find_label(const char *label);
void clear_program();
Statement* get_head();
Statement* find_line(int line_num);
Statement* find_next_statement(int line_num);
void delete_line(int line_num);

#endif

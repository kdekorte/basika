#ifndef INTERPRETER_H
#define INTERPRETER_H
#include <signal.h>
#include "common.h"

void interpret_line(const char *input, int is_direct, int *last_line_num, int source_line_number);
int read_program_line(FILE *file, char *line, size_t size, int *physical_line);
void run_program();
void compile_statement_expressions(Statement *stmt);
void basic_output(const char *text); // Expose for program.c
void basika_trigger_key_event(int key_idx);
void basika_trigger_strig_event(int strig_idx, int pressed);
void basika_trigger_pen_event(void);
int basika_auto_line(int advance);
void basika_auto_cancel(void);
const char *basika_take_edit_text(void);
extern volatile sig_atomic_t stop_running;
extern volatile sig_atomic_t break_requested;

#endif

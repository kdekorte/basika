#ifndef INTERPRETER_H
#define INTERPRETER_H
#include <signal.h>

void interpret_line(const char *input, int is_direct, int *last_line_num, int source_line_number);
void run_program();
void basic_output(const char *text); // Expose for program.c
void basika_trigger_key_event(int key_idx);
void basika_trigger_strig_event(int strig_idx, int pressed);
extern volatile sig_atomic_t stop_running;

#endif

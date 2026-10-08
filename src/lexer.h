#ifndef LEXER_H
#define LEXER_H
#include "common.h"

Token get_next_token(const char **input);
int find_variable(const char *name); // Declare find_variable for lexer.c
void tokenize_line(Statement *stmt);
/* $NOPREFIX: QB64 names such as _DISPLAY may be written without the "_". */
void lexer_set_noprefix(int enabled);

#endif

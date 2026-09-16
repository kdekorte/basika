#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include "lexer.h"

typedef struct {
    const char *keyword;
    TokenType type;
} KeywordMap;

static const KeywordMap keyword_table[] = {
    {"ABS", TOKEN_ABS},
    {"AND", TOKEN_AND},
    {"ARGC", TOKEN_ARGC},
    {"ARGV$", TOKEN_ARGVS},
    {"AS", TOKEN_AS},
    {"ASC", TOKEN_ASC},
    {"ATN", TOKEN_ATN},
    {"BASE", TOKEN_BASE},
    {"BEEP", TOKEN_BEEP},
    {"CALL", TOKEN_CALL},
    {"CHDIR", TOKEN_CHDIR},
    {"CHR$", TOKEN_CHR},
    {"CIRCLE", TOKEN_CIRCLE},
    {"CLOSE", TOKEN_CLOSE},
    {"CLS", TOKEN_CLS},
    {"COLOR", TOKEN_COLOR},
    {"COMMAND$", TOKEN_COMMANDS},
    {"COS", TOKEN_COS},
    {"CVD", TOKEN_CVD},
    {"CVI", TOKEN_CVI},
    {"CVS", TOKEN_CVS},
    {"DATA", TOKEN_DATA},
    {"DATE$", TOKEN_DATE},
    {"DECLARE", TOKEN_DECLARE},
    {"_DEFLATE$", TOKEN_DEFLATE},
    {"DEF", TOKEN_DEF},
    {"DEFDBL", TOKEN_DEFDBL},
    {"DEFINT", TOKEN_DEFINT},
    {"DEFSNG", TOKEN_DEFSNG},
    {"DEFSTR", TOKEN_DEFSTR},
    {"DELETE", TOKEN_DELETE},
    {"DIM", TOKEN_DIM},
    {"DRAW", TOKEN_DRAW},
    {"ELSE", TOKEN_ELSE},
    {"END", TOKEN_END},
    {"ENVIRON", TOKEN_ENVIRON},
    {"ENVIRON$", TOKEN_ENVIRON},
    {"EOF", TOKEN_EOF_FUNC},
    {"ERASE", TOKEN_ERASE},
    {"ERROR", TOKEN_ERR},
    {"EXIT", TOKEN_EXIT},
    {"EXP", TOKEN_EXP},
    {"FIELD", TOKEN_FIELD},
    {"FILES", TOKEN_FILES},
    {"FIX", TOKEN_FIX},
    {"FOR", TOKEN_FOR},
    {"FUNCTION", TOKEN_FUNCTION},
    {"GET", TOKEN_GET},
    {"GET$", TOKEN_GETS},
    {"GOSUB", TOKEN_GOSUB},
    {"GOTO", TOKEN_GOTO},
    {"HEX$", TOKEN_HEX},
    {"IF", TOKEN_IF},
    {"INKEY$", TOKEN_INKEY},
    {"INPUT", TOKEN_INPUT},
    {"INSTR", TOKEN_INSTR},
    {"INT", TOKEN_INT},
    {"KEY", TOKEN_KEY},
    {"KILL", TOKEN_KILL},
    {"LCASE$", TOKEN_LCASE},
    {"LEFT$", TOKEN_LEFT},
    {"LEN", TOKEN_LEN},
    {"LET", TOKEN_LET},
    {"LINE", TOKEN_LINE},
    {"LIST", TOKEN_LIST},
    {"LOC", TOKEN_LOC},
    {"LOCATE", TOKEN_LOCATE},
    {"LOF", TOKEN_LOF},
    {"LOG", TOKEN_LOG},
    {"LSET", TOKEN_LSET},
    {"LTRIM$", TOKEN_LTRIM},
    {"MID$", TOKEN_MID},
    {"MKD$", TOKEN_MKD},
    {"MKDIR", TOKEN_MKDIR},
    {"MKI$", TOKEN_MKI},
    {"MKS$", TOKEN_MKS},
    {"MOD", TOKEN_MOD},
    {"NAME", TOKEN_NAME},
    {"NEW", TOKEN_NEW},
    {"NEXT", TOKEN_NEXT},
    {"NOT", TOKEN_NOT},
    {"OCT$", TOKEN_OCT},
    {"OFF", TOKEN_OFF},
    {"ON", TOKEN_ON},
    {"OPEN", TOKEN_OPEN},
    {"OPTION", TOKEN_OPTION},
    {"OR", TOKEN_OR},
    {"PAINT", TOKEN_PAINT},
    {"PEEK", TOKEN_PEEK},
    {"PLAY", TOKEN_PLAY},
    {"POKE", TOKEN_POKE},
    {"PRINT", TOKEN_PRINT},
    {"PSET", TOKEN_PSET},
    {"PUT", TOKEN_PUT},
    {"QUIT", TOKEN_QUIT},
    {"RANDOMIZE", TOKEN_RANDOMIZE},
    {"READ", TOKEN_READ},
    {"REM", TOKEN_REM},
    {"RESTORE", TOKEN_RESTORE},
    {"RESUME", TOKEN_RESUME},
    {"RETURN", TOKEN_RETURN},
    {"REVERSE", TOKEN_REVERSE},
    {"RIGHT$", TOKEN_RIGHT},
    {"RMDIR", TOKEN_RMDIR},
    {"RND", TOKEN_RND},
    {"RSET", TOKEN_RSET},
    {"RTRIM$", TOKEN_RTRIM},
    {"RUN", TOKEN_RUN},
    {"SCREEN", TOKEN_SCREEN},
    {"SCREENSHOT", TOKEN_SCREENSHOT},
    {"SEEK", TOKEN_SEEK},
    {"SGN", TOKEN_SGN},
    {"SHARED", TOKEN_SHARED},
    {"SHELL", TOKEN_SHELL},
    {"SIN", TOKEN_SIN},
    {"SLEEP", TOKEN_SLEEP},
    {"SOUND", TOKEN_SOUND},
    {"SPACE$", TOKEN_SPACE},
    {"SPC", TOKEN_SPC},
    {"SQR", TOKEN_SQR},
    {"STATIC", TOKEN_STATIC},
    {"STEP", TOKEN_STEP},
    {"STR$", TOKEN_STR},
    {"STRIG", TOKEN_STRIG},
    {"STRING$", TOKEN_STRING_FUNC},
    {"SUB", TOKEN_SUB},
    {"SWAP", TOKEN_SWAP},
    {"SYSTEM", TOKEN_SYSTEM},
    {"TAB", TOKEN_TAB},
    {"TAN", TOKEN_TAN},
    {"THEN", TOKEN_THEN},
    {"TIME$", TOKEN_TIME},
    {"TIMER", TOKEN_TIMER},
    {"TO", TOKEN_TO},
    {"TRIM$", TOKEN_TRIM},
    {"UCASE$", TOKEN_UCASE},
    {"USING", TOKEN_USING},
    {"VAL", TOKEN_VAL},
    {"VARPTR", TOKEN_VARPTR},
    {"VIEW", TOKEN_VIEW},
    {"WEND", TOKEN_WEND},
    {"WHILE", TOKEN_WHILE},
    {"WINDOW", TOKEN_WINDOW},
    {"XOR", TOKEN_XOR},
    {"_FONT", TOKEN_FONT},
    {"_FREEFONT", TOKEN_FREEFONT},
    {"_FREEIMAGE", TOKEN_FREEIMAGE},
    {"_LOADFONT", TOKEN_LOADFONT},
    {"_LOADIMAGE", TOKEN_LOADIMAGE},
    {"_NEWIMAGE", TOKEN_NEWIMAGE},
    {"_PRINTSTRING", TOKEN_PRINTSTRING},
    {"_PRINTWIDTH", TOKEN_PRINTWIDTH},
    {"_PUTIMAGE", TOKEN_PUTIMAGE},
    {"_INFLATE$", TOKEN_INFLATE},
};

#define KEYWORD_COUNT (sizeof(keyword_table) / sizeof(keyword_table[0]))
#define KEYWORD_HASH_CAPACITY 512
#define KEYWORD_HASH_MASK (KEYWORD_HASH_CAPACITY - 1)

static int keyword_hash_table[KEYWORD_HASH_CAPACITY];
static int keyword_hash_initialized = 0;

static unsigned int hash_keyword(const char *word) {
    unsigned int hash = 5381;
    unsigned char c;
    while ((c = (unsigned char)*word++) != '\0') {
        hash = ((hash << 5) + hash) + (unsigned int)toupper(c);
    }
    return hash & KEYWORD_HASH_MASK;
}

static void initialize_keyword_hash(void) {
    if (keyword_hash_initialized) return;

    for (size_t i = 0; i < KEYWORD_HASH_CAPACITY; i++) keyword_hash_table[i] = -1;
    for (size_t keyword_idx = 0; keyword_idx < KEYWORD_COUNT; keyword_idx++) {
        unsigned int slot = hash_keyword(keyword_table[keyword_idx].keyword);
        while (keyword_hash_table[slot] != -1) {
            slot = (slot + 1) & KEYWORD_HASH_MASK;
        }
        keyword_hash_table[slot] = (int)keyword_idx;
    }
    keyword_hash_initialized = 1;
}

static const KeywordMap *find_keyword(const char *word) {
    initialize_keyword_hash();
    unsigned int slot = hash_keyword(word);
    for (size_t probe = 0; probe < KEYWORD_HASH_CAPACITY; probe++) {
        int keyword_idx = keyword_hash_table[slot];
        if (keyword_idx == -1) return NULL;
        if (strcasecmp(word, keyword_table[keyword_idx].keyword) == 0) {
            return &keyword_table[keyword_idx];
        }
        slot = (slot + 1) & KEYWORD_HASH_MASK;
    }
    return NULL;
}

Token get_next_token(const char **input) {
    Token token;
    token.text[0] = '\0';
    token.is_double = 0;
    token.int_val = 0;
    token.double_val = 0.0;
    token.var_idx = -1;
    token.type_generation = 0;
    
    while (isspace(**input)) (*input)++;

    if (**input == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    if (isdigit(**input) || **input == '.') {
        char buffer[32];
        int i = 0;
        int is_double = 0;
        while (isdigit(**input) || **input == '.' || toupper(**input) == 'E' || toupper(**input) == 'D') {
            char c = **input;
            if (toupper(c) == 'D') is_double = 1;
            
            // atof expects 'E' for exponents; convert 'D' internally
            buffer[i++] = (toupper(c) == 'D') ? 'E' : c;
            (*input)++;
            
            // Handle signs in exponents
            if (toupper(c) == 'E' || toupper(c) == 'D') {
                if (**input == '+' || **input == '-') buffer[i++] = *(*input)++;
            }
        }
        
        // Handle suffixes
        if (**input == '!') { (*input)++; } 
        else if (**input == '#') { (*input)++; is_double = 1; }
        else if (**input == '%') { (*input)++; }
        
        // Rule: numbers with more than 7 digits or a dot are double in some dialects,
        // but IBM BASICA treats any number with # or D as double. 
        // Without suffix, if it has a dot or E, it's single.
        buffer[i] = '\0';
        token.type = TOKEN_NUMBER;
        token.double_val = atof(buffer);
        token.is_double = is_double;
        
        if (!is_double) {
            // Force single precision truncation
            token.double_val = (double)((float)token.double_val);
        }
        token.int_val = (int)token.double_val;
        return token;
    }

    if (**input == '"') {
        (*input)++;
        int i = 0;
        while (**input != '"' && **input != '\0') token.text[i++] = *(*input)++;
        if (**input == '"') (*input)++;
        token.text[i] = '\0';
        token.type = TOKEN_STRING;
        return token;
    }

    if (**input == '\'') {
        // Skip to end of line for BASIC single-quote comments
        while (**input != '\0' && **input != '\n') (*input)++;
        token.type = TOKEN_APOSTROPHE; // Treat apostrophe as a comment token
        return token;
    }

    if (**input == '=') {
        (*input)++;
        token.type = TOKEN_EQUALS;
        return token;
    }
    if (**input == ',') { (*input)++; token.type = TOKEN_COMMA; return token; }
    if (**input == ';') { (*input)++; token.type = TOKEN_SEMICOLON; return token; }
    if (**input == ':') { (*input)++; token.type = TOKEN_COLON; return token; }
    if (**input == '<') { (*input)++; token.type = TOKEN_LESS; return token; }
    if (**input == '>') { (*input)++; token.type = TOKEN_GREATER; return token; }

    if (**input == '+') { (*input)++; token.type = TOKEN_PLUS; return token; }
    if (**input == '-') { (*input)++; token.type = TOKEN_MINUS; return token; }
    if (**input == '*') { (*input)++; token.type = TOKEN_STAR; return token; }
    if (**input == '/') { (*input)++; token.type = TOKEN_SLASH; return token; }
    if (**input == '^') { (*input)++; token.type = TOKEN_POWER; return token; }
    if (**input == '(') { (*input)++; token.type = TOKEN_LPAREN; return token; }
    if (**input == ')') { (*input)++; token.type = TOKEN_RPAREN; return token; }
    if (**input == '\\') { (*input)++; token.type = TOKEN_IDIV; return token; }
    if (**input == '#') { (*input)++; token.type = TOKEN_HASH; strcpy(token.text, "#"); return token; }

    char buffer[BASIC_TOKEN_TEXT_MAX];
    int i = 0;
    // Accept alphanumeric, type suffixes ($, %, !, #), and underscore in identifiers
    while ((isalnum(**input) || **input == '$' || **input == '%' || **input == '!' || **input == '#' || **input == '_') && i < BASIC_TOKEN_TEXT_MAX - 1) buffer[i++] = *(*input)++;
    buffer[i] = '\0';

    const KeywordMap *res = find_keyword(buffer);

    if (res) {
        token.type = res->type;
        if (token.type == TOKEN_REM) {
            while (**input != '\0' && **input != '\n') (*input)++;
        }
    } else {
        token.type = TOKEN_IDENTIFIER;
        strcpy(token.text, buffer);
        if (i > 0 && buffer[i-1] == '#') token.is_double = 1;
    }

    if (i == 0 && **input != '\0') {
        token.type = TOKEN_ERROR;
        token.text[0] = *(*input)++;
        token.text[1] = '\0';
    }
    return token;
}

void tokenize_line(Statement *stmt) {
    static const unsigned char POSTFIX_MASK[UCHAR_MAX + 1] = {
        ['$'] = 1, ['%'] = 1, ['!'] = 1, ['#'] = 1
    };
    const char *ptr = stmt->raw_command;
    int capacity = 16;
    stmt->tokens = malloc(capacity * sizeof(Token));
    stmt->token_count = 0;
    while (1) {
        const char *token_start = ptr;
        Token t = get_next_token(&ptr);
        if (t.type == TOKEN_EOF) break;
        t.start_ptr = token_start;
        /* Keep one slot for an internal EOF sentinel used by token-stream lookahead. */
        if (stmt->token_count + 1 >= capacity) {
            capacity *= 2;
            stmt->tokens = realloc(stmt->tokens, capacity * sizeof(Token));
        }
        stmt->tokens[stmt->token_count++] = t;
        stmt->tokens[stmt->token_count - 1].var_idx = -1; // Default to -1 (not found)

        // Pre-computed bitmask table for fast character classification
        if (t.type == TOKEN_IDENTIFIER) {
            int len = strlen(t.text);
            if (len > 0 && POSTFIX_MASK[(unsigned char)t.text[len - 1]]) {
                stmt->tokens[stmt->token_count - 1].var_idx = find_variable(t.text);
            }
        }
    }

    stmt->tokens[stmt->token_count] = (Token){
        .type = TOKEN_EOF,
        .start_ptr = ptr,
        .var_idx = -1,
        .type_generation = 0
    };
}

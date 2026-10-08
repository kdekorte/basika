#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include "lexer.h"

/* Reads a numeric literal's type suffix (!, #, %, &, %%, && or one of the
 * ~ unsigned forms) and returns the NumKind it implies, or 0 if there is none. */
static int read_number_suffix(const char **input) {
    const char *p = *input;
    int is_unsigned = 0;
    if (p[0] == '~' && (p[1] == '%' || p[1] == '&')) {
        is_unsigned = 1;
        p++;
    }
    int kind = 0;
    if (!is_unsigned && *p == '!') { kind = NUM_SINGLE; p++; }
    else if (!is_unsigned && *p == '#') { kind = NUM_DOUBLE; p++; }
    else if (p[0] == '%' && p[1] == '%') { kind = NUM_LONG; p += 2; }
    else if (p[0] == '%') { kind = is_unsigned ? NUM_LONG : NUM_INTEGER; p++; }
    else if (p[0] == '&' && p[1] == '&' && !isalnum((unsigned char)p[2])) {
        kind = is_unsigned ? NUM_UINT64 : NUM_INT64;
        p += 2;
    } else if (p[0] == '&' && p[1] != '&' && !isalnum((unsigned char)p[1])) {
        kind = is_unsigned ? NUM_INT64 : NUM_LONG;
        p++;
    }
    if (kind) *input = p;
    return kind;
}

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
    {"CASE", TOKEN_CASE},
    {"CHAIN", TOKEN_CHAIN},
    {"CHDIR", TOKEN_CHDIR},
    {"CHR$", TOKEN_CHR},
    {"CIRCLE", TOKEN_CIRCLE},
    {"CINT", TOKEN_CINT},
    {"CLNG", TOKEN_CLNG},
    {"CSNG", TOKEN_CSNG},
    {"CDBL", TOKEN_CDBL},
    {"CLEAR", TOKEN_CLEAR},
    {"CLOSE", TOKEN_CLOSE},
    {"CLS", TOKEN_CLS},
    {"COLOR", TOKEN_COLOR},
    {"COMMAND$", TOKEN_COMMANDS},
    {"COMMON", TOKEN_COMMON},
    {"CONST", TOKEN_CONST},
    {"CONT", TOKEN_CONT},
    {"COS", TOKEN_COS},
    {"CVD", TOKEN_CVD},
    {"CVI", TOKEN_CVI},
    {"CVL", TOKEN_CVL},
    {"LBOUND", TOKEN_LBOUND},
    {"UBOUND", TOKEN_UBOUND},
    {"CVS", TOKEN_CVS},
    {"DATA", TOKEN_DATA},
    {"DATE$", TOKEN_DATE},
    {"DECLARE", TOKEN_DECLARE},
    {"_DEFLATE$", TOKEN_DEFLATE},
    {"DEF", TOKEN_DEF},
    {"DEFDBL", TOKEN_DEFDBL},
    {"DEFINT", TOKEN_DEFINT},
    {"DEFLNG", TOKEN_DEFLNG},
    {"DEFSNG", TOKEN_DEFSNG},
    {"DEFSTR", TOKEN_DEFSTR},
    {"DELETE", TOKEN_DELETE},
    {"DIM", TOKEN_DIM},
    {"DO", TOKEN_DO},
    {"DRAW", TOKEN_DRAW},
    {"ELSE", TOKEN_ELSE},
    {"ELSEIF", TOKEN_ELSEIF},
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
    {"INPUT$", TOKEN_INPUTS},
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
    {"LOCK", TOKEN_LOCK},
    {"LOF", TOKEN_LOF},
    {"LOG", TOKEN_LOG},
    {"LOOP", TOKEN_LOOP},
    {"LSET", TOKEN_LSET},
    {"LTRIM$", TOKEN_LTRIM},
    {"MID$", TOKEN_MID},
    {"MKD$", TOKEN_MKD},
    {"MKDIR", TOKEN_MKDIR},
    {"MKI$", TOKEN_MKI},
    {"MKL$", TOKEN_MKL},
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
    {"PALETTE", TOKEN_PALETTE},
    {"PCOPY", TOKEN_PCOPY},
    {"PEEK", TOKEN_PEEK},
    {"PEN", TOKEN_PEN},
    {"PLAY", TOKEN_PLAY},
    {"POKE", TOKEN_POKE},
    {"PRESET", TOKEN_PRESET},
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
    {"SELECT", TOKEN_SELECT},
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
    {"STICK", TOKEN_STICK},
    {"STOP", TOKEN_STOP},
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
    {"TYPE", TOKEN_TYPE},
    {"TRIM$", TOKEN_TRIM},
    {"UCASE$", TOKEN_UCASE},
    {"UNLOCK", TOKEN_UNLOCK},
    {"UNTIL", TOKEN_UNTIL},
    {"USING", TOKEN_USING},
    {"VAL", TOKEN_VAL},
    {"VARPTR", TOKEN_VARPTR},
    {"VIEW", TOKEN_VIEW},
    {"WAIT", TOKEN_WAIT},
    {"WEND", TOKEN_WEND},
    {"WHILE", TOKEN_WHILE},
    {"WIDTH", TOKEN_WIDTH},
    {"WINDOW", TOKEN_WINDOW},
    {"WRITE", TOKEN_WRITE},
    {"XOR", TOKEN_XOR},
    {"EQV", TOKEN_EQV},
    {"IMP", TOKEN_IMP},
    {"_AUTODISPLAY", TOKEN_AUTODISPLAY},
    {"_DELAY", TOKEN_DELAY},
    {"_DEST", TOKEN_DEST},
    {"_DISPLAY", TOKEN_DISPLAY},
    {"_FONT", TOKEN_FONT},
    {"_FREEFONT", TOKEN_FREEFONT},
    {"_FREEIMAGE", TOKEN_FREEIMAGE},
    {"_LOADFONT", TOKEN_LOADFONT},
    {"_LIMIT", TOKEN_LIMIT},
    {"_LOADIMAGE", TOKEN_LOADIMAGE},
    {"_NEWIMAGE", TOKEN_NEWIMAGE},
    {"_PRINTSTRING", TOKEN_PRINTSTRING},
    {"_PRINTWIDTH", TOKEN_PRINTWIDTH},
    {"_PUTIMAGE", TOKEN_PUTIMAGE},
    {"_SOURCE", TOKEN_SOURCE},
    {"_TITLE", TOKEN_TITLE},
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

static int noprefix_enabled = 0;

void lexer_set_noprefix(int enabled) { noprefix_enabled = enabled != 0; }

/* QB64 names the interpreter recognizes by their text rather than as
 * keywords; with $NOPREFIX they may be written without the "_". */
static const char *find_qb64_name(const char *name) {
    static const char *const names[] = {
        "_RGB32", "_RGBA32", "_RGB", "_RGBA", "_RED32", "_GREEN32", "_BLUE32", "_ALPHA32",
        "_RED", "_GREEN", "_BLUE", "_ALPHA", "_PI", "_WIDTH", "_HEIGHT", "_COPYIMAGE",
        "_KEYDOWN", "_KEYHIT", "_MOUSEX", "_MOUSEY", "_MOUSEBUTTON", "_MOUSEINPUT", "_MOUSEWHEEL",
        "_RESIZE", "_RESIZEWIDTH", "_RESIZEHEIGHT", "_ROUND", "_PRINTMODE",
        "_KEEPBACKGROUND", "_ONLYBACKGROUND", "_FILLBACKGROUND", "_STRETCH", "_SMOOTH",
        "_SQUAREPIXELS", "_FULLSCREEN", "_EXPLICIT", "_EXPLICITARRAY",
        "_UNSIGNED", "_BYTE", "_INTEGER64", "_FLOAT",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcasecmp(name, names[i]) == 0) return names[i];
    }
    return NULL;
}

Token get_next_token(const char **input) {
    Token token;
    token.text[0] = '\0';
    token.is_double = 0;
    token.int_val = 0;
    token.double_val = 0.0;
    token.int64_val = 0;
    token.num_kind = 0;
    token.var_idx = -1;
    token.type_generation = 0;
    token.proc_cache = NULL;
    token.proc_generation = 0;
    token.const_cache = -1;
    token.const_generation = 0;
    token.string_cache = -1;
    token.string_generation = 0;

    while (isspace(**input)) (*input)++;

    if (**input == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    if (isdigit((unsigned char)**input) ||
        (**input == '.' && isdigit((unsigned char)(*input)[1]))) {
        char buffer[32];
        int i = 0;
        int is_double = 0;
        int single_exponent = 0;
        while ((isdigit(**input) || **input == '.' || toupper(**input) == 'E' || toupper(**input) == 'D') &&
               i < (int)sizeof(buffer) - 3) {
            char c = **input;
            if (toupper(c) == 'D') is_double = 1;
            if (toupper(c) == 'E') single_exponent = 1;
            
            // atof expects 'E' for exponents; convert 'D' internally
            buffer[i++] = (toupper(c) == 'D') ? 'E' : c;
            (*input)++;
            
            // Handle signs in exponents
            if (toupper(c) == 'E' || toupper(c) == 'D') {
                if (**input == '+' || **input == '-') buffer[i++] = *(*input)++;
            }
        }
        
        // Handle suffixes
        int suffix_kind = read_number_suffix(input);
        int single_suffix = suffix_kind == NUM_SINGLE;
        int long_suffix = suffix_kind != 0 && suffix_kind != NUM_INTEGER && suffix_kind < NUM_SINGLE;
        if (suffix_kind == NUM_DOUBLE) is_double = 1;

        buffer[i] = '\0';
        // As in QBasic, an unsuffixed literal with more than 7 significant
        // digits is DOUBLE; an E exponent or ! suffix keeps it SINGLE.
        if (!single_suffix && !single_exponent && !is_double) {
            int significant = 0, leading = 1;
            for (const char *d = buffer; *d && toupper((unsigned char)*d) != 'E'; d++) {
                if (!isdigit((unsigned char)*d)) continue;
                if (leading && *d == '0') continue;
                leading = 0;
                significant++;
            }
            if (significant > 7) is_double = 1;
        }
        token.type = TOKEN_NUMBER;
        token.double_val = atof(buffer);
        token.is_double = is_double;

        // Whole-number literals are exact: INTEGER up to 32767 and LONG up
        // to 2147483647 as in QBasic, then _INTEGER64 (or _UNSIGNED
        // _INTEGER64) as in QB64. A suffix can widen the type. Other
        // literals are SINGLE unless DOUBLE.
        int is_whole = strpbrk(buffer, ".eE") == NULL && suffix_kind < NUM_SINGLE;
        errno = 0;
        unsigned long long whole = is_whole ? strtoull(buffer, NULL, 10) : 0;
        if (is_whole && errno != ERANGE) {
            int kind;
            if (whole <= 32767ULL) kind = NUM_INTEGER;
            else if (whole <= 2147483647ULL) kind = NUM_LONG;
            else if (whole <= 9223372036854775807ULL) kind = NUM_INT64;
            else kind = NUM_UINT64;
            if (suffix_kind > kind) kind = suffix_kind;
            token.num_kind = kind;
            token.int64_val = (int64_t)whole;
            token.double_val = kind == NUM_UINT64 ? (double)whole : (double)(int64_t)whole;
            if (long_suffix || whole > 9999999ULL) token.is_double = 1;
        } else if (suffix_kind && suffix_kind < NUM_SINGLE) {
            // 2.5% rounds to a whole number of the suffix's type.
            token.num_kind = suffix_kind;
            token.int64_val = (int64_t)nearbyint(token.double_val);
            token.double_val = (double)token.int64_val;
            if (long_suffix) token.is_double = 1;
        } else if (!is_double) {
            // Force single precision truncation
            token.double_val = (double)((float)token.double_val);
            token.num_kind = NUM_SINGLE;
        } else {
            token.num_kind = NUM_DOUBLE;
        }
        token.int_val = fabs(token.double_val) < 2147483648.0 ? (int)token.double_val : 0;
        return token;
    }

    // &H, &O and &B literals follow QBasic typing: up to 16 significant bits
    // is a signed INTEGER, up to 32 a signed LONG and wider values a signed
    // _INTEGER64. A suffix picks the type, so &HFFFF& is 65535 and
    // &HFFFFFFFFFFFFFFFF~&& is the largest _UNSIGNED _INTEGER64.
    if (**input == '&') {
        int radix = 0;
        char prefix = (char)toupper((unsigned char)(*input)[1]);
        if (prefix == 'H') radix = 16;
        else if (prefix == 'O') radix = 8;
        else if (prefix == 'B') radix = 2;
        if (radix) {
            const char *p = *input + 2;
            unsigned long long value = 0;
            int bits = 0;
            int digit_count = 0;
            while (1) {
                int d;
                char c = (char)toupper((unsigned char)*p);
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else break;
                if (d >= radix) break;
                value = value * (unsigned long long)radix + (unsigned long long)d;
                digit_count++;
                p++;
            }
            if (digit_count > 0) {
                for (unsigned long long v = value; v; v >>= 1) bits++;
                const char *suffix_start = p;
                int suffix_kind = read_number_suffix(&p);
                if (suffix_kind >= NUM_SINGLE) {
                    // &H10# is not a typed hex literal; leave the suffix alone.
                    p = suffix_start;
                    suffix_kind = 0;
                }
                int kind = bits <= 16 ? NUM_INTEGER : bits <= 32 ? NUM_LONG : NUM_INT64;
                if (suffix_kind > kind) kind = suffix_kind;
                int64_t result;
                if (kind == NUM_INTEGER) {
                    result = (int16_t)(uint16_t)value;
                } else if (kind == NUM_LONG) {
                    result = bits <= 16 ? (int64_t)value : (int32_t)(uint32_t)value;
                } else {
                    result = (int64_t)value; // &HFFFFFFFF&& is 4294967295
                }
                *input = p;
                token.type = TOKEN_NUMBER;
                token.num_kind = kind;
                token.int64_val = result;
                token.double_val = kind == NUM_UINT64 ? (double)value : (double)result;
                token.int_val = (int)(int32_t)result;
                token.is_double = bits > 16 || kind != NUM_INTEGER;
                return token;
            }
        }
    }

    if (**input == '.') {
        (*input)++;
        token.type = TOKEN_DOT;
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
    // Dots followed by an identifier character are member-chain separators.
    while (i < BASIC_TOKEN_TEXT_MAX - 1) {
        if (isalnum(**input) || **input == '$' || **input == '%' || **input == '!' || **input == '#' || **input == '_') {
            buffer[i++] = *(*input)++;
        } else if (**input == '.' && (isalpha((unsigned char)(*input)[1]) || (*input)[1] == '_')) {
            buffer[i++] = *(*input)++;
        } else {
            break;
        }
    }
    // QB64 suffixes end an identifier: & LONG, && _INTEGER64, and ~ for the
    // unsigned forms (~% ~%% ~& ~&&). % and %% are read by the loop above.
    if (i > 0 && i < BASIC_TOKEN_TEXT_MAX - 4) {
        if (**input == '~' && ((*input)[1] == '&' || (*input)[1] == '%')) {
            char kind = (*input)[1];
            buffer[i++] = *(*input)++;
            buffer[i++] = *(*input)++;
            if (**input == kind) buffer[i++] = *(*input)++;
        } else if (**input == '&' && !isalnum((unsigned char)(*input)[1])) {
            buffer[i++] = *(*input)++;
            if (**input == '&' && !isalnum((unsigned char)(*input)[1])) buffer[i++] = *(*input)++;
        }
    }
    buffer[i] = '\0';

    const KeywordMap *res = find_keyword(buffer);
    const char *qb64_name = NULL;
    if (!res && noprefix_enabled && i > 0 && i < BASIC_TOKEN_TEXT_MAX - 1 && isalpha((unsigned char)buffer[0])) {
        char prefixed[BASIC_TOKEN_TEXT_MAX];
        prefixed[0] = '_';
        memcpy(prefixed + 1, buffer, (size_t)i + 1);
        res = find_keyword(prefixed);
        if (!res) qb64_name = find_qb64_name(prefixed);
    }

    if (res) {
        token.type = res->type;
        if (token.type == TOKEN_REM) {
            while (**input != '\0' && **input != '\n') (*input)++;
        }
    } else {
        token.type = TOKEN_IDENTIFIER;
        strcpy(token.text, qb64_name ? qb64_name : buffer);
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

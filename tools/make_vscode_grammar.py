#!/usr/bin/env python3
"""Regenerate the VS Code syntax highlighting from the lexer's keyword tables.

Run from the repository root:  python3 tools/make_vscode_grammar.py
"""
import json
import re

lexer = open("src/lexer.c").read()
table = lexer[lexer.index("keyword_table[]"):lexer.index("keyword_hash_table")]
keywords = re.findall(r'\{"([A-Z_][A-Z0-9_$]*)",\s*TOKEN_', table)
names = lexer[lexer.index("find_qb64_name"):]
names = names[:names.index("};")]
qb64 = re.findall(r'"(_[A-Z0-9_$]+)"', names)

def pattern(words):
    # Longest first; $ ends names, so \b would not work after it.
    words = sorted(set(words), key=lambda w: (-len(w), w))
    return r"(?i)(?<![A-Za-z0-9_.$])(" + "|".join(re.escape(w) for w in words) + r")(?![A-Za-z0-9_])"

statements = {"IF", "THEN", "ELSE", "ELSEIF", "END", "FOR", "NEXT", "TO", "STEP", "DO", "LOOP",
              "WHILE", "WEND", "UNTIL", "SELECT", "CASE", "GOTO", "GOSUB", "RETURN", "EXIT",
              "SUB", "FUNCTION", "CALL", "ON", "RESUME", "STOP", "SYSTEM", "QUIT", "RUN", "CHAIN"}
operators = {"AND", "OR", "NOT", "XOR", "EQV", "IMP", "MOD"}
storage = {"DIM", "AS", "TYPE", "CONST", "SHARED", "STATIC", "COMMON", "DECLARE", "DEF", "OPTION",
           "DEFINT", "DEFLNG", "DEFSNG", "DEFDBL", "DEFSTR", "REDIM", "ERASE", "LET"}

control = [k for k in keywords if k in statements]
ops = [k for k in keywords if k in operators]
store = [k for k in keywords if k in storage]
types = ["INTEGER", "LONG", "SINGLE", "DOUBLE", "STRING", "_BYTE", "_INTEGER64", "_FLOAT",
         "_UNSIGNED", "_BIT", "_OFFSET", "_MEM"]
rest = [k for k in keywords if k not in statements | operators | storage and k != "REM"]
qb64_all = [w for w in set(qb64) | {k for k in keywords if k.startswith("_")}]
builtins = [w for w in rest if not w.startswith("_")] + [w for w in qb64_all if w not in rest or True]
builtins = sorted(set(w for w in rest if not w.startswith("_")))
qb64_all = sorted(set(w for w in qb64_all if w not in types))

grammar = {
    "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
    "name": "Basika BASIC",
    "scopeName": "source.basika",
    "patterns": [{"include": "#comments"}, {"include": "#strings"}, {"include": "#metacommands"},
                 {"include": "#numbers"}, {"include": "#keywords"}, {"include": "#labels"}],
    "repository": {
        "comments": {"patterns": [
            {"name": "comment.line.apostrophe.basika", "match": "'.*$"},
            {"name": "comment.line.rem.basika", "match": r"(?i)(?<![A-Za-z0-9_.$])REM\b.*$"}]},
        "strings": {"name": "string.quoted.double.basika", "begin": '"', "end": '"|$'},
        "metacommands": {"name": "meta.preprocessor.basika", "match": r"(?i)^\s*\$[A-Za-z]+.*$"},
        "numbers": {"patterns": [
            {"name": "constant.numeric.hex.basika", "match": r"(?i)&H[0-9A-F]+(?:~?&&?|%)?"},
            {"name": "constant.numeric.octal.basika", "match": r"(?i)&O?[0-7]+(?:~?&&?|%)?"},
            {"name": "constant.numeric.binary.basika", "match": r"(?i)&B[01]+(?:~?&&?|%)?"},
            {"name": "constant.numeric.basika",
             "match": r"(?i)(?<![A-Za-z0-9_$])(?:\d+\.?\d*|\.\d+)(?:[ED][+-]?\d+)?(?:~?&&|[%&!#])?"}]},
        "keywords": {"patterns": [
            {"name": "keyword.control.basika", "match": pattern(control)},
            {"name": "keyword.operator.word.basika", "match": pattern(ops)},
            {"name": "storage.type.basika", "match": pattern(types)},
            {"name": "storage.modifier.basika", "match": pattern(store)},
            {"name": "support.function.qb64.basika", "match": pattern(qb64_all)},
            {"name": "support.function.basika", "match": pattern(builtins)}]},
        "labels": {"name": "entity.name.label.basika", "match": r"^\s*[A-Za-z_][A-Za-z0-9_]*(?=:)"},
    },
}
json.dump(grammar, open("editors/vscode/syntaxes/basika.tmLanguage.json", "w"), indent=2)
open("editors/vscode/syntaxes/basika.tmLanguage.json", "a").write("\n")
print(len(control), len(ops), len(store), len(builtins), len(qb64_all))

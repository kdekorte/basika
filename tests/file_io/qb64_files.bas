' QB64 file and folder functions: _FILEEXISTS, _DIREXISTS, _DIR$, _CWD$,
' _STARTDIR$, _FULLPATH$, _FILES$, _READFILE$ and _WRITEFILE. Folders come
' back with a trailing "/", true is -1, and the paths that depend on the
' machine are checked by their shape. Everything happens in tests/qb64_files_tmp.
DIM SHARED errs AS STRING
ON ERROR GOTO Trap
base$ = _CWD$
MKDIR "tests/qb64_files_tmp"
CHDIR "tests/qb64_files_tmp"
MKDIR "sub"
_WRITEFILE "a.txt", "alpha"
_WRITEFILE "b.bas", ""
_WRITEFILE "sub/c.txt", "c"

PRINT "FILEEXISTS file, folder, none:"; _FILEEXISTS("a.txt"); _FILEEXISTS("sub"); _FILEEXISTS("none")
PRINT "DIREXISTS folder, file, none:"; _DIREXISTS("sub"); _DIREXISTS("a.txt"); _DIREXISTS("none")
IF _FILEEXISTS("sub/c.txt") AND NOT _FILEEXISTS("") THEN PRINT "paths and empty names ok"

' _CWD$ follows CHDIR; _STARTDIR$ is where the program started.
IF RIGHT$(_CWD$, 1) = "/" AND RIGHT$(_CWD$, 22) = "/tests/qb64_files_tmp/" THEN PRINT "CWD$ ok"
IF _STARTDIR$ = base$ THEN PRINT "STARTDIR$ is the starting folder"
CHDIR "sub"
IF RIGHT$(_CWD$, 26) = "/tests/qb64_files_tmp/sub/" THEN PRINT "CWD$ follows CHDIR"
CHDIR ".."

' _FULLPATH$: files without a "/", folders with one.
IF _FULLPATH$("a.txt") = _CWD$ + "a.txt" THEN PRINT "FULLPATH$ file ok"
IF _FULLPATH$("sub/../sub") = _CWD$ + "sub/" THEN PRINT "FULLPATH$ folder ok"
IF _FULLPATH$(".") = _CWD$ THEN PRINT "FULLPATH$ . ok"

' _DIR$: special folders end in "/"; an unknown name is the desktop.
IF RIGHT$(_DIR$("temp"), 1) = "/" AND RIGHT$(_DIR$("home"), 1) = "/" THEN PRINT "DIR$ ok"
IF _DIR$("DESKTOP") = _DIR$("desktop") AND _DIR$("nonsense") = _DIR$("desktop") THEN PRINT "DIR$ names ok"
IF RIGHT$(_DIR$("desktop"), 8) = "Desktop/" THEN PRINT "DIR$ desktop ok"

' _FILES$: the first match, then the rest one per call, then "".
PRINT "FILES$ *:"; SortedFiles$("*")
PRINT "FILES$ sub:"; SortedFiles$("sub")
PRINT "FILES$ *.txt:"; SortedFiles$("*.txt")
PRINT "FILES$ none: ["; _FILES$("*.none"); "]"

' _READFILE$ and _WRITEFILE keep every byte; _WRITEFILE replaces.
b$ = CHR$(0) + "bin" + CHR$(255) + CHR$(13) + CHR$(10) + "end"
_WRITEFILE "bin.dat", b$
r$ = _READFILE$("bin.dat")
PRINT "READFILE$ length"; LEN(r$)
IF r$ = b$ THEN PRINT "READFILE$ keeps every byte"
_WRITEFILE "bin.dat", "short"
PRINT "WRITEFILE replaces: "; _READFILE$("bin.dat")
PRINT "READFILE$ empty file:"; LEN(_READFILE$("b.bas"))

PRINT "--- errors"
x$ = _FULLPATH$("none/nothing"): PRINT "FULLPATH$ missing:"; errs
x$ = _READFILE$("none.txt"): PRINT "READFILE$ missing:"; errs
_WRITEFILE "none/x.txt", "x": PRINT "WRITEFILE no folder:"; errs

KILL "bin.dat": KILL "a.txt": KILL "b.bas": KILL "sub/c.txt"
RMDIR "sub"
CHDIR base$
RMDIR "tests/qb64_files_tmp"
PRINT "cleaned up:"; NOT _DIREXISTS("tests/qb64_files_tmp")
END

Trap:
errs = STR$(ERR)
RESUME NEXT

FUNCTION SortedFiles$ (spec$)
    DIM names(20) AS STRING, n AS INTEGER, i AS INTEGER, j AS INTEGER, t$
    f$ = _FILES$(spec$)
    DO WHILE LEN(f$) AND n < 20
        names(n) = f$: n = n + 1
        f$ = _FILES$
    LOOP
    FOR i = 0 TO n - 2
        FOR j = i + 1 TO n - 1
            IF names(j) < names(i) THEN t$ = names(i): names(i) = names(j): names(j) = t$
        NEXT
    NEXT
    t$ = ""
    FOR i = 0 TO n - 1: t$ = t$ + " " + names(i): NEXT
    SortedFiles$ = t$
END FUNCTION

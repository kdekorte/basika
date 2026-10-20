#!/usr/bin/env basika
' OS operations demo for executable .bas scripts.
'
' The shebang line above lets this file run directly, like a shell script:
'
'     chmod +x demo/os_shebang_demo.bas
'     ./demo/os_shebang_demo.bas
'
' It lists the files in the current folder into a temporary file, reads
' them into an internal list, uppercases every name in that list, writes
' the uppercased list to a second file, reads that file back to validate
' it, and then cleans up both temporary files.

rawfile$ = "os_demo_files.tmp"
upfile$ = "os_demo_upper.tmp"

PRINT "Basika shebang demo: OS level file operations"
PRINT "Working folder: "; _CWD$
PRINT

' 1. List every file in the current folder into a file. FILES with a
'    redirect writes one name per line instead of showing the listing.
PRINT "1. Listing files into "; rawfile$
FILES "*", rawfile$
IF NOT _FILEEXISTS(rawfile$) THEN
    PRINT "FAIL: could not create "; rawfile$
    END
END IF

' 2. Read the listing into an internal list, then uppercase every name.
DIM names$(1000)
count = 0
OPEN rawfile$ FOR INPUT AS #1
DO WHILE NOT EOF(1) AND count < 1000
    LINE INPUT #1, f$
    IF LEN(f$) > 0 THEN count = count + 1: names$(count) = f$
LOOP
IF NOT EOF(1) THEN PRINT "Warning: more than 1000 entries, list truncated"
CLOSE #1
FOR i = 1 TO count
    names$(i) = UCASE$(names$(i))
NEXT i
PRINT "2. Read"; count; "names and uppercased them in the internal list"

' 3. Write the uppercased list to a file.
OPEN upfile$ FOR OUTPUT AS #1
FOR i = 1 TO count
    PRINT #1, names$(i)
NEXT i
CLOSE #1
PRINT "3. Wrote uppercased list to "; upfile$

' 4. Validate: read the file back. It is valid when it holds the same
'    names in the same order, and every name is fully uppercase.
valid = _FILEEXISTS(upfile$)
vcount = 0
matched = 0
IF valid THEN
    OPEN upfile$ FOR INPUT AS #1
    DO WHILE NOT EOF(1) AND valid
        LINE INPUT #1, f$
        IF LEN(f$) > 0 THEN
            vcount = vcount + 1
            IF vcount <= count AND f$ = names$(vcount) AND f$ = UCASE$(f$) THEN
                matched = matched + 1
            ELSE
                valid = 0
            END IF
        END IF
    LOOP
    CLOSE #1
    IF vcount <> count THEN valid = 0
END IF
IF valid THEN
    PRINT "4. Validation PASS:"; matched; "of"; count; "names match, all uppercase"
ELSE
    PRINT "4. Validation FAIL"
END IF

' 5. Clean up the temporary files.
KILL rawfile$
KILL upfile$
IF NOT _FILEEXISTS(rawfile$) AND NOT _FILEEXISTS(upfile$) THEN
    PRINT "5. Cleanup PASS: temporary files removed"
ELSE
    PRINT "5. Cleanup FAIL: temporary files remain"
END IF

IF valid THEN PRINT "Demo finished successfully" ELSE PRINT "Demo finished with errors"
END

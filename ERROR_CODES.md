# Basika Error Codes

This document lists the runtime error codes supported by Basika. These codes can be retrieved using the `ERR` variable within a BASIC program, and the line number of the last error can be retrieved using `ERL`.

| Code | Message | Description |
|------|---------|-------------|
| 1 | NEXT without FOR | A `NEXT` statement was encountered without a matching `FOR` loop. |
| 2 | Syntax error | An unrecognized command or invalid syntax was encountered, including malformed graphics command arguments. |
| 3 | RETURN without GOSUB | A `RETURN` statement was encountered without a matching `GOSUB`. |
| 4 | Out of DATA | A `READ` statement was executed but no more `DATA` items are available. |
| 5 | Illegal function call | A function argument is out of its valid range, such as an invalid image/font handle or graphics coordinate. |
| 6 | Overflow | The result of a calculation is too large to be represented. |
| 7 | Out of memory | The program is too large or has too many nested loops/subroutines. |
| 8 | Undefined line number | A `GOTO`, `GOSUB`, or `IF...THEN` referenced a non-existent line. |
| 9 | Subscript out of range | An array index is outside the defined bounds. |
| 10 | Duplicate Definition | An array was dimensioned twice or `OPTION BASE` used after dimensioning. |
| 11 | Division by zero | Division by zero was attempted. |
| 13 | Type mismatch | A string was used where a number was expected, or vice versa. |
| 14 | Out of string space | String memory limit reached. |
| 19 | Can't RESUME | A `RESUME` statement was encountered but no error was trapped. |
| 20 | RESUME without error | A `RESUME` statement was executed but no error occurred. |
| 26 | FOR without NEXT | A `FOR` loop was started but no matching `NEXT` was found. |
| 29 | WHILE without WEND | A `WHILE` loop was started but no matching `WEND` was found. |
| 30 | WEND without WHILE | A `WEND` statement was encountered without a matching `WHILE`. |
| 50 | FIELD overflow | (Placeholder) A `FIELD` statement exceeded record length. |
| 51 | Internal error | An internal error occurred in the interpreter. |
| 52 | Bad file number | An invalid or unopened file number was referenced. |
| 53 | File not found | The specified file does not exist. Image and font loading failures return handle `0`; callers should handle that result before drawing or selecting the resource. |
| 54 | Bad file mode | An illegal operation for the current file mode was attempted. |
| 55 | File already open | `OPEN` was called for a file number that is already in use. |
| 57 | Device I/O error | An error occurred during hardware input or output. |
| 58 | File already exists | A file with the same name already exists. |
| 59 | Bad record length | The record length specified in `OPEN` is invalid. |
| 61 | Disk full | The storage device is full. |
| 62 | Input past end | Attempted to read past the end of a file. |
| 63 | Bad record number | An invalid record number was used in `GET` or `PUT`. |
| 64 | Bad file name | An invalid filename was used. |
| 67 | Too many files | Too many files are currently open. |
| 70 | Permission Denied | Access to the file or directory was denied. |
| 71 | Disk not ready | The storage device is not accessible. |
| 75 | Path/File access error | An error occurred while accessing a path or file. |
| 76 | Path not found | The specified directory path does not exist. |

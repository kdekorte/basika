10 MKDIR "tests/test_dir"
20 CHDIR "tests/test_dir"
30 MKDIR "nested_dir"
40 RMDIR "nested_dir"
50 CHDIR "../.."
60 RMDIR "tests/test_dir"
70 PRINT "Filesystem operations successful"
80 END
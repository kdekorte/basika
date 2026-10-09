' Comments after TYPE, field and END TYPE lines, and comment-only lines.
TYPE Foe ' an enemy
    ' where it is
    lane AS INTEGER ' which lane
    d AS SINGLE REM depth
    tag AS STRING * 3 ' fixed length
END TYPE ' done
DIM f AS Foe
f.lane = 4: f.d = .5: f.tag = "abc"
PRINT f.lane; f.d; f.tag

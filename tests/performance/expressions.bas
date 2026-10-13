' Expression benchmark: the per-pixel math of a waving-flag demo, without
' graphics. DEFLNG, math and QB64 functions, arrays and AND all run from
' compiled expressions. Prints a checksum.
DEFLNG A-Z
DIM t AS DOUBLE, z AS DOUBLE, xx AS DOUBLE, yy AS DOUBLE, dx AS DOUBLE, dy AS DOUBLE
DIM pal(15)
FOR i = 0 TO 15: pal(i) = i * 17: NEXT
a = 40: w = 300: h = 160
FOR frame = 1 TO 9
    t = t + .2
    FOR y = 0 TO h
        FOR x = 0 TO w
            z = (.1 + .4 * (x / w)) * a * SIN(x / 35 - y / 70 - t) + .5 * a
            dz = 50 * a * COS(x / 35 - y / 70 - t) / 35
            xx = x + z * .707 - a * .707
            yy = y - z * .707
            IF INT(xx) >= 0 AND INT(xx) < w - 1 AND INT(yy) >= 0 AND INT(yy) < h - 1 THEN
                dx = xx - INT(xx)
                dy = yy - INT(yy)
                r = _ROUND((1 - dy) * ((1 - dx) * pal(INT(xx) MOD 16) + dx * pal((INT(xx) + 1) MOD 16)) + dy * pal(INT(yy) MOD 16)) + dz
                IF r < 0 THEN r = 0
                IF r > 255 THEN r = 255
                sum = (sum + r) MOD 1000000
            END IF
        NEXT
    NEXT
NEXT
PRINT "Checksum:"; sum

' QB64 sound handles. Whether a sound is heard depends on the audio device,
' so this checks what does not: loading, lengths, and handle errors.
h& = _SNDOPEN("tests/sound_fixture.wav")
PRINT "handle"; h& > 0; "length"; INT(_SNDLEN(h&) * 100 + .5) / 100
h2& = _SNDOPEN("tests/sound_fixture.wav", "STREAM")
PRINT "second handle"; h2& > h&
PRINT "missing file"; _SNDOPEN("tests/no_such_sound.wav")
_SNDVOL h&, .5
_SNDPLAY h&: _SNDPAUSE h&: _SNDPLAY h&: _SNDSTOP h&
_SNDLOOP h&: _SNDSTOP h&
PRINT "stopped"; _SNDPLAYING(h&); _SNDPAUSED(h&); _SNDGETPOS(h&)
_SNDPLAYCOPY h&, .2: _SNDPLAYCOPY h&
_SNDPLAYFILE "tests/sound_fixture.wav", , .1
_SNDCLOSE h2&
ON ERROR GOTO failed
_SNDPLAY h2&
PRINT _SNDLEN(h2&)
_SNDPLAYFILE "tests/no_such_sound.wav"
_SNDPLAY 0
_SNDCLOSE h&
PRINT "done"
END
failed:
PRINT "error"; ERR
RESUME NEXT

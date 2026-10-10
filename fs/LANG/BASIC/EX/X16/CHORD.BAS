' A chord -- K4510 BASIC, MELODY (OPL2) through the sequencer
' C major for two seconds: PLAY on channels 1, 2 and 3 queues a
' whole note each on the $D5E0 sequencer, and the three sound
' together while the program goes on.  Not timed: a chord is
' not a race.  The X16 twin: compare/x16/x16/CHORD.BAS
' (FMCHORD on the YM2151, PSGCHORD on the VERA).
PRINT "C MAJOR ON MELODY, TWO SECONDS, K4510 BASIC"
PLAY 1, "T120 O4 C1"
PLAY 2, "T120 O4 E1"
PLAY 3, "T120 O4 G1"
SLEEP 2.5
PRINT "CHORD OK"
PRINT "DONE"
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""

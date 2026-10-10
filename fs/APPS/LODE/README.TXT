LODE -- collect the gold, then the exit ladder appears; the top wins.

  arrows   run, climb, hang from the rope (while held)
  Z X      dig left / right (used at the next cell if you are running)
  G        the guards on / off       - +   slower / faster (PACE, in the panel)
  P        pause: resume, the guards, the pace, or end the game
  Esc      back to the title (ESC there leaves)
  E        the level editor, from the title

THE LEVELS are files: LEVEL01.TXT, LEVEL02.TXT ... up to LEVEL20.TXT,
played in order, as many as are here.  Fifteen lines of up to twenty
characters:

  #  brick (diggable)    @  stone (not)    H  ladder    -  rope
  $  gold    E  the exit ladder, hidden until the gold is gone
  P  where you start     G  a guard (three at most)   space  nothing

Edit them with VI, or with the editor: E on the title.  Arrows
move the cursor (a yellow frame), the same letters paint under it, space or X clears,
PgUp/PgDn walk the levels, N makes a new one after the last, S saves
(it insists on one P, an E and some gold), T plays the level from the
editor and comes back, Esc leaves.  The first three levels are the
game's own; LEVEL04.TXT and up are yours.

# The ant farm -- ideas, to talk about

Status: **noted, not started** (Doc, 2026-09-15: "Before you do any of this,
just note it down, and talk to me about it later"). They come after the
sidebar work in `docs/SIDEBARS-PLAN.md`, which gives them their home:
`OPTIONS.CFG` for the settings and `STATE.DAT` so a colony lives long enough
for any of this to matter.

## Doc's list

1. **Days of about 30 minutes, nights the same.** (Today a day is the host's
   real 24 hours.)
2. **The moon shows its real phase** at night.
3. **Weather follows the real weather where you are.** Cold reduces activity.
   - Cloudy days are cloudy in the farm.
   - Rain is rain; **very heavy rain floods the farm and everyone dies**.
   - Snow the same. Ants are much less active in winter, but some activity
     goes on **under the snow cover**.
4. **Ants live 5 to 7 days** and slow down after day 3.
5. **An anteater**: a 1 in 30 chance each day that it comes by and destroys
   most of the colony.
6. **The breakout**: a colony that survives more than a week (adjustable,
   farm time) has a 1 in 10 chance of learning to break out onto the main
   K4510 screen and eat the characters, or whatever is there.
8. **The brood** (Doc, later the same day): if ants die, eggs must hatch,
   the larvae must be fed and the queen tended. So the colony's life is a
   cycle -- queen lays, nurses carry eggs to brood chambers, larvae are fed
   by foragers' food, pupae become workers -- and a colony whose foragers
   bring back nothing, or whose queen is untended, dwindles.
9. **Read up first** (Doc): real ant foraging and in-nest behaviour, to
   model it on -- trail pheromones and how trails reinforce and fade,
   division of labour by age (young ants nurse inside, older ones forage
   outside), how chambers are placed (brood near the warm top by day, deeper
   when cold), the midden. To search before the modelling starts.
7. **The rocket**: a colony that survives more than a week (adjustable) has
   a 1 in 20 chance of building a rocket and blasting off; the sidebar
   scrolls slowly into the space sidebar.

## My notes on them

- **1 -- day length**: becomes the `day` option (`day = 30m` the default,
  `real` still there). With 30-minute days, "a week" is 3.5 hours and a
  5-7 day life is 2.5 to 3.5 hours: the numbers in 4-7 are all farm days, so
  they stay right whatever the day is set to.
- **2 -- the moon**: needs nothing from outside; the phase is arithmetic on
  the date. Easy.
- **3 -- weather**: needs a place and a weather service.
  - The place: typed into `OPTIONS.CFG` (a town or a latitude and
    longitude), never guessed from the IP address -- nothing leaves the
    machine unless you have set it.
  - The service: Open-Meteo (free, no account, no key), asked once every
    half hour by the host, not the guest.
  - Offline, or no place set: weather made up from the season and the date,
    so the farm still has rain and snow.
  - With 30-minute days and real weather, one real rainy afternoon is several
    rainy farm days. Probably right; worth seeing.
  - The flood: "very heavy" needs a number (say 20 mm in an hour), and a
    colony could survive if it has dug a chamber higher than the water. That
    makes digging matter.
- **4 -- lifetimes**: the queen lays to replace them; a colony with no queen
  dwindles. Dead ants carried to a midden heap on the surface.
- **5 -- the anteater**: its tongue down the tunnels, the survivors the ones
  deepest in. A colony that digs deep does better.
- **6 -- the breakout** is the one that needs care. A sidebar today never
  touches the machine, and a program's screen being eaten could lose
  someone's work. My suggestion: the ants eat **a picture of** the screen,
  drawn over it by the emulator, not the guest's own memory. It looks the
  same; a key press, or the ants being chased back, and the screen is
  whole again. And it only happens at the shell prompt or while the machine
  is idle, never in the middle of VI.
- **7 -- the rocket**: fits the sidebar plan well -- a sidebar able to hand
  over to another (`next = space`). And the colony's state stays, so the
  ant farm can come back with "the colony that went to space" in its
  history.

## Some of mine

- **A chronicle**: `ANTFARM/HISTORY.TXT`, one line an event -- "Day 12:
  heavy rain, 3 lost", "Day 19: the anteater. 41 of 60 lost", "Day 23: a
  new queen". Readable with TYPE. The best part of a long-lived colony is
  its story.
- **Seasons from the real date**: longer summer days, shorter winter ones;
  a nuptial flight in summer where winged ants leave, and one founds a new
  colony **in the other sidebar**.
- **Food from you**: every so often, what you type drops crumbs on the
  surface. A colony on a busy machine eats well; one left alone forages.
- **A rival colony** that can dig into the second entrance: a war, and
  either colony can win.
- **Visitors that do nothing bad**: a beetle, a worm through the soil,
  birds at dawn.
- **Adjustable fate**: the chances (anteater, breakout, rocket) all in
  `OPTIONS.CFG`, with `fate = off` for a farm where nothing terrible ever
  happens.

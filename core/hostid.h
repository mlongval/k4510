/* Which host is this?
 *
 * The CPU clock is measured rather than guessed, but not at boot: the boot is
 * instantaneous, always (Doc, 2026-08-27), and the measuring is done from
 * inside the machine by SETUP.prg, which sweeps the ladder with sound and
 * video and the network really running and writes its answer back through
 * SYS+$28.  What the frontend still needs is a way to know whether the clock
 * in k4510.cfg was measured on THIS machine, because a card or a config file
 * moves between hosts and a number measured on one says nothing about another.
 *
 * That is all this file is.  The two-phase boot-time probe that used to live
 * beside it (core/calib.c, the ms_fixed + ms_per_mhz line of
 * docs/CPU-CLOCK-POLICY.md) was cut on 2026-09-01: it had been compiled but
 * unreachable since the boot was made instantaneous, and a measuring engine
 * that never measures is worse than none -- it reads as a capability the
 * machine has.  The design record is in docs/CPU-CLOCK-POLICY.md and the
 * BUILD-LOG's three-host sweep; the git history has the code.
 */
#ifndef K4510_HOSTID_H
#define K4510_HOSTID_H
#ifdef __cplusplus
extern "C" {
#endif
/* A number that changes when the host does -- CPU model and count on the
 * desktop, the board on the Pi -- so a cached measurement is trusted only on
 * the machine it was taken on.  Never 0, which means "never measured". */
int host_id_hash(void);
#ifdef __cplusplus
}
#endif
#endif

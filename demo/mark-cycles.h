/* Written by tools/mark-cycles.py -- do not edit.  What the measured loops of
 * demo/mark-asm.s cost a 65C02, counted on py65 from the built mark.prg. */
#define MK_CYC_SPIN   16435562UL   /* mk_spin(50) */
#define MK_CYC_SIEVE  15693505UL
#define MK_CYC_COPY   32895757UL   /* mk_copy(250) */
#define MK_CYC_MANDEL 158005728UL
/* mark-asm.s sha256 1c2bc4d23444b0305b6beaaaae0b759094108b9ee569c079cb379196adc357b3 */

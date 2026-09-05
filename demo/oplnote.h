/* A test tone on the OPL2, for the programs that need something sounding
 * while they measure (BENCH, SETUP): one voice, a plain two-operator tone.
 * voice 0-8; note as an F-number and block, A-440 by default. */
#ifndef K4510_OPLNOTE_H
#define K4510_OPLNOTE_H
#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
static void opl_reg(uint8_t r, uint8_t v) { REG(OPL_ADDR) = r; REG(OPL_DATA) = v; }
static void opl_note_on(uint8_t voice, uint16_t fnum, uint8_t block)
{
    static const uint8_t slot[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
    uint8_t m = slot[voice], c = (uint8_t)(m + 3);
    opl_reg(0x01, 0x20); opl_reg(0xBD, 0x00);
    opl_reg((uint8_t)(0x20 + m), 0x21); opl_reg((uint8_t)(0x40 + m), 0x18);
    opl_reg((uint8_t)(0x60 + m), 0xF0); opl_reg((uint8_t)(0x80 + m), 0x0F); opl_reg((uint8_t)(0xE0 + m), 0x01);
    opl_reg((uint8_t)(0x20 + c), 0x21); opl_reg((uint8_t)(0x40 + c), 0x08);
    opl_reg((uint8_t)(0x60 + c), 0xF0); opl_reg((uint8_t)(0x80 + c), 0x0F); opl_reg((uint8_t)(0xE0 + c), 0x00);
    opl_reg((uint8_t)(0xC0 + voice), 0x00);
    opl_reg((uint8_t)(0xB0 + voice), 0);
    opl_reg((uint8_t)(0xA0 + voice), (uint8_t)(fnum & 0xFF));
    opl_reg((uint8_t)(0xB0 + voice), (uint8_t)(0x20 | (block << 2) | ((fnum >> 8) & 3)));
}
static void opl_note_off(uint8_t voice) { opl_reg((uint8_t)(0xB0 + voice), 0); }
#define OPL_A440_FNUM 580   /* A-440 in block 4 */
#define OPL_A440_BLOCK 4
#endif

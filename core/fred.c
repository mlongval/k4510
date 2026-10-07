/* ---- FRED, the MATH unit ($D700) ---------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "state.h"
static uint8_t math_reg[0x80];       /* $D700-$D77F image; F0..F7 at 0, FI at $24, integer unit at $68.. */
static float  mf_get(int n) { float f; memcpy(&f, &math_reg[n * 4], 4); return f; }
static void   mf_set(int n, float f) { memcpy(&math_reg[n * 4], &f, 4); }
static uint32_t m32(int off) { return (uint32_t)math_reg[off] | ((uint32_t)math_reg[off + 1] << 8) | ((uint32_t)math_reg[off + 2] << 16) | ((uint32_t)math_reg[off + 3] << 24); }
static void m32w(int off, uint32_t v) { for (int i = 0; i < 4; i++) math_reg[off + i] = (uint8_t)(v >> (8 * i)); }
static void math_int_update(void)
{
    uint32_t a = m32(0x70), b = m32(0x74);
    uint64_t p = (uint64_t)a * b;
    m32w(0x78, (uint32_t)p); m32w(0x7C, (uint32_t)(p >> 32));
    if (b == 0) { m32w(0x6C, 0xFFFFFFFFu); m32w(0x68, 0xFFFFFFFFu); }
    else { uint64_t q = ((uint64_t)a << 32) / b; m32w(0x6C, (uint32_t)(q >> 32)); m32w(0x68, (uint32_t)q); }
}
/* MS-BASIC float-to-ASCII, EhBASIC-style: 6 significant digits, fixed
 * format for 0.01 <= |v| < 1e6, otherwise d.dddddE+xx; ".5" fractions,
 * trailing zeros stripped -- measured against Lee's own FOUT output. */
static void ms_ftoa(float vf, char *out, int lead)
{
    char *p = out, digits[12]; double v = vf; int e10 = 0, dp, i, n;
    if (lead) *p++ = vf < 0 ? '-' : ' ';
    else if (vf < 0) *p++ = '-';
    if (v < 0) v = -v;
    if (v == 0) { *p++ = '0'; *p = 0; return; }
    if (isnan(v)) { strcpy(p, "NAN"); return; }
    if (isinf(v)) { strcpy(p, "1E+38"); return; }         /* MS BASIC has no inf: overflow prints as its largest */
    while (v >= 999999.5) { v /= 10; e10++; }
    while (v < 99999.95)  { v *= 10; e10--; }
    snprintf(digits, sizeof digits, "%06lu", (unsigned long)(v + 0.5));
    if (digits[6]) { digits[6] = 0; e10++; }              /* 999999.5+ rounded up a digit */
    dp = 6 + e10;                                         /* value = 0.digits * 10^dp */
    n = 6; while (n > 1 && digits[n - 1] == '0') n--;     /* strip trailing zeros */
    if (dp >= -1 && dp <= 6) {                            /* fixed: 0.01 <= v < 1e6 */
        if (dp <= 0) { *p++ = '.'; for (i = 0; i < -dp; i++) *p++ = '0'; for (i = 0; i < n; i++) *p++ = digits[i]; }
        else {
            for (i = 0; i < dp; i++) *p++ = i < n ? digits[i] : '0';
            if (n > dp) { *p++ = '.'; for (i = dp; i < n; i++) *p++ = digits[i]; }
        }
    } else {                                                /* E format */
        *p++ = digits[0];
        if (n > 1) { *p++ = '.'; for (i = 1; i < n; i++) *p++ = digits[i]; }
        *p++ = 'E'; *p++ = dp - 1 < 0 ? '-' : '+';
        i = dp - 1 < 0 ? 1 - dp : dp - 1;
        *p++ = (char)('0' + i / 10); *p++ = (char)('0' + i % 10);
    }
    *p = 0;
}
static void math_fop(uint8_t op)
{
    int d = (math_reg[0x21] >> 4) & 7, sidx = math_reg[0x21] & 7;
    float a = mf_get(d), b = mf_get(sidx), r = a; int store = 1;
    switch (op & 0x1F) {
    case MATH_MOV: r = b; break;       case MATH_ADD: r = a + b; break;   case MATH_SUB: r = a - b; break;
    case MATH_MUL: r = a * b; break;   case MATH_DIV: r = a / b; break;
    case MATH_SQRT: r = sqrtf(b); break; case MATH_SIN: r = sinf(b); break; case MATH_COS: r = cosf(b); break;
    case MATH_TAN: r = tanf(b); break; case MATH_ATAN: r = atanf(b); break; case MATH_ATAN2: r = atan2f(a, b); break;
    case MATH_EXP: r = expf(b); break; case MATH_LOG: r = logf(b); break;  case MATH_POW: r = powf(a, b); break;
    case MATH_ABS: r = fabsf(b); break; case MATH_NEG: r = -b; break;     case MATH_FLOOR: r = floorf(b); break;
    case MATH_ROUND: r = roundf(b); break; case MATH_FMOD: r = fmodf(a, b); break;
    case MATH_CMP: r = a - b; store = 0; break;
    case MATH_ITOF: r = (float)(int32_t)m32(0x24); break;
    case MATH_FTOI: { float t = truncf(b); int32_t i = (t > 2147483520.0f) ? INT32_MAX : (t < -2147483648.0f) ? INT32_MIN : (int32_t)t; m32w(0x24, (uint32_t)i); r = b; store = 0; break; }
    case MATH_FTOA: case MATH_FTOAR: {                    /* number output on the MATH unit: the exact */
        char buf[24]; uint32_t p = m32(0x30); int i;       /* MS-BASIC 9-digit format EhBASIC always used */
        ms_ftoa(b, buf, op == MATH_FTOA);
        for (i = 0; buf[i]; i++) k4510_ram[(p + i) & K4510_PHYS_MASK] = (uint8_t)buf[i];
        k4510_ram[(p + i) & K4510_PHYS_MASK] = 0;
        store = 0; break; }
    default: store = 0; break;
    }
    if (store) mf_set(d, r);
    math_reg[0x22] = (uint8_t)((r == 0.0f ? 1 : 0) | (r < 0.0f ? 2 : 0) | (isnan(r) || isinf(r) ? 4 : 0));
}
static void math_list_run(void)
{
    uint32_t pc = m32(0x28) & K4510_PHYS_MASK;
    uint16_t cnt = (uint16_t)(math_reg[0x2E] | (math_reg[0x2F] << 8));
    uint8_t status = 0xFF;
    for (int guard = 0; guard < 65536; guard++) {
        uint8_t op = k4510_ram[pc & K4510_PHYS_MASK], arg = k4510_ram[(pc + 1) & K4510_PHYS_MASK];
        pc += 2;
        if (op < 0x20) { math_reg[0x21] = arg; math_fop(op); continue; }
        int fl = math_reg[0x22];
        switch (op) {
        case ML_END:     status = 0; goto done;
        case ML_STOPNEG: if (fl & 2)   { status = 1; goto done; } break;
        case ML_STOPPOS: if (!(fl & 2)) { status = 1; goto done; } break;
        case ML_STOPZERO: if (fl & 1)  { status = 1; goto done; } break;
        case ML_STOPNZ:  if (!(fl & 1)) { status = 1; goto done; } break;
        case ML_JUMP:    pc += (int8_t)arg * 2; break;
        case ML_DJNZ:    if (--cnt) pc += (int8_t)arg * 2; break;
        case ML_STOPFIGE: { int32_t fi = (int32_t)m32(0x24); if (fi >= (int32_t)arg) { status = 1; goto done; } break; }
        case ML_LDF:     for (int i = 0; i < 4; i++) math_reg[((arg >> 4) & 7) * 4 + i] = k4510_ram[(pc + i) & K4510_PHYS_MASK]; pc += 4; break;
        case ML_LDI:     for (int i = 0; i < 4; i++) math_reg[0x24 + i] = k4510_ram[(pc + i) & K4510_PHYS_MASK]; pc += 4; break;
        case ML_LDMS: {  uint32_t a = ((uint32_t)k4510_ram[pc & K4510_PHYS_MASK] | ((uint32_t)k4510_ram[(pc + 1) & K4510_PHYS_MASK] << 8)
                         | ((uint32_t)k4510_ram[(pc + 2) & K4510_PHYS_MASK] << 16) | ((uint32_t)k4510_ram[(pc + 3) & K4510_PHYS_MASK] << 24)) & K4510_PHYS_MASK;
                         uint8_t e = k4510_ram[a], m1 = k4510_ram[(a + 1) & K4510_PHYS_MASK], m2 = k4510_ram[(a + 2) & K4510_PHYS_MASK], m3 = k4510_ram[(a + 3) & K4510_PHYS_MASK];
                         float f = 0.0f;
                         if (e >= 2) { uint32_t bits = ((uint32_t)(m1 & 0x80) << 24) | ((uint32_t)(e - 2) << 23) | ((uint32_t)(m1 & 0x7F) << 16) | ((uint32_t)m2 << 8) | m3; memcpy(&f, &bits, 4); }
                         mf_set((arg >> 4) & 7, f); pc += 4; break; }
        default:         status = 0xFF; goto done;
        }
    }
done:
    math_reg[0x2D] = status; math_reg[0x2E] = (uint8_t)cnt; math_reg[0x2F] = (uint8_t)(cnt >> 8);
}
uint8_t fred_read(uint8_t r) { return r < sizeof math_reg ? math_reg[r] : 0xFF; }
void fred_write(uint8_t r, uint8_t v)
{
    if (r >= sizeof math_reg) return;
    if (r == 0x20) { math_fop(v); return; }
    if (r == 0x2C) { math_list_run(); return; }
    math_reg[r] = v;
    if (r >= 0x70 && r < 0x78) math_int_update();
}
void fred_reset(void) { memset(math_reg, 0, sizeof math_reg); math_int_update(); }
void fred_state_save(FILE *f) { state_put(f, "MATH", math_reg, sizeof math_reg); }
int  fred_state_load(FILE *f)
{
    if (state_get(f, "MATH", math_reg, sizeof math_reg)) return -2;
    math_int_update();
    return 0;
}
void fred_dump(FILE *f)
{
    fprintf(f, "MATH F0..F7:"); for (int i = 0; i < 8; i++) fprintf(f, " %g", mf_get(i)); fprintf(f, "  FI=%d flags=%02X mlstat=%02X\n", (int)m32(0x24), math_reg[0x22], math_reg[0x2D]);
}

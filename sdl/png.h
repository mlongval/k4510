#ifndef K4510_PNG_H
#define K4510_PNG_H
#include <stdint.h>
/* RAW is H rows of 1 + W*3 bytes, each led by its filter byte (0), RGB;
 * written to PATH through PATH.tmp, renamed when whole. */
void png_write(const char *path, const uint8_t *raw, int W, int H);
#endif

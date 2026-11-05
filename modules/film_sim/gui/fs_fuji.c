/*
 * Format a Fuji recipe as a .fs the engine already knows how to read.
 *
 * Every mapping here has a measured precedent in this module, so nothing new
 * is being invented at save time:
 *
 *   highlight/shadow -> CURVE        the same five points the creator's tone
 *                                    screen writes, and the same arithmetic
 *   color            -> SAT          -4..+4 -> 50..150 percent
 *   chrome           -> COLOR_CHROME 0/1/2, parsed by sc_level()
 *   fx_blue          -> FX_BLUE      ditto
 *   grain            -> GRAIN        weak 8 / strong 16 on the 0..100 scale
 *                     -> GRAIN_SIZE  small 6 / large 20
 *
 * WB is deliberately absent: it is a camera property, applied through the
 * stock-offset path, not part of a look file.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fs_fuji.h"

void fs_fuji_defaults(fs_fuji_t *f)
{
    memset(f, 0, sizeof *f);
    memcpy(f->name, "RECIPE", 7);
    f->grain = 1;          /* Fuji's own defaults are Weak grain, Small */
    f->grain_size = 0;
}

/* out level for an in level, in Fuji's sense: + highlight compresses the top,
 * + shadow deepens the bottom. Scale chosen so the extremes (+-4) move an end
 * by about 30 levels -- visible, not destructive. */
static int tone_out(const fs_fuji_t *f, int in)
{
    int t = in * 100 / 255;
    int out = in;
    out -= (f->highlight * t * t) / 1000;                /* top end     */
    out -= (f->shadow * (100 - t) * (100 - t)) / 1000;   /* bottom end  */
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return out;
}

static int append(char *buf, int cap, int n, const char *fmt, ...)
{
    va_list ap;
    int wrote;
    if (n < 0 || n >= cap) return -1;
    va_start(ap, fmt);
    wrote = vsnprintf(buf + n, (size_t)(cap - n), fmt, ap);
    va_end(ap);
    if (wrote < 0 || n + wrote >= cap) return -1;
    return n + wrote;
}

static const char *level_word(int v)
{
    return v == 2 ? "STRONG" : (v == 1 ? "WEAK" : "OFF");
}

int fs_fuji_write(const fs_fuji_t *f, char *buf, int cap)
{
    static const int curve_in[5] = { 0, 64, 128, 192, 255 };
    int n = 0, i;

#define PUT(...) do { n = append(buf, cap, n, __VA_ARGS__); if (n < 0) return -1; } while (0)

    PUT("VERSION   3\n");
    PUT("NAME      %s\n", f->name[0] ? f->name : "RECIPE");
    PUT("DESC      FUJI RECIPE (TONE CURVE, COLOR, CHROME, GRAIN)\n");
    PUT("WB        AS_SHOT\n");

    n = append(buf, cap, n, "CURVE     ");
    if (n < 0) return -1;
    for (i = 0; i < 5; i++)
        PUT("%s%d,%d", i ? " " : "", curve_in[i], tone_out(f, curve_in[i]));
    PUT("\n");
    PUT("CURVE_R   0,0 128,128 255,255\n");
    PUT("CURVE_G   0,0 128,128 255,255\n");
    PUT("CURVE_B   0,0 128,128 255,255\n");

    PUT("SAT       %d\n", 100 + f->color * 12);
    PUT("COLOR_CHROME %s\n", level_word(f->chrome));
    PUT("FX_BLUE   %s\n", level_word(f->fx_blue));
    PUT("GRAIN     %d\n", f->grain == 2 ? 16 : (f->grain == 1 ? 8 : 0));
    PUT("GRAIN_SIZE %d\n", f->grain_size ? 20 : 6);

#undef PUT
    return n;
}

/**
 * The shot-time look: the same idea as Florin's filmyuv_apply, on our recipes.
 *
 * What his code does
 * ------------------
 * Disassembling work_000 (0xa0f8) -- one of the eight per-pixel workers
 * filmyuv_apply dispatches to -- the whole loop is three table lookups and
 * about ten multiplies per UYVY pair:
 *
 *      lr  = *src++;                            U | Y0<<8 | V<<16 | Y1<<24
 *      r3  = (lr >> 8) & 0xff;                  Y0
 *      r7  = ctx[lr >> 24];                     Y1 through a 256-byte table
 *      r8  = ctx[r3];                            Y0 through the same table
 *      idx = ((V^80)>>3)*32 + ((U^80)>>3);      5 bits of each, 32x32
 *      cg  = ctx[0x500 + idx];                  chroma gain
 *      ip  = *(uint32_t*)(ctx + 0x100 + ((r7+r8)>>1)*4);
 *           byte0 = gain (Q6), byte1 = U offset, byte2 = V offset
 *      U' = (byte of ip, signed) + (cg * (U signed) * gain) >> 6   clamp +-127
 *      V' = (byte of ip, signed) + (cg * (V signed) * gain) >> 6   clamp +-127
 *
 * So his film look is a SEPARABLE model: a luma curve, a chroma gain and
 * offset per luminance, and a chroma gain per (U,V). It is not a 3D LUT over
 * RGB at shot time -- the .CUB is baked down to exactly those three tables
 * beforehand. That is why it costs ~1 s on 22 Mpx instead of a minute.
 *
 * It is also, very neatly, the shape of our own recipe: CURVE is the luma
 * table, SPLIT_SH/SPLIT_HI are chroma offsets weighted by luma, SAT is the
 * gain, and HUE_SAT per sector is the (U,V) table. So the same three tables
 * can be baked from a .fs file and applied to the frame Canon is about to
 * encode -- one JPEG encode, no decode, no second generation.
 *
 * What is lost
 * ------------
 * Grain and halation. Grain needs a random field per pixel and halation needs
 * a neighbourhood; neither fits a separable lookup, and Florin dropped both
 * from this path for the same reason. They stay in the develop path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifndef SE_HOSTED
#include <dryos.h>
#include <mem.h>
#include <fio-ml.h>
#endif

#define SE_YUV_STOCK_MAX (16 * 1024)

#include "fs_yuvlook.h"
#include "se/engine/look.h"
#include "se/engine/stock.h"

/* The tables themselves, baked in look.c by running the real grade. */
static se_yuvtbl tbl;
/* The same look as a joint (y,u,v) lattice, for the LiveView filter. */
static se_yuv3   tbl3;
static int       baked;
static char      baked_name[SE_STOCK_NAME];

int fs_yuvlook_bake(const char *stock_path, int strength)
{
    se_stock st;
    se_look  lk;
    char    *text;
    size_t   got = 0;
    FILE    *f;

    baked = 0;
#ifdef SE_HOSTED
    f = fopen(stock_path, "rb");
#else
    f = FIO_OpenFile(stock_path, O_RDONLY | O_SYNC);
#endif
    if (!f)
        return -1;
    text = malloc(SE_YUV_STOCK_MAX);
    if (!text) {
#ifdef SE_HOSTED
        fclose(f);
#else
        FIO_CloseFile(f);
#endif
        return -1;
    }
#ifdef SE_HOSTED
    got = fread(text, 1, SE_YUV_STOCK_MAX, f);
    fclose(f);
#else
    /* 8 KB at a time: FIO_ReadFile() asserts on more than that when the
     * destination is ordinary cacheable memory (src/fio-ml.c). */
    while (got + 8192 <= SE_YUV_STOCK_MAX) {
        int n = FIO_ReadFile(f, text + got, 8192);
        if (n <= 0) break;
        got += (size_t)n;
    }
    FIO_CloseFile(f);
#endif
    if (!got || se_stock_parse(text, got, &st) != SE_OK) {
        free(text);
        return -1;
    }
    free(text);

    if (!st.name[0]) {
        /* An empty NAME means the parse found nothing it recognised -- which
         * is what happened when this ran against an empty stock path and
         * silently produced identity tables. Refuse instead. */
        return -1;
    }

    se_look_from_stock(&st, 1, &lk);
    se_look_set_strength(&lk, strength);
    se_look_bake_yuv(&lk, &tbl);
    /* The LiveView filter shows the same look through the joint lattice, so
     * the panel and the file cannot disagree about which stock is loaded. */
    se_look_bake_yuv3(&lk, &tbl3);

    snprintf(baked_name, sizeof baked_name, "%s", st.name);
    baked = 1;
    return 0;
}

int fs_yuvlook_ready(void) { return baked; }
const char *fs_yuvlook_name(void) { return baked ? baked_name : ""; }

/* The baked tables, for host tools that plot or compare them. The camera path
 * never needs this -- it only ever calls fs_yuvlook_apply(). */
const se_yuvtbl *fs_yuvlook_table(void) { return baked ? &tbl : NULL; }

/* The same look as a joint (y,u,v) lattice. NULL until a bake succeeds. */
const se_yuv3 *fs_yuvlook_uv3(void) { return baked ? &tbl3 : NULL; }

/*
 * Apply to a UYVY frame in place.
 *
 * Three table lookups and two multiplies per pixel, no floating point, no
 * allocation: this runs inside the shot, before Canon's encoder reads the
 * buffer. `stride` is in bytes and must be at least width*2.
 */
uint32_t fs_yuvlook_apply(unsigned char *buf, int width, int height, int stride)
{
#ifndef SE_HOSTED
    uint32_t t0 = get_ms_clock();
#endif
    int y;

    if (!baked || !buf || width < 2 || height < 1 || stride < width * 2)
        return 0;

    for (y = 0; y < height; y++) {
        unsigned char *p = buf + (size_t)y * (size_t)stride;
        int x;
        for (x = 0; x + 1 < width; x += 2, p += 4) {
            /*
             * Chroma is a SIGNED byte on this body: 0 is neutral, not 128.
             * Proven by dumping Canon's own LiveView plane (LV_RAW.BIN):
             * decoded as signed the frame is correct, decoded as (byte-128)
             * it is a mess of magenta and cyan. fs_core_process_rows, the
             * LiveView path, has always used (signed char) -- this function
             * was the odd one out.
             */
            int u   = (int)(signed char)p[0];
            int v   = (int)(signed char)p[2];
            int y0  = p[1];
            int y1  = p[3];
            int yav = (y0 + y1) >> 1;
            int cell = ((v + 128) >> 3) * 32 + ((u + 128) >> 3);
            int g    = tbl.gain[yav];
            int cg   = tbl.cg[cell];
            int dy   = tbl.dy[cell];
            int cgg  = cg * g;
            int ru   = (tbl.matrix[0] * u + tbl.matrix[1] * v) >> 8;
            int rv   = (tbl.matrix[2] * u + tbl.matrix[3] * v) >> 8;
            int nu   = tbl.uoff[yav] + ((ru * cgg) >> 12);
            int nv   = tbl.voff[yav] + ((rv * cgg) >> 12);
            int ny0  = tbl.y[y0] + dy;
            int ny1  = tbl.y[y1] + dy;

            p[0] = (unsigned char)(signed char)(nu < -128 ? -128 : nu > 127 ? 127 : nu);
            p[2] = (unsigned char)(signed char)(nv < -128 ? -128 : nv > 127 ? 127 : nv);
            p[1] = (unsigned char)(ny0 < 0 ? 0 : ny0 > 255 ? 255 : ny0);
            p[3] = (unsigned char)(ny1 < 0 ? 0 : ny1 > 255 ? 255 : ny1);
        }
    }
#ifdef SE_HOSTED
    return 0;
#else
    return get_ms_clock() - t0;
#endif
}

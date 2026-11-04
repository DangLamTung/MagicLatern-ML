/*
 * lv3d - the LiveView look as a joint (y,u,v) lattice, against the forms it
 * could replace, on the panel's own geometry.
 *
 * The LiveView directory grades a 1440-byte UYVY row, 480 of them. This reads
 * a 720x480 P6 PPM (two pixels per UYVY word), builds that plane, bakes every
 * form from ONE .fs recipe, and reports two things per form:
 *
 *   accuracy  per-channel error against se_look_rows -- the develop
 *   cost      ms/frame on this machine, best of several passes
 *
 *   ./lv3d input.ppm stock.fs
 *
 * Nothing here is measured on the camera. The cost RATIO is what carries over;
 * the absolute milliseconds are this Mac's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../engine/se.h"
#include "../engine/look.h"
#include "../engine/stock.h"
#include "../../fs_core.h"
#include "../../fs_yuvlook.h"

/* The module header and the engine must agree on the lattice, or the filter
 * would index a buffer the bake never filled. */
typedef char fs_lv3d_n_check[(SE_YUV3_NY == FS_UV3_NY && SE_YUV3_NC == FS_UV3_NC) ? 1 : -1];

#define PITCH 1440                 /* bytes a row, UYVY              */
#define PIX   (PITCH / 2)          /* pixels a row                   */
#define H     480
#define RGBP  (PIX * 3)            /* bytes of RGB a row             */

static unsigned char plane[PITCH * H];
static unsigned char in_rgb[RGBP * H];
static unsigned char truth[RGBP * H];

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void rgb_to_ycc(const unsigned char *p, int *Y, int *U, int *V)
{
    *Y = (77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8;
    *U = (-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8;
    *V = (128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8;
}

/* UYVY -> interleaved RGB, `npix` pixels. The same conversion tableerr and
 * lvgrade use, so the numbers stay comparable between tools. */
static void unpack(const unsigned char *s, unsigned char *o, int npix)
{
    for (int x = 0; x + 1 < npix; x += 2) {
        int u = (signed char)s[0], y0 = s[1], v = (signed char)s[2], y1 = s[3];
        o[0] = clamp8(y0 + ((359 * v) >> 8));
        o[1] = clamp8(y0 - ((88 * u + 183 * v) >> 8));
        o[2] = clamp8(y0 + ((454 * u) >> 8));
        o[3] = clamp8(y1 + ((359 * v) >> 8));
        o[4] = clamp8(y1 - ((88 * u + 183 * v) >> 8));
        o[5] = clamp8(y1 + ((454 * u) >> 8));
        s += 4;
        o += 6;
    }
}

static int read_ppm(const char *path, unsigned char *rgb, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    char magic[3] = {0};
    int maxv;

    if (!f) return -1;
    if (fscanf(f, "%2s %d %d %d", magic, w, h, &maxv) != 4 ||
        strcmp(magic, "P6") || maxv != 255 || fgetc(f) != '\n') {
        fclose(f);
        return -1;
    }
    if (fread(rgb, 1, (size_t)(*w) * (*h) * 3, f) != (size_t)(*w) * (*h) * 3) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* A sink that ADVANCES: se_look_rows() emits one call per row, and a sink
 * that always writes to the same address would leave only the last row in the
 * buffer (which is exactly what made the first run of this tool compare three
 * forms against a nearly empty truth). */
static int sink_row(void *ctx, const unsigned char *row, int width)
{
    unsigned char **p = ctx;
    memcpy(*p, row, (size_t)width * 3);
    *p += (size_t)width * 3;
    return 0;
}

/* The develop: the same pixels through se_look_rows. */
static int develop(se_look *lk, unsigned char *in, unsigned char *out,
                   int npix, int nrows)
{
    size_t need = se_look_bytes(lk, npix);
    void *mem = malloc(need + 64);
    se_arena ar;
    se_look_run *run;
    unsigned char *dst = out;
    int rc = -1;

    if (!mem) return -1;
    se_arena_init(&ar, mem, need + 64);
    if (se_look_begin(&run, lk, npix, &ar) != SE_OK) goto done;
    if (se_look_rows(run, in, nrows, sink_row, &dst) != SE_OK) goto done;
    se_look_end(run, sink_row, &dst);
    rc = 0;
done:
    free(mem);
    return rc;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

typedef struct { long n; double sum; int max; double sy, scb, scr, smag; } stat;

static void stat_add(stat *s, int d) { s->n++; s->sum += d; if (d > s->max) s->max = d; }

static void stat_show(const char *nm, const stat *s)
{
    if (!s->n) { printf("  %-38s (no samples)\n", nm); return; }
    printf("  %-38s mean %5.2f  max %3d   [dY %4.2f dCb %4.2f dCr %4.2f |C| %4.1f]\n",
           nm, s->sum / (double)s->n, s->max,
           s->sy / (double)s->n, s->scb / (double)s->n, s->scr / (double)s->n,
           s->smag / (double)s->n);
}

/* Score a graded plane against the develop, per channel. */
static void score(const unsigned char *graded, stat *s)
{
    static unsigned char got[RGBP * H];
    int y, x, i;

    for (y = 0; y < H; y++)
        unpack(graded + (size_t)y * PITCH, got + (size_t)y * RGBP, PIX);
    for (y = 0; y < H; y++)
        for (x = 0; x < RGBP; x += 3)
            for (i = 0; i < 3; i++) {
                int a = got[(size_t)y * RGBP + x + i];
                int b = truth[(size_t)y * RGBP + x + i];
                stat_add(s, a > b ? a - b : b - a);
            }

    /* where the error lives: luma, Cb, Cr -- and how much chroma there was */
    for (y = 0; y < H; y++)
        for (x = 0; x < RGBP; x += 3) {
            const unsigned char *g = got + (size_t)y * RGBP + x;
            const unsigned char *t = truth + (size_t)y * RGBP + x;
            int Yg, Ug, Vg, Yt, Ut, Vt;
            rgb_to_ycc(g, &Yg, &Ug, &Vg);
            rgb_to_ycc(t, &Yt, &Ut, &Vt);
            s->sy  += Yg > Yt ? Yg - Yt : Yt - Yg;
            s->scb += Ug > Ut ? Ug - Ut : Ut - Ug;
            s->scr += Vg > Vt ? Vg - Vt : Vt - Vg;
            s->smag += (Ut < 0 ? -Ut : Ut) + (Vt < 0 ? -Vt : Vt);
        }
}

static int load_stock(const char *path, se_stock *st)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *text;
    size_t got;
    int rc;

    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024) { fclose(f); return -1; }
    text = malloc((size_t)sz);
    if (!text) { fclose(f); return -1; }
    got = fread(text, 1, (size_t)sz, f);
    fclose(f);
    rc = se_stock_parse(text, got, st);
    free(text);
    return rc == SE_OK && st->name[0] ? 0 : -1;
}

#define PASSES 40

int main(int argc, char **argv)
{
    static unsigned char rgb[PIX * H * 3];
    static unsigned char work[PITCH * H];
    static fs_lut_t lut_preset;
    int w = 0, h = 0, x, y, pass;
    se_stock st;
    se_look lk;
    se_yuv3 t3;
    stat s_lat = {0}, s_shot = {0}, s_preset = {0};
    double t_lat = 1e9, t_preset = 1e9, t_shot = 1e9;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <720x480.ppm> <stock.fs>\n", argv[0]);
        return 2;
    }
    if (read_ppm(argv[1], rgb, &w, &h) != 0) {
        fprintf(stderr, "read %s failed\n", argv[1]);
        return 1;
    }
    if (w != PIX || h != H) {
        fprintf(stderr, "%s is %dx%d, expected %dx%d\n", argv[1], w, h, PIX, H);
        return 1;
    }
    if (load_stock(argv[2], &st) != 0) {
        fprintf(stderr, "parse %s failed\n", argv[2]);
        return 1;
    }

    /* the UYVY plane this picture would occupy in VRAM */
    for (y = 0; y < H; y++)
        for (x = 0; x < PIX; x += 2) {
            unsigned char *q = plane + (size_t)y * PITCH + (size_t)x * 2;
            int Y0, U0, V0, Y1, U1, V1;
            rgb_to_ycc(rgb + ((size_t)y * PIX + x) * 3, &Y0, &U0, &V0);
            rgb_to_ycc(rgb + ((size_t)y * PIX + x + 1) * 3, &Y1, &U1, &V1);
            (void)U1; (void)V1;              /* 4:2:2 keeps the left chroma */
            q[0] = (unsigned char)(signed char)U0;
            q[1] = (unsigned char)Y0;
            q[2] = (unsigned char)(signed char)V0;
            q[3] = (unsigned char)Y1;
        }

    /* The look, and the lattice form of it. */
    se_look_from_stock(&st, 1, &lk);
    se_look_set_strength(&lk, 100);
    lk.grain = 0;
    lk.hal_amount = 0;
    se_look_bake_yuv3(&lk, &t3);

    /* The develop: unpack the plane row by row, then grade it whole. */
    for (y = 0; y < H; y++)
        unpack(plane + (size_t)y * PITCH, in_rgb + (size_t)y * RGBP, PIX);
    if (develop(&lk, in_rgb, truth, PIX, H) != 0) {
        fprintf(stderr, "develop failed\n");
        return 1;
    }

    /* --- the joint lattice ------------------------------------------- */
    for (pass = 0; pass < PASSES; pass++) {
        unsigned int rnd = 12345u;
        double t0, dt;
        memcpy(work, plane, sizeof work);
        t0 = now_ms();
        fs_core_process_rows3(t3.y, t3.v, work, work, PITCH, 0, H, 0, &rnd);
        dt = now_ms() - t0;
        if (dt < t_lat) t_lat = dt;
    }
    score(work, &s_lat);

    /* --- the shipping separable shot tables -------------------------- */
    if (fs_yuvlook_bake(argv[2], 100) == 0) {
        double best = 1e9;
        for (pass = 0; pass < PASSES; pass++) {
            double t;
            memcpy(work, plane, sizeof work);
            t = (double)fs_yuvlook_apply(work, PIX, H, PITCH);
            if (t > 0 && t < best) best = t;
        }
        t_shot = best;
        score(work, &s_shot);
    }

    /* --- the LiveView preset form, which is what runs today ----------- */
    fs_core_build_luts(&lut_preset, &fs_presets[FS_PRESET_FUJI_SUPERIA], 100, 0);
    for (pass = 0; pass < PASSES; pass++) {
        unsigned int rnd = 12345u;
        double t0, dt;
        memcpy(work, plane, sizeof work);
        t0 = now_ms();
        fs_core_process_rows(&lut_preset, work, work, PITCH, 0, H, &rnd);
        dt = now_ms() - t0;
        if (dt < t_preset) t_preset = dt;
    }
    score(work, &s_preset);

    printf("%dx%d panel, %s + %s\n", PIX, H, argv[1], st.name);
    printf("\nerror against se_look_rows on the same pixels (levels, per channel)\n");
    {
        char nm[64];
        snprintf(nm, sizeof nm, "joint (y,u,v) lattice, %d B",
                 256 + SE_YUV3_NODES * 3 * 2);
        stat_show(nm, &s_lat);
    }
    stat_show("separable shot tables, 3084 B", &s_shot);
    stat_show("LV preset form, a DIFFERENT look", &s_preset);
    printf("\ncost on this Mac, best of %d passes\n", PASSES);
    printf("  %-30s %7.2f ms/frame\n", "joint (y,u,v) lattice", t_lat);
    printf("  %-30s %7.2f ms/frame\n", "LV preset tables", t_preset);
    printf("  lattice / preset: %.2fx\n", t_lat / t_preset);
    if (t_shot < 1e9)
        printf("  %-30s %7.2f ms/frame\n", "separable shot tables", t_shot);
    return 0;
}

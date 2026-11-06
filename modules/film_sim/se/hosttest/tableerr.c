/*
 * tableerr - what does the shot-time table form lose against the full look?
 *
 * The shot path grades a UYVY frame with three tables (fs_yuvlook_apply): a
 * luma table, a chroma gain plus U/V offset per luma, and a 32x32 cell table
 * over (U,V) holding a gain and a luma shift. The develop path runs the real
 * grade in RGB (se_look_rows).
 *
 * This measures the difference, on the same input, over a sweep of luma and
 * chroma -- so "the shot look has no luma x chroma relation" is a number and
 * not an opinion.
 *
 *   ./tableerr stock.fs
 *
 * Grain and halation are switched off on BOTH sides: they are not reproduced
 * by the tables at all (documented, and not the question here), and leaving
 * them in would swamp the error being measured.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../engine/se.h"
#include "../engine/look.h"
#include "../engine/lut.h"
#include "../engine/stock.h"
#include "../../fs_yuvlook.h"

extern int fs_yuvlook_bake(const char *stock_path, int strength);

#define NP_Y   14
#define NP_ANG 12
#define NP_RAD 5

typedef struct { int y, u, v; } smp;

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void unpack(const unsigned char *s, unsigned char *o, int width)
{
    for (int x = 0; x + 1 < width; x += 2) {
        int u = (signed char)s[0], y0 = s[1], v = (signed char)s[2], y1 = s[3];
        o[0] = clamp8(y0 + ((359 * v + 128) >> 8));
        o[1] = clamp8(y0 - ((88 * u + 183 * v + 128) >> 8));
        o[2] = clamp8(y0 + ((454 * u + 128) >> 8));
        o[3] = clamp8(y1 + ((359 * v + 128) >> 8));
        o[4] = clamp8(y1 - ((88 * u + 183 * v + 128) >> 8));
        o[5] = clamp8(y1 + ((454 * u + 128) >> 8));
        s += 4;
        o += 6;
    }
}

/* RGB8 -> (Y, Cb, Cr), the same integers pack() uses. */
static void rgb_to_ycc(const unsigned char *p, int *Y, int *U, int *V)
{
    *Y = (77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8;
    *U = (-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8;
    *V = (128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8;
}

static int copy_row(void *ctx, const unsigned char *row, int width)
{
    memcpy(ctx, row, (size_t)width * 3);
    return 0;
}

/* Read a .fs file the way fs_yuvlook_bake does, so both paths see one stock. */
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

/* ------------------------------------------- 3D representations --- */
/*
 * The same look, resampled three ways and measured against the same rows.
 * `tables` above is the shipping 2D form; these are the 3D forms it could be
 * replaced with. Every one is fitted by running se_look itself, so nothing
 * here is an independent approximation of the look -- it is the look, sampled.
 */
static int run_look_rgb(const se_look *lk, const unsigned char *in,
                        unsigned char *out, int npix)
{
    int w = npix + (npix & 1);
    size_t need = se_look_bytes(lk, w);
    void *mem = malloc(need + 64);
    unsigned char *buf = calloc((size_t)w * 3, 1);
    unsigned char *obuf = calloc((size_t)w * 3, 1);
    se_arena ar;
    se_look_run *run;
    int rc = -1;

    if (!mem || !buf || !obuf) goto done;
    memcpy(buf, in, (size_t)npix * 3);
    se_arena_init(&ar, mem, need + 64);
    if (se_look_begin(&run, lk, w, &ar) != SE_OK) goto done;
    if (se_look_rows(run, buf, 1, copy_row, obuf) != SE_OK) goto done;
    se_look_end(run, copy_row, obuf);
    memcpy(out, obuf, (size_t)npix * 3);
    rc = 0;
done:
    free(mem); free(buf); free(obuf);
    return rc;
}

static void ycc_to_rgb(int y, int u, int v, unsigned char *o)
{
    o[0] = clamp8(y + ((359 * v) >> 8));
    o[1] = clamp8(y - ((88 * u + 183 * v) >> 8));
    o[2] = clamp8(y + ((454 * u) >> 8));
}

/* One sample's worst channel error: the two pixels of the pair, three channels. */
static int samp_err(const unsigned char *a, const unsigned char *b, int p)
{
    int ip, c, dmax = 0;
    for (ip = 0; ip < 2; ip++)
        for (c = 0; c < 3; c++) {
            int d = (int)a[(size_t)p * 6 + ip * 3 + c]
                  - (int)b[(size_t)p * 6 + ip * 3 + c];
            if (d < 0) d = -d;
            if (d > dmax) dmax = d;
        }
    return dmax;
}

/* A joint (Y,U,V) lattice holding the graded (Y,U,V) at each node. */
typedef struct { int n; int *y, *u, *v; } jlat;

static int jlat_build(jlat *L, const se_look *lk, int n)
{
    int nn = n * n * n, i, iy, iu, iv;
    unsigned char *in = malloc((size_t)nn * 3);
    unsigned char *out = malloc((size_t)nn * 3);
    int rc = -1;

    L->n = n;
    L->y = malloc(sizeof(int) * (size_t)nn);
    L->u = malloc(sizeof(int) * (size_t)nn);
    L->v = malloc(sizeof(int) * (size_t)nn);
    if (!in || !out || !L->y || !L->u || !L->v) goto done;

    i = 0;
    for (iv = 0; iv < n; iv++)
        for (iu = 0; iu < n; iu++)
            for (iy = 0; iy < n; iy++) {
                int y  = iy * 255 / (n - 1);
                int u  = -128 + iu * 256 / (n - 1);
                int v  = -128 + iv * 256 / (n - 1);
                ycc_to_rgb(y, u, v, in + (size_t)i * 3);
                i++;
            }
    if (run_look_rgb(lk, in, out, nn) != 0) goto done;

    for (i = 0; i < nn; i++) {
        int Y, U, V;
        rgb_to_ycc(out + (size_t)i * 3, &Y, &U, &V);
        L->y[i] = Y; L->u[i] = U; L->v[i] = V;
    }
    rc = 0;
done:
    free(in); free(out);
    return rc;
}

static void jlat_free(jlat *L) { free(L->y); free(L->u); free(L->v); L->y = NULL; }

/* Trilinear read, returning the graded (Y,U,V) for one input (y,u,v). */
static void jlat_at(const jlat *L, int y, int u, int v, int *oy, int *ou, int *ov)
{
    const int n = L->n;
    const double sy = 255.0 / (n - 1), sc = 256.0 / (n - 1);
    double ty = y / sy, tu = (u + 128) / sc, tv = (v + 128) / sc;
    int iy = (int)ty, iu = (int)tu, iv = (int)tv;
    double fy = ty - iy, fu = tu - iu, fv = tv - iv;
    int c;
    int *dst[3];

    if (iy > n - 2) { iy = n - 2; fy = 1.0; }
    if (iu > n - 2) { iu = n - 2; fu = 1.0; }
    if (iv > n - 2) { iv = n - 2; fv = 1.0; }
    if (iy < 0) { iy = 0; fy = 0.0; }
    if (iu < 0) { iu = 0; fu = 0.0; }
    if (iv < 0) { iv = 0; fv = 0.0; }

    dst[0] = &L->y[0]; dst[1] = &L->u[0]; dst[2] = &L->v[0];
    for (c = 0; c < 3; c++) {
        const int *d = dst[c];
#define NODE(a, b, e) d[(size_t)((iv + (e)) * n * n + (iu + (b)) * n + (iy + (a)))]
        double x00 = NODE(0,0,0) + (NODE(1,0,0) - NODE(0,0,0)) * fy;
        double x10 = NODE(0,0,1) + (NODE(1,0,1) - NODE(0,0,1)) * fy;
        double x01 = NODE(0,1,0) + (NODE(1,1,0) - NODE(0,1,0)) * fy;
        double x11 = NODE(0,1,1) + (NODE(1,1,1) - NODE(0,1,1)) * fy;
        double a = x00 + (x10 - x00) * fv;
        double b = x01 + (x11 - x01) * fv;
        int val = (int)(a + (b - a) * fu + 0.5);
#undef NODE
        if (c == 0) *oy = val; else if (c == 1) *ou = val; else *ov = val;
    }
}

typedef struct { int n, sum, max; } stat;

static void stat_add(stat *s, int d)
{
    s->n++;
    s->sum += d;
    if (d > s->max) s->max = d;

}

static void stat_show(const char *name, const stat *s)
{
    if (!s->n) { printf("  %-22s (no samples)\n", name); return; }
    printf("  %-22s n=%-5d mean %5.2f  max %3d\n",
           name, s->n, (double)s->sum / s->n, s->max);
}

int main(int argc, char **argv)
{
    static smp s[NP_Y * NP_ANG * NP_RAD + 3 * 1024];
    int ns = 0, i, a, r, p, ip;
    static const int ys[NP_Y] = { 24, 40, 56, 72, 88, 104, 128,
                                  152, 168, 184, 200, 216, 232, 244 };
    static const int rads[NP_RAD] = { 8, 24, 48, 80, 112 };

    se_stock st;
    se_look lk;
    unsigned char *uyvy, *tab, *rgb_t, *rgb_k, *rgb_out;
    int width;
    se_arena ar;
    se_look_run *run;
    stat all = {0}, by_chroma[4] = {{0}}, by_luma[4] = {{0}}, at128 = {0}, off128 = {0};
    stat centre = {0}, offcentre = {0}, centre128 = {0};
    long long csum_c = 0, csum_o = 0;

    int strength = (argc > 2) ? atoi(argv[2]) : 100;

    if (argc < 2) { fprintf(stderr, "usage: %s stock.fs [strength]\n", argv[0]); return 2; }

    for (i = 0; i < NP_Y; i++)
        for (a = 0; a < NP_ANG; a++)
            for (r = 0; r < NP_RAD; r++) {
                double th = a * 2.0 * M_PI / NP_ANG;
                int u = (int)lrint(rads[r] * cos(th));
                int v = (int)lrint(rads[r] * sin(th));
                int y = ys[i];
                int R = y + ((359 * v) >> 8);
                int G = y - ((88 * u + 183 * v) >> 8);
                int B = y + ((454 * u) >> 8);
                /* A sample that is outside the RGB cube cannot be a pixel. */
                if (R < 0 || R > 255 || G < 0 || G > 255 || B < 0 || B > 255)
                    continue;
                s[ns].y = y; s[ns].u = u; s[ns].v = v;
                ns++;
            }
    /* Exact cell centres, at three lumas: 128 is where cg/dy were probed, the
     * other two show what the missing luma coupling costs. */
    {
        static const int cyl[3] = { 64, 128, 192 };
        int li, cv, cu;
        for (li = 0; li < 3; li++)
            for (cv = 0; cv < 32; cv++)
                for (cu = 0; cu < 32; cu++) {
                    int u = cu * 8 + 4 - 128;
                    int v = cv * 8 + 4 - 128;
                    int y = cyl[li];
                    int R = y + ((359 * v) >> 8);
                    int G = y - ((88 * u + 183 * v) >> 8);
                    int B = y + ((454 * u) >> 8);
                    if (R < 0 || R > 255 || G < 0 || G > 255 || B < 0 || B > 255)
                        continue;
                    s[ns].y = y; s[ns].u = u; s[ns].v = v;
                    ns++;
                }
    }
    if (!ns) { fprintf(stderr, "no usable samples\n"); return 1; }

    if (load_stock(argv[1], &st) != 0) { fprintf(stderr, "parse %s failed\n", argv[1]); return 1; }

    /* Grain and halation off, so only the separable part is being compared --
     * the tables do not reproduce them at all, and that is not the question. */
    se_look_from_stock(&st, 1, &lk);
    se_look_set_strength(&lk, strength);
    lk.grain = 0;
    lk.hal_amount = 0;

    width = ns * 2;                       /* one pixel pair per sample */
    uyvy  = malloc((size_t)width * 2);
    tab   = malloc((size_t)width * 2);
    rgb_t = malloc((size_t)width * 3);
    rgb_k = malloc((size_t)width * 3);
    rgb_out = malloc((size_t)width * 3);
    if (!uyvy || !tab || !rgb_t || !rgb_k || !rgb_out) { fprintf(stderr, "oom\n"); return 1; }

    for (p = 0; p < ns; p++) {
        unsigned char *q = uyvy + (size_t)p * 4;
        q[0] = (unsigned char)(signed char)s[p].u;
        q[1] = (unsigned char)s[p].y;
        q[2] = (unsigned char)(signed char)s[p].v;
        q[3] = (unsigned char)s[p].y;
    }

    /* The develop: same bytes, unpacked, through the real grade. */
    unpack(uyvy, rgb_t, width);
    {
        size_t need = se_look_bytes(&lk, width);
        void *mem = malloc(need + 64);
        if (!mem) { fprintf(stderr, "oom arena\n"); return 1; }
        se_arena_init(&ar, mem, need + 64);
        if (se_look_begin(&run, &lk, width, &ar) != SE_OK) {
            fprintf(stderr, "se_look_begin failed\n");
            return 1;
        }
        if (se_look_rows(run, rgb_t, 1, copy_row, rgb_out) != SE_OK) {
            fprintf(stderr, "se_look_rows failed\n");
            return 1;
        }
        se_look_end(run, copy_row, rgb_out);
        free(mem);
    }
    memcpy(rgb_t, rgb_out, (size_t)width * 3);

    /* The shot path: the very function the camera runs. */
    memcpy(tab, uyvy, (size_t)width * 2);
    if (fs_yuvlook_bake(argv[1], strength) != 0) {
        fprintf(stderr, "bake %s failed\n", argv[1]);
        return 1;
    }
    fs_yuvlook_apply(tab, width, 1, width * 2);
    unpack(tab, rgb_k, width);

    printf("%s: %d samples, tables %s, develop = se_look_rows (grain 0, halo 0)\n",
           argv[1], ns, fs_yuvlook_name());

    for (p = 0; p < ns; p++) {
        int dmax = 0;
        int cmag = (s[p].u < 0 ? -s[p].u : s[p].u) + (s[p].v < 0 ? -s[p].v : s[p].v);
        for (ip = 0; ip < 2; ip++) {
            const unsigned char *t = rgb_k + (size_t)p * 6 + ip * 3;
            const unsigned char *k = rgb_t + (size_t)p * 6 + ip * 3;
            int c;
            for (c = 0; c < 3; c++) {
                int d = (int)t[c] - (int)k[c];
                if (d < 0) d = -d;
                if (d > dmax) dmax = d;
            }
        }
        stat_add(&all, dmax);
        stat_add(&by_chroma[cmag < 16 ? 0 : cmag < 64 ? 1 : cmag < 128 ? 2 : 3], dmax);
        {
            int y = s[p].y;
            stat_add(&by_luma[y < 64 ? 0 : y < 128 ? 1 : y < 192 ? 2 : 3], dmax);
        }
        if (s[p].y == 128) stat_add(&at128, dmax); else stat_add(&off128, dmax);
        {
            int ct = (((s[p].u + 128) & 7) == 4) && (((s[p].v + 128) & 7) == 4);
            stat_add(ct ? &centre : &offcentre, dmax);
            if (ct && s[p].y == 128) stat_add(&centre128, dmax);
            if (ct) csum_c += cmag; else csum_o += cmag;
        }
    }

    printf("\nmax channel error (levels, 0..255)\n");
    stat_show("all", &all);
    printf("\nby input chroma |u|+|v|\n");
    stat_show("0..15",     &by_chroma[0]);
    stat_show("16..63",    &by_chroma[1]);
    stat_show("64..127",   &by_chroma[2]);
    stat_show("128..2*127",&by_chroma[3]);
    printf("\nby input luma\n");
    stat_show("0..63",   &by_luma[0]);
    stat_show("64..127", &by_luma[1]);
    stat_show("128..191",&by_luma[2]);
    stat_show("192..255",&by_luma[3]);
    printf("\nthe cell table is probed at luma 128\n");
    stat_show("luma == 128", &at128);
    stat_show("luma != 128", &off128);
    /* Luma, chroma magnitude and hue, separately: which one is wrong says
     * which relation the table form cannot carry. */
    {
        long long sy = 0, sc = 0;
        double sh = 0.0;
        int my = 0, mc = 0;
        int nh = 0, n = 0;
        for (p = 0; p < ns; p++) {
            int ip2;
            for (ip2 = 0; ip2 < 2; ip2++) {
                int Yt, Ut, Vt, Yk, Uk, Vk, dy, dct, dck;
                const unsigned char *t = rgb_t + (size_t)p * 6 + ip2 * 3;
                const unsigned char *k = rgb_k + (size_t)p * 6 + ip2 * 3;
                rgb_to_ycc(k, &Yt, &Ut, &Vt);
                rgb_to_ycc(t, &Yk, &Uk, &Vk);
                dy = Yk - Yt; if (dy < 0) dy = -dy;
                sy += dy; if (dy > my) my = dy;
                dct = Ut < 0 ? -Ut : Ut; dct += Vt < 0 ? -Vt : Vt;
                dck = Uk < 0 ? -Uk : Uk; dck += Vk < 0 ? -Vk : Vk;
                {
                    int d = dck - dct; if (d < 0) d = -d;
                    sc += d; if (d > mc) mc = d;
                }
                n++;
                if (dct > 40 && dck > 40) {
                    double a1 = atan2((double)Vt, (double)Ut);
                    double a2 = atan2((double)Vk, (double)Uk);
                    double dh = a1 - a2;
                    while (dh > M_PI) dh -= 2 * M_PI;
                    while (dh < -M_PI) dh += 2 * M_PI;
                    sh += fabs(dh) * 180.0 / M_PI;
                    nh++;
                }
            }
        }
        printf("\ndecomposed (same samples, per pixel)\n");
        printf("  luma   |dY|          mean %5.2f  max %3d\n", (double)sy / n, my);
        printf("  chroma |C|           mean %5.2f  max %3d\n", (double)sc / n, mc);
        if (nh)
            printf("  hue, where |C|>40    mean %5.2f deg over %d px\n", sh / nh, nh);
    }

    printf("\nthe cell table was probed at exact cell centres\n");    stat_show("cell centre", &centre);
    stat_show("between cells", &offcentre);
    stat_show("luma 128 + centre", &centre128);
    printf("  (mean input |u|+|v|: centre %.1f, between %.1f -- a more saturated\n"
           "   set has a larger error, so compare these two with that in mind)\n",
           centre.n ? (double)csum_c / centre.n : 0.0,
           offcentre.n ? (double)csum_o / offcentre.n : 0.0);

    /* ------------------------------------------- 3D representations --- */
    {
        unsigned char *rgb_in = malloc((size_t)width * 3);
        unsigned char *r3 = malloc((size_t)width * 3);
        uint16_t *u16 = malloc(sizeof(uint16_t) * (size_t)width * 3);
        stat s_cube = {0}, s_j8 = {0}, s_j12 = {0}, s_j16 = {0};
        int kb_cube = 0, kb_j[3] = {0, 0, 0};

        if (!rgb_in || !r3 || !u16) {
            fprintf(stderr, "3d: out of memory\n");
        } else {
            unpack(uyvy, rgb_in, width);

            /* 17^3 RGB cube, read through the shipping tetrahedral apply. */
            {
                static se_lut3 cube;
                void *lm = malloc(512 * 1024);
                int N3 = 17, nn = N3 * N3 * N3, i3;

                if (lm) {
                    se_arena ar2;
                    unsigned char *lin = malloc((size_t)nn * 3);
                    unsigned char *lout = malloc((size_t)nn * 3);
                    int bi, gi, ri, x;

                    se_arena_init(&ar2, lm, 512 * 1024);
                    if (lin && lout && se_lut3_identity(&cube, N3, &ar2) == SE_OK) {
                        i3 = 0;
                        for (bi = 0; bi < N3; bi++)
                        for (gi = 0; gi < N3; gi++)
                        for (ri = 0; ri < N3; ri++) {
                            lin[i3 * 3 + 0] = (unsigned char)(ri * 255 / (N3 - 1));
                            lin[i3 * 3 + 1] = (unsigned char)(gi * 255 / (N3 - 1));
                            lin[i3 * 3 + 2] = (unsigned char)(bi * 255 / (N3 - 1));
                            i3++;
                        }
                        if (run_look_rgb(&lk, lin, lout, nn) == 0) {
                            for (i3 = 0; i3 < nn * 3; i3++)
                                cube.data[i3] = (uint16_t)(lout[i3] * 257);
                            for (x = 0; x < width * 3; x++)
                                u16[x] = (uint16_t)(rgb_in[x] * 257);
                            se_lut3_apply_row(&cube, u16, width);
                            for (x = 0; x < width * 3; x++)
                                r3[x] = (unsigned char)((u16[x] + 128u) / 257u);
                            for (p = 0; p < ns; p++)
                                stat_add(&s_cube, samp_err(r3, rgb_t, p));
                            kb_cube = (int)se_lut3_bin_bytes(N3);
                        }
                    }
                    free(lin); free(lout);
                }
                free(lm);
            }

            /* Joint (Y,U,V) lattice: no RGB conversion at run time. */
            {
                static const int sizes[3] = { 8, 12, 16 };
                int k;
                for (k = 0; k < 3; k++) {
                    jlat L;
                    if (jlat_build(&L, &lk, sizes[k]) != 0) { jlat_free(&L); continue; }
                    for (p = 0; p < ns; p++) {
                        unsigned char px[3];
                        int Y, U, V;
                        jlat_at(&L, s[p].y, s[p].u, s[p].v, &Y, &U, &V);
                        ycc_to_rgb(Y, U, V, px);
                        memcpy(r3 + (size_t)p * 6, px, 3);
                        memcpy(r3 + (size_t)p * 6 + 3, px, 3);
                    }
                    for (p = 0; p < ns; p++) {
                        stat *t = k == 0 ? &s_j8 : (k == 1 ? &s_j12 : &s_j16);
                        stat_add(t, samp_err(r3, rgb_t, p));
                    }
                    kb_j[k] = (int)((size_t)sizes[k] * sizes[k] * sizes[k] * 6);
                    jlat_free(&L);
                }
            }

            printf("\n3D forms of the same look, same samples, against the same develop\n");
            printf("  %-34s %s\n", "shipping tables 2x2D (9228 B)", "(see 'all' above)");
            stat_show("17^3 RGB cube, tetrahedral", &s_cube);
            stat_show("joint (y,u,v) 8^3, 3.0 KB", &s_j8);
            stat_show("joint (y,u,v) 12^3, 10.1 KB", &s_j12);
            stat_show("joint (y,u,v) 16^3, 24.0 KB", &s_j16);
            printf("  (cube on card %d B, joints %d/%d/%d B)\n",
                   kb_cube, kb_j[0], kb_j[1], kb_j[2]);
        }
        free(rgb_in); free(r3); free(u16);
    }

    free(uyvy); free(tab); free(rgb_t); free(rgb_k); free(rgb_out);
    return 0;
}

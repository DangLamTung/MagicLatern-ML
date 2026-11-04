/*
 * lutchk - does an exported cube still hold the recipe it came from?
 *
 * fs2cube writes a .cube by running a .fs over an RGB lattice; cube2bin packs
 * that to 17^3; the develop applies the packed form before the look. This
 * walks the same road and compares the two ends on the same lattice, using the
 * engine for both sides so nothing is re-implemented:
 *
 *   A  the recipe:            se_look_rows(recipe)
 *   B  the cube, then a flat look:  LUT -> se_look_rows(flat, SAT 100)
 *
 * A - B is what the projection and the 17^3 resample cost.
 *
 *   ./lutchk recipe.fs flat.fs packed.bin [size]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/se.h"
#include "../engine/look.h"
#include "../engine/lut.h"
#include "../engine/stock.h"

#define ROW_MAX 5472

static int copy_row(void *ctx, const unsigned char *row, int width)
{
    memcpy(ctx, row, (size_t)width * 3);
    return 0;
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

static void grade(const char *stock, unsigned char *rgb, unsigned char *out,
                  int total)
{
    se_stock st;
    se_look lk;
    se_arena ar;
    se_look_run *run;
    void *mem;
    size_t need;

    if (load_stock(stock, &st) != 0) {
        fprintf(stderr, "parse %s failed\n", stock);
        exit(1);
    }
    se_look_from_stock(&st, 1, &lk);
    se_look_set_strength(&lk, 100);
    lk.grain = 0;       /* not a per-pixel colour function, and not in a cube */
    lk.hal_amount = 0;  /* needs a neighbourhood, and not in a cube either     */

    need = se_look_bytes(&lk, total);
    mem = malloc(need + 64);
    if (!mem) { fprintf(stderr, "oom\n"); exit(1); }
    se_arena_init(&ar, mem, need + 64);
    if (se_look_begin(&run, &lk, total, &ar) != SE_OK) {
        fprintf(stderr, "se_look_begin failed\n");
        exit(1);
    }
    if (se_look_rows(run, rgb, 1, copy_row, out) != SE_OK) {
        fprintf(stderr, "se_look_rows failed\n");
        exit(1);
    }
    se_look_end(run, copy_row, out);
    free(mem);
}

/* The develop's own conversion: 8-bit -> 16, tetrahedral, back with rounding. */
static void apply_lut(const se_lut3 *cube, unsigned char *rgb, int total)
{
    static uint16_t buf[ROW_MAX * 3];
    int done = 0;

    while (done < total) {
        int n = total - done;
        int x;
        if (n > ROW_MAX) n = ROW_MAX;
        for (x = 0; x < n * 3; x++)
            buf[x] = (uint16_t)(rgb[(size_t)done * 3 + x] * 257u);
        se_lut3_apply_row(cube, buf, n);
        for (x = 0; x < n * 3; x++)
            rgb[(size_t)done * 3 + x] = (uint8_t)((buf[x] + 128u) / 257u);
        done += n;
    }
}

int main(int argc, char **argv)
{
    int n = (argc > 4) ? atoi(argv[4]) : 33;
    int total, i;
    unsigned char *probe, *a, *withlut;
    FILE *f;
    long sz;
    void *bin;
    se_lut3 cube;
    se_arena ar;
    long long sum = 0;
    int worst = 0, over8 = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: %s recipe.fs flat.fs packed.bin [size]\n", argv[0]);
        return 2;
    }
    if (n < 2 || n > 49) { fprintf(stderr, "size out of range\n"); return 2; }
    total = n * n * n;

    probe = malloc((size_t)total * 3);
    a = malloc((size_t)total * 3);
    withlut = malloc((size_t)total * 3);
    if (!probe || !a || !withlut) { fprintf(stderr, "oom\n"); return 1; }

    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++) {
                unsigned char *p = probe + ((size_t)b * n * n + (size_t)g * n + r) * 3;
                p[0] = (unsigned char)(r * 255 / (n - 1));
                p[1] = (unsigned char)(g * 255 / (n - 1));
                p[2] = (unsigned char)(b * 255 / (n - 1));
            }

    /* A: the recipe */
    memcpy(a, probe, (size_t)total * 3);
    grade(argv[1], probe, a, total);

    /* B: the packed cube, then the flat recipe */
    f = fopen(argv[3], "rb");
    if (!f) { perror(argv[3]); return 1; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    bin = malloc((size_t)sz);
    if (!bin || fread(bin, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "read %s failed\n", argv[3]);
        return 1;
    }
    fclose(f);
    {
        /* the loader wants an arena for the cube's copy */
        size_t need = (size_t)64 * 1024 * 1024;
        void *mem = malloc(need);
        if (!mem) { fprintf(stderr, "oom arena\n"); return 1; }
        se_arena_init(&ar, mem, need);
        if (se_lut3_load_bin(bin, (size_t)sz, &cube, &ar) != SE_OK) {
            fprintf(stderr, "%s is not a packed cube\n", argv[3]);
            return 1;
        }
    }
    memcpy(withlut, probe, (size_t)total * 3);
    apply_lut(&cube, withlut, total);
    grade(argv[2], withlut, withlut, total);

    for (i = 0; i < total; i++)
        for (int c = 0; c < 3; c++) {
            int d = (int)a[i * 3 + c] - (int)withlut[i * 3 + c];
            if (d < 0) d = -d;
            sum += d;
            if (d > worst) worst = d;
            if (d > 8) over8++;
        }
    printf("%s vs %s + %s: mean %.2f levels, worst %d, %.2f%% over 8 "
           "(%d^3 points, cube %d^3)\n",
           argv[1], argv[2], argv[3],
           (double)sum / (total * 3), worst, 100.0 * over8 / (total * 3),
           n, cube.size);
    return 0;
}

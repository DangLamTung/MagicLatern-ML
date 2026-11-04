/*
 * fs2cube - a .fs recipe, exported as a 3D .cube.
 *
 * The reverse of lut2fs: instead of pushing a cube into the recipe vocabulary,
 * this runs the recipe over an RGB lattice and writes what it does as a cube.
 * That makes a look portable -- the same colour can be handed to Resolve, to
 * an external LUT box, or back to this develop through its LUT path -- and it
 * is how a "custom LUT with a recipe" is made here: the recipe is the source of
 * truth, the cube is a render of it.
 *
 * Grain and halation are switched off: neither is a per-pixel colour function,
 * so neither belongs in a cube. They stay in the recipe.
 *
 *   ./fs2cube stock.fs out.cube [size]
 *
 * The output is a standard Adobe/Resolve .cube: LUT_3D_SIZE n, then n^3 lines
 * of "r g b" in 0..1, red varying fastest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/se.h"
#include "../engine/look.h"
#include "../engine/stock.h"

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

int main(int argc, char **argv)
{
    int n = (argc > 3) ? atoi(argv[3]) : 33;
    se_stock st;
    se_look lk;
    int total, i, c;
    unsigned char *rgb, *out;
    se_arena ar;
    se_look_run *run;
    void *mem;
    size_t need;
    FILE *fout;

    if (argc < 3) {
        fprintf(stderr, "usage: %s stock.fs out.cube [size]\n", argv[0]);
        return 2;
    }
    if (n < 2 || n > 65) { fprintf(stderr, "size out of range\n"); return 2; }
    if (load_stock(argv[1], &st) != 0) {
        fprintf(stderr, "parse %s failed\n", argv[1]);
        return 1;
    }

    se_look_from_stock(&st, 1, &lk);
    se_look_set_strength(&lk, 100);
    lk.grain = 0;         /* not a per-pixel colour function */
    lk.hal_amount = 0;    /* needs a neighbourhood             */

    total = n * n * n;
    rgb = malloc((size_t)total * 3);
    out = malloc((size_t)total * 3);
    if (!rgb || !out) { fprintf(stderr, "oom\n"); return 1; }

    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++) {
                unsigned char *p = rgb + ((size_t)b * n * n + (size_t)g * n + r) * 3;
                p[0] = (unsigned char)(r * 255 / (n - 1));
                p[1] = (unsigned char)(g * 255 / (n - 1));
                p[2] = (unsigned char)(b * 255 / (n - 1));
            }

    need = se_look_bytes(&lk, total);
    mem = malloc(need + 64);
    if (!mem) { fprintf(stderr, "oom arena\n"); return 1; }
    se_arena_init(&ar, mem, need + 64);
    if (se_look_begin(&run, &lk, total, &ar) != SE_OK) {
        fprintf(stderr, "se_look_begin failed\n");
        return 1;
    }
    if (se_look_rows(run, rgb, 1, copy_row, out) != SE_OK) {
        fprintf(stderr, "se_look_rows failed\n");
        return 1;
    }
    se_look_end(run, copy_row, out);

    fout = fopen(argv[2], "w");
    if (!fout) { perror(argv[2]); return 1; }
    fprintf(fout, "TITLE \"%s\"\n", st.name);
    fprintf(fout, "# exported from a .fs recipe by fs2cube; grain and halation\n"
                  "# are not in it -- they are not a per-pixel colour function\n");
    fprintf(fout, "LUT_3D_SIZE %d\n", n);
    fprintf(fout, "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n");
    for (i = 0; i < total; i++) {
        fprintf(fout, "%.6f %.6f %.6f\n",
                out[i * 3 + 0] / 255.0, out[i * 3 + 1] / 255.0,
                out[i * 3 + 2] / 255.0);
    }
    fclose(fout);
    (void)c;

    printf("%s -> %s (%d^3, %d points)\n", argv[1], argv[2], n, total);
    free(rgb);
    free(out);
    free(mem);
    return 0;
}

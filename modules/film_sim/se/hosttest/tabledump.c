/* Dump the shot-time tables a .fs recipe bakes into, as text.
 *
 * The 2D part of the look is 32x32 cells over (U,V) -- too small to see in the
 * source and impossible to judge from the recipe, so this prints the actual
 * baked values for plotting. Nothing here is reimplemented: it calls the same
 * fs_yuvlook_bake() the camera calls.
 *
 *   ./tabledump stock.fs > tables.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/look.h"
#include "../engine/stock.h"
#include "../../fs_yuvlook.h"

extern int fs_yuvlook_bake(const char *stock_path, int strength);

/* fs_yuvlook.c keeps its baked table to itself; the accessor below is the one
 * the plot needs. Declared here rather than in the module header because the
 * camera never asks for it. */

int main(int argc, char **argv)
{
    const se_yuvtbl *t;
    int i, cu, cv;

    if (argc < 2) { fprintf(stderr, "usage: %s stock.fs\n", argv[0]); return 2; }
    if (fs_yuvlook_bake(argv[1], 100) != 0) { fprintf(stderr, "bake failed\n"); return 1; }
    t = fs_yuvlook_table();
    if (!t) { fprintf(stderr, "no table\n"); return 1; }

    printf("# name %s\n", fs_yuvlook_name());
    printf("# strength %d\n", t->strength);
    printf("[1D]\ny\tgain\tuoff\tvoff\n");
    for (i = 0; i < 256; i++)
        printf("%d\t%d\t%d\t%d\n", t->y[i], t->gain[i], t->uoff[i], t->voff[i]);
    printf("[MATRIX]\n%d\t%d\t%d\t%d\n",
           t->matrix[0], t->matrix[1], t->matrix[2], t->matrix[3]);
    printf("[2D]\ncg\tdy\n");
    for (cv = 0; cv < SE_YUV_GRID; cv++)
        for (cu = 0; cu < SE_YUV_GRID; cu++) {
            int k = cv * SE_YUV_GRID + cu;
            printf("%d\t%d\n", t->cg[k], t->dy[k]);
        }
    return 0;
}

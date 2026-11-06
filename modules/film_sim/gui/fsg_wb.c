/*
 * The WB shift screen, drawn.
 *
 * fsg_fill() and fsg_text() only, like every other screen here. The 19x19 grid
 * uses a 10x10 palette table (100 entries in 80..179 via fsg_pal()), so the
 * camera's 8-bit overlay displays the actual colour casts rather than falling
 * back to white in ml_color().
 */
#include <stdio.h>
#include <string.h>

#include "fsg.h"
#include "fsg_wb.h"

#define GRID_N    19        /* -9 .. +9 inclusive */
#define CELL      15
#define GRID_PX   (GRID_N * CELL)
#define WB_PAL_D  10        /* 10x10 = 100 palette slots (< FSG_PAL_N = 175) */

/*
 * Fuji's two axes as tints:
 *   RED +  adds red, RED -  adds cyan (green and blue together)
 *   BLUE + adds blue, BLUE - adds yellow (red and green together)
 * so the top of the grid is blue everywhere and the right edge is warm.
 */
static int cell_colour_rgb(int r, int b)
{
    int rr = 128 + r * 7 + (b < 0 ? -b * 4 : 0);
    int gg = 128 - r * 4 + (b < 0 ? -b * 4 : b * 2);
    int bb = 128 + b * 7 - r * 4;
    int dark = (r * r + b * b) / 6;

    if (rr < 40) rr = 40;
    if (rr > 235) rr = 235;
    if (gg < 40) gg = 40;
    if (gg > 235) gg = 235;
    if (bb < 40) bb = 40;
    if (bb > 235) bb = 235;
    if (dark > 18) dark = 18;
    rr = rr * (30 - dark) / 30;
    gg = gg * (30 - dark) / 30;
    bb = bb * (30 - dark) / 30;
    return (rr << 16) | (gg << 8) | bb;
}

static void wb_register_palette(void)
{
    int pal[WB_PAL_D * WB_PAL_D];
    int bi, bj;
    for (bj = 0; bj < WB_PAL_D; bj++) {
        int b = 9 - (bj * 18) / (WB_PAL_D - 1);
        for (bi = 0; bi < WB_PAL_D; bi++) {
            int r = -9 + (bi * 18) / (WB_PAL_D - 1);
            pal[bj * WB_PAL_D + bi] = cell_colour_rgb(r, b);
        }
    }
    fsg_pal(pal, WB_PAL_D * WB_PAL_D);
}

static int cell_slot(int i, int j)
{
    int bi = (i * (WB_PAL_D - 1) + 9) / 18;
    int bj = (j * (WB_PAL_D - 1) + 9) / 18;
    if (bi < 0) bi = 0;
    if (bi >= WB_PAL_D) bi = WB_PAL_D - 1;
    if (bj < 0) bj = 0;
    if (bj >= WB_PAL_D) bj = WB_PAL_D - 1;
    return FSG_PIX(bj * WB_PAL_D + bi);
}

static void rect_outline(int x, int y, int w, int h, int c)
{
    fsg_fill(x, y, w, 1, c);
    fsg_fill(x, y + h - 1, w, 1, c);
    fsg_fill(x, y, 1, h, c);
    fsg_fill(x + w - 1, y, 1, h, c);
}

static void corners(int x, int y, int w, int h, int rgb, int len)
{
    fsg_fill(x, y, len, 2, rgb);
    fsg_fill(x, y, 2, len, rgb);
    fsg_fill(x + w - len, y, len, 2, rgb);
    fsg_fill(x + w - 2, y, 2, len, rgb);
    fsg_fill(x, y + h - 2, len, 2, rgb);
    fsg_fill(x, y + h - len, 2, len, rgb);
    fsg_fill(x + w - len, y + h - 2, len, 2, rgb);
    fsg_fill(x + w - 2, y + h - len, 2, len, rgb);
}

static void panel_card(int x, int y, int w, int h)
{
    fsg_fill(x + 3, y + 3, w, h, FSG_BLACK);
    fsg_fill(x, y, w, h, FSG_PANEL);
    rect_outline(x, y, w, h, FSG_LINE);
    corners(x, y, w, h, FSG_AMBER_DIM, 14);
}

static void header(const char *right)
{
    fsg_fill(0, 0, FSG_W, 44, FSG_RED_DEEP);
    fsg_fill(0, 42, FSG_W, 2, FSG_RED);
    fsg_fill(0, 0, 6, 44, FSG_AMBER);
    fsg_text(18, 7, FSG_T_MED, FSG_AMBER, FSG_RED_DEEP, "FUJI WB SHIFT");
    fsg_text(18, 27, FSG_T_SMALL, FSG_GREY, FSG_RED_DEEP,
             "RECIPE OFFSET ON TOP OF THE STOCK'S OWN BALANCE");
    if (right && right[0]) {
        int rx = FSG_W - 18 - (int)strlen(right) * 11;
        fsg_text(rx, 13, FSG_T_MED, FSG_WHITE, FSG_RED_DEEP, "%s", right);
    }
}

static void footer(const char *l, const char *r)
{
    fsg_fill(0, FSG_H - 36, FSG_W, 36, FSG_BG_DEEP);
    fsg_fill(0, FSG_H - 36, FSG_W, 1, FSG_LINE);
    if (l) fsg_text(16, FSG_H - 24, FSG_T_SMALL, FSG_GREY, FSG_BG_DEEP, "%s", l);
    if (r) {
        int rx = FSG_W - 16 - (int)strlen(r) * 8;
        fsg_text(rx, FSG_H - 24, FSG_T_SMALL, FSG_AMBER, FSG_BG_DEEP, "%s", r);
    }
}

void fsg_draw_wb(const fs_wb_t *w)
{
    const int sx = 18, sy = 54, sw = 392, sh = 374;
    const int gx = sx + (sw - GRID_PX) / 2, gy = sy + 44;
    const int cx = gx + (GRID_N / 2) * CELL + CELL / 2;
    const int cy = gy + (GRID_N / 2) * CELL + CELL / 2;
    const int px = 424, py = 54, pw = 278, ph = 374;
    const int ix = px + 18, iw = pw - 36;
    char buf[48];
    int i, j;

    wb_register_palette();

    fsg_fill(0, 0, FSG_W, FSG_H, FSG_BG);
    header(w->applied ? "IN CAMERA" : (w->moved ? "NOT APPLIED" : "NEUTRAL"));

    /* Left card: 19x19 Chromatic WB Grid */
    panel_card(sx, sy, sw, sh);

    /* Cardinal axis labels around the 19x19 Fuji WB matrix */
    fsg_text(cx - 42, sy + 18, FSG_T_SMALL, FSG_WHITE, FSG_PANEL, "B +9 (BLUE)");
    fsg_text(cx - 46, gy + GRID_PX + 10, FSG_T_SMALL, FSG_AMBER, FSG_PANEL,
             "B -9 (AMBER)");
    fsg_text(sx + 10, cy - 6, FSG_T_SMALL, FSG_GREY, FSG_PANEL, "R-9");
    fsg_text(gx + GRID_PX + 8, cy - 6, FSG_T_SMALL, FSG_AMBER, FSG_PANEL, "R+9");

    /* Outer border around the 19x19 matrix */
    fsg_fill(gx - 2, gy - 2, GRID_PX + 3, GRID_PX + 3, FSG_BG_DEEP);
    rect_outline(gx - 2, gy - 2, GRID_PX + 3, GRID_PX + 3, FSG_LINE);

    /* 361 tinted cells via palette slots */
    for (j = 0; j < GRID_N; j++)
        for (i = 0; i < GRID_N; i++)
            fsg_fill(gx + i * CELL, gy + j * CELL, CELL - 1, CELL - 1,
                     cell_slot(i, j));

    /* Centre (0, 0) axes hairlines + neutral cell outline */
    fsg_fill(cx, gy, 1, GRID_PX - 1, FSG_LINE_DIM);
    fsg_fill(gx, cy, GRID_PX - 1, 1, FSG_LINE_DIM);
    rect_outline(gx + 9 * CELL, gy + 9 * CELL, CELL - 1, CELL - 1, FSG_LINE);

    /* Stock's own shift marker (subtle grey outline box on Fuji axes) */
    {
        int sr = -w->stock_gm, sb = -w->stock_ba;
        if (sr >= -9 && sr <= 9 && sb >= -9 && sb <= 9 && (sr || sb)) {
            int spx = gx + (sr + 9) * CELL;
            int spy = gy + (9 - sb) * CELL;
            rect_outline(spx, spy, CELL - 1, CELL - 1, FSG_WHITE);
        }
    }

    /* Active user crosshair */
    {
        int ux = gx + (w->r + 9) * CELL;
        int uy = gy + (9 - w->b) * CELL;
        int c = w->applied ? FSG_GREEN : FSG_AMBER;

        fsg_fill(ux - 1, uy - 1, CELL + 1, CELL + 1, c);
        fsg_fill(ux + 1, uy + 1, CELL - 3, CELL - 3, FSG_BG_DEEP);
        fsg_fill(ux + 5, uy + 5, 5, 5, c);
    }

    /* Right card: Three-tier WB telemetry */
    panel_card(px, py, pw, ph);
    {
        int y = py + 16;
        int abs_ba = w->stock_ba + w->ba;
        int abs_gm = w->stock_gm + w->gm;

        if (abs_ba > 9) abs_ba = 9;
        if (abs_ba < -9) abs_ba = -9;
        if (abs_gm > 9) abs_gm = 9;
        if (abs_gm < -9) abs_gm = -9;

        fsg_text(ix, y, FSG_T_SMALL, FSG_AMBER, FSG_PANEL, "FUJI RECIPE OFFSET");
        y += 20;
        snprintf(buf, sizeof buf, "R %+d", w->r);
        fsg_text(ix, y, FSG_T_LARGE, FSG_AMBER, FSG_PANEL, "%s", buf);
        snprintf(buf, sizeof buf, "B %+d", w->b);
        fsg_text(ix + 116, y, FSG_T_LARGE, FSG_AMBER, FSG_PANEL, "%s", buf);
        y += 38;

        fsg_fill(ix, y, iw, 1, FSG_LINE_DIM);
        y += 14;

        fsg_text(ix, y, FSG_T_SMALL, FSG_GREY, FSG_PANEL, "STOCK BASE (B/A, G/M)");
        y += 20;
        snprintf(buf, sizeof buf, "B/A %+d", w->stock_ba);
        fsg_text(ix, y, FSG_T_MED, FSG_GREY, FSG_PANEL, "%s", buf);
        snprintf(buf, sizeof buf, "G/M %+d", w->stock_gm);
        fsg_text(ix + 116, y, FSG_T_MED, FSG_GREY, FSG_PANEL, "%s", buf);
        y += 32;

        fsg_fill(ix, y, iw, 1, FSG_LINE_DIM);
        y += 14;

        fsg_text(ix, y, FSG_T_SMALL, FSG_WHITE, FSG_PANEL, "CAMERA TOTAL (PROP_WBS)");
        y += 20;
        snprintf(buf, sizeof buf, "B/A %+d", abs_ba);
        fsg_text(ix, y, FSG_T_MED, FSG_WHITE, FSG_PANEL, "%s", buf);
        snprintf(buf, sizeof buf, "G/M %+d", abs_gm);
        fsg_text(ix + 116, y, FSG_T_MED, FSG_WHITE, FSG_PANEL, "%s", buf);
        y += 28;

        fsg_text(ix, y, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "THERMAL SHIFT");
        snprintf(buf, sizeof buf, "%+d MIRED", abs_ba * 5);
        fsg_text(ix + iw - (int)strlen(buf) * 8, y, FSG_T_SMALL,
                 FSG_GREY, FSG_PANEL, "%s", buf);
        y += 32;

        /* Status pill */
        if (w->applied) {
            fsg_fill(ix, y, iw, 26, FSG_GREEN_DARK);
            rect_outline(ix, y, iw, 26, FSG_GREEN);
            fsg_text(ix + 10, y + 7, FSG_T_SMALL, FSG_GREEN, FSG_GREEN_DARK,
                     "APPLIED TO CAMERA WB");
        } else if (w->moved) {
            fsg_fill(ix, y, iw, 26, FSG_RED_DEEP);
            rect_outline(ix, y, iw, 26, FSG_AMBER);
            fsg_text(ix + 10, y + 7, FSG_T_SMALL, FSG_AMBER, FSG_RED_DEEP,
                     "PRESS SET TO WRITE WB");
        } else {
            fsg_fill(ix, y, iw, 26, FSG_BG_DEEP);
            rect_outline(ix, y, iw, 26, FSG_LINE_DIM);
            fsg_text(ix + 10, y + 7, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG_DEEP,
                     "OFFSET ZERO (STOCK ONLY)");
        }
    }

    footer("DIAL/ARROWS move   SET apply to camera", "Q zero   MENU back");
}

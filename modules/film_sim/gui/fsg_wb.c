/*
 * The WB shift screen, drawn.
 *
 * fsg_fill() and fsg_text() only, like every other screen here -- so the grid
 * is square cells and the "axes" are bands of colour, not gradients: the
 * panel's palette has 175 indices and no blend.
 */
#include <stdio.h>

#include "fsg.h"
#include "fsg_wb.h"

#define GRID_N   19        /* -9 .. +9 inclusive */
#define CELL     16
#define GRID_PX  (GRID_N * CELL)

/* Cell colour at (ba, gm): the cast that cell stands for, as a tint over the
 * neutral panel. A cell near the centre is almost neutral, one at a corner is
 * strongly cast -- the values, not the palette, decide. */
/*
 * Sign conventions come from ML's own shot.c, not from taste:
 *   PROP_WBS_BA > 0 is AMBER, < 0 is BLUE   (wbs_ba_display)
 *   PROP_WBS_GM > 0 is GREEN, < 0 is MAGENTA (wbs_gm_display)
 * Getting these backwards would draw a grid that pushes the opposite cast to
 * the camera, which is the one thing this screen exists to prevent.
 */
static int cell_colour(int r, int b)
{
    /*
     * Fuji's two axes as tints:
     *   RED +  adds red, RED -  adds cyan (green and blue together)
     *   BLUE + adds blue, BLUE - adds yellow (red and green together)
     * so the top of the grid is blue everywhere and the right edge is warm,
     * which is what the four labels say.
     */
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
    /* the rim reads brighter so the grid has an edge without a border fill */
    if (dark > 18) dark = 18;
    rr = rr * (30 - dark) / 30;
    gg = gg * (30 - dark) / 30;
    bb = bb * (30 - dark) / 30;
    return (rr << 16) | (gg << 8) | bb;
}

static void header(const char *right)
{
    fsg_fill(0, 0, FSG_W, 44, FSG_RED_DEEP);
    fsg_fill(0, 42, FSG_W, 2, FSG_RED_DARK);
    fsg_fill(0, 0, 6, 44, FSG_AMBER);
    fsg_text(20, 9, FSG_T_MED, FSG_AMBER, FSG_RED_DEEP, "FUJI WB SHIFT");
    fsg_text(20, 30, FSG_T_SMALL, 0xE8C9A0, FSG_RED_DEEP,
             "AN OFFSET ON TOP OF THE STOCK'S OWN BALANCE");
    fsg_text(536, 15, FSG_T_MED, FSG_WHITE, FSG_RED_DEEP, "%s", right);
}

static void footer(const char *s)
{
    fsg_fill(0, FSG_H - 44, FSG_W, 44, FSG_BG_DEEP);
    fsg_fill(0, FSG_H - 44, FSG_W, 1, FSG_LINE);
    fsg_text(16, FSG_H - 33, FSG_T_SMALL, FSG_GREY, FSG_BG_DEEP, "%s", s);
}

void fsg_draw_wb(const fs_wb_t *w)
{
    const int gx = 84, gy = 84;
    const int cx = gx + (GRID_N / 2) * CELL + CELL / 2;
    const int cy = gy + (GRID_N / 2) * CELL + CELL / 2;
    char buf[48];
    int i, j;

    fsg_fill(0, 0, FSG_W, FSG_H, FSG_BG);
    header(w->applied ? "IN CAMERA" : (w->moved ? "NOT APPLIED" : "NEUTRAL"));

    /* the grid, one fill per cell: 361 of them, drawn on a key press only */
    for (j = 0; j < GRID_N; j++)
        for (i = 0; i < GRID_N; i++)
            fsg_fill(gx + i * CELL, gy + j * CELL, CELL - 1, CELL - 1,
                     cell_colour(i - 9, 9 - j));

    /* centre cross, so neutral is locatable without counting cells */
    fsg_fill(cx - 1, gy, 2, GRID_PX, FSG_BG_DEEP);
    fsg_fill(gx, cy - 1, GRID_PX, 2, FSG_BG_DEEP);

    /* the crosshair: what is selected */
    {
        int px = gx + (w->r + 9) * CELL;
        int py = gy + (9 - w->b) * CELL;
        int c = w->applied ? FSG_GREEN : FSG_AMBER;

        fsg_fill(px - 1, py - 1, CELL + 1, CELL + 1, c);
        fsg_fill(px + 1, py + 1, CELL - 3, CELL - 3, FSG_BG_DEEP);
        fsg_fill(px + 5, py + 5, 5, 5, c);
    }

    /* axis labels, outside the grid */
    fsg_text(gx, gy - 22, FSG_T_SMALL, FSG_GREY, FSG_BG, "BLUE +");
    fsg_text(gx + GRID_PX - 60, gy - 22, FSG_T_SMALL, FSG_GREY, FSG_BG, "BLUE -");
    fsg_text(gx - 52, gy + GRID_PX / 2 - 6, FSG_T_SMALL, FSG_GREY, FSG_BG, "RED -");
    fsg_text(gx + GRID_PX + 8, gy + GRID_PX / 2 - 6, FSG_T_SMALL, FSG_GREY, FSG_BG,
             "RED +");

    /*
     * Three numbers, because there are three of them and confusing any two is
     * the whole failure mode: the OFFSET this screen sets, the stock's own
     * shift, and what the camera therefore holds. Showing the offset alone
     * was misleading -- the camera gets offset + stock.
     */
    {
        const int x = gx + GRID_PX + 92;
        int y = gy + 4;
        int abs_ba = w->stock_ba + w->ba;
        int abs_gm = w->stock_gm + w->gm;

        if (abs_ba > 9) abs_ba = 9;
        if (abs_ba < -9) abs_ba = -9;
        if (abs_gm > 9) abs_gm = 9;
        if (abs_gm < -9) abs_gm = -9;

        /* the recipe's own numbers, R then B, as Fuji prints them */
        fsg_text(x, y, FSG_T_SMALL, FSG_GREY, FSG_BG, "RED / BLUE");
        snprintf(buf, sizeof buf, "%+d", w->r);
        fsg_text(x, y + 16, FSG_T_LARGE, FSG_AMBER, FSG_BG, "%s", buf);
        snprintf(buf, sizeof buf, "%+d", w->b);
        fsg_text(x + 66, y + 16, FSG_T_LARGE, FSG_AMBER, FSG_BG, "%s", buf);

        fsg_text(x, y + 56, FSG_T_SMALL, FSG_GREY, FSG_BG, "STOCK B/A, G/M");
        snprintf(buf, sizeof buf, "%+d", w->stock_ba);
        fsg_text(x, y + 72, FSG_T_MED, FSG_GREY, FSG_BG, "%s", buf);
        snprintf(buf, sizeof buf, "%+d", w->stock_gm);
        fsg_text(x + 66, y + 72, FSG_T_MED, FSG_GREY, FSG_BG, "%s", buf);

        fsg_text(x, y + 104, FSG_T_SMALL, FSG_GREY, FSG_BG, "CAMERA B/A, G/M");
        snprintf(buf, sizeof buf, "%+d", abs_ba);
        fsg_text(x, y + 120, FSG_T_MED, FSG_WHITE, FSG_BG, "%s", buf);
        snprintf(buf, sizeof buf, "%+d", abs_gm);
        fsg_text(x + 66, y + 120, FSG_T_MED, FSG_WHITE, FSG_BG, "%s", buf);

        /* 5 mireks a unit, the number ML's own menu quotes */
        snprintf(buf, sizeof buf, "%+d mired", abs_ba * 5);
        fsg_text(x, y + 152, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG, "%s", buf);

        if (w->moved && !w->applied)
            fsg_text(x, y + 186, FSG_T_SMALL, FSG_AMBER, FSG_BG, "PRESS SET");
        else if (w->applied)
            fsg_text(x, y + 186, FSG_T_SMALL, FSG_GREEN, FSG_BG, "IN CAMERA");
    }

    footer("ARROWS move    SET write to camera    Q zero    MENU back");
}

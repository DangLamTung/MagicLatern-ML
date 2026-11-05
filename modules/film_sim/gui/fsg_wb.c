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
static int cell_colour(int ba, int gm)
{
    int r = 128 + ba * 7;          /* amber up, blue down */
    int g = 128 + gm * 6;          /* green up, magenta down */
    int b = 128 - ba * 7;
    int dark = (ba * ba + gm * gm) / 6;

    if (r < 40) r = 40;
    if (r > 235) r = 235;
    if (g < 40) g = 40;
    if (g > 235) g = 235;
    if (b < 40) b = 40;
    if (b > 235) b = 235;
    /* the rim reads brighter so the grid has an edge without a border fill */
    if (dark > 18) dark = 18;
    r = r * (30 - dark) / 30;
    g = g * (30 - dark) / 30;
    b = b * (30 - dark) / 30;
    return (r << 16) | (g << 8) | b;
}

static void header(const char *right)
{
    fsg_fill(0, 0, FSG_W, 44, FSG_RED_DEEP);
    fsg_fill(0, 42, FSG_W, 2, FSG_RED_DARK);
    fsg_fill(0, 0, 6, 44, FSG_AMBER);
    fsg_text(20, 9, FSG_T_MED, FSG_AMBER, FSG_RED_DEEP, "FUJI WB SHIFT");
    fsg_text(20, 30, FSG_T_SMALL, 0xE8C9A0, FSG_RED_DEEP,
             "WRITTEN TO THE CAMERA'S OWN WHITE BALANCE");
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
        int px = gx + (w->ba + 9) * CELL;
        int py = gy + (9 - w->gm) * CELL;
        int c = w->applied ? FSG_GREEN : FSG_AMBER;

        fsg_fill(px - 1, py - 1, CELL + 1, CELL + 1, c);
        fsg_fill(px + 1, py + 1, CELL - 3, CELL - 3, FSG_BG_DEEP);
        fsg_fill(px + 5, py + 5, 5, 5, c);
    }

    /* axis labels, outside the grid */
    fsg_text(gx, gy - 22, FSG_T_SMALL, FSG_GREY, FSG_BG, "GREEN");
    fsg_text(gx + GRID_PX - 54, gy - 22, FSG_T_SMALL, FSG_GREY, FSG_BG, "MAGENTA");
    fsg_text(gx - 66, gy + GRID_PX / 2 - 6, FSG_T_SMALL, FSG_GREY, FSG_BG, "BLUE");
    fsg_text(gx + GRID_PX + 8, gy + GRID_PX / 2 - 6, FSG_T_SMALL, FSG_GREY, FSG_BG,
             "AMBER");

    /* readouts */
    {
        const int x = gx + GRID_PX + 92;
        int y = gy + 8;

        fsg_text(x, y, FSG_T_SMALL, FSG_GREY, FSG_BG, "B / A");
        snprintf(buf, sizeof buf, "%+d", w->ba);
        fsg_text(x, y + 20, FSG_T_LARGE, FSG_AMBER, FSG_BG, "%s", buf);

        fsg_text(x, y + 70, FSG_T_SMALL, FSG_GREY, FSG_BG, "G / M");
        snprintf(buf, sizeof buf, "%+d", w->gm);
        fsg_text(x, y + 90, FSG_T_LARGE, FSG_AMBER, FSG_BG, "%s", buf);

        /* 5 mireks a unit, the number ML's own menu quotes */
        snprintf(buf, sizeof buf, "%+d mired", w->ba * 5);
        fsg_text(x, y + 140, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG, "%s", buf);

        if (w->moved && !w->applied)
            fsg_text(x, y + 186, FSG_T_SMALL, FSG_AMBER, FSG_BG, "PRESS SET");
        else if (w->applied)
            fsg_text(x, y + 186, FSG_T_SMALL, FSG_GREEN, FSG_BG, "IN CAMERA");
    }

    footer("ARROWS move    SET write to camera    Q zero    MENU back");
}

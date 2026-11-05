/*
 * The 12-colour creator, drawn.
 *
 * Only fsg_fill() and fsg_text(): no lines, no circles, no alpha. So the
 * wheel is a ring of square beads and a disc rasterised row by row, and the
 * "dotted" guide ray is a run of small squares. That is a constraint of the
 * panel, not a shortcut.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fsg.h"
#include "fsg_creator.h"

/* ---- colours ---------------------------------------------------------- */

/* radial steps per wedge: one palette entry each, see register_palette */
#define CREATOR_BANDS   4

static const char *const wedge_name[FS_CREATOR_WEDGES] = {
    "RED", "ORANGE", "YELLOW", "LIME", "GREEN", "SPRING",
    "CYAN", "AZURE", "BLUE", "VIOLET", "MAGENTA", "ROSE",
};

const char *fs_creator_wedge_name(int i)
{
    return wedge_name[((i % FS_CREATOR_WEDGES) + FS_CREATOR_WEDGES) % FS_CREATOR_WEDGES];
}

/*
 * Nominal colour of a wedge, as a hue sweep at a fixed S and V so the twelve
 * read as one family. Integer only: the camera has no libm in this path.
 *
 * These are for the BEADS. They are not what the stock does to a colour --
 * the engine decides that -- they only say which wedge a number belongs to.
 */
int fs_creator_wedge_rgb(int i)
{
    /* hue in sixths of a turn, 0..3599; wedge i sits at i * 300 */
    int h = ((i % FS_CREATOR_WEDGES) + FS_CREATOR_WEDGES) % FS_CREATOR_WEDGES;
    int seg = (h * 300) / 600;          /* which 60-degree sector */
    int f   = (h * 300) % 600;          /* 0..599 within it       */
    int up  = f * 255 / 600;
    int dn  = 255 - up;
    /* S = 0.72, V = 1.0: vivid enough to tell apart, dull enough not to
     * look like a warning light on a camera screen. */
    int v   = 235;
    int lo  = v - (v * 72) / 100;
    int r, g, b;

    switch (seg) {
    case 0:  r = v;  g = lo + (v - lo) * up / 255; b = lo; break;
    case 1:  r = v - (v - lo) * up / 255; g = v;   b = lo; break;
    case 2:  r = lo; g = v;   b = lo + (v - lo) * up / 255; break;
    case 3:  r = lo; g = v - (v - lo) * up / 255; b = v;   break;
    case 4:  r = lo + (v - lo) * up / 255; g = lo; b = v;  break;
    default: r = v;  g = lo;  b = v - (v - lo) * up / 255; break;
    }
    (void)dn;
    return (r << 16) | (g << 8) | b;
}

/*
 * The wheel is filled, so one colour per wedge is not enough: each wedge is
 * drawn as four concentric bands and the bands need their own entries. The
 * overlay palette has 175 (FSG_PAL_N, entries 80..254) and only the first
 * sixteen are ever borrowed by another screen, so 48 is safe -- nothing else
 * references slots 16..47.
 *
 * The steps are eighths, not a real blend: the panel indexes a palette, there
 * is no interpolation, so a "gradient" is only ever this many flat steps.
 */
static const int wedge_shade[CREATOR_BANDS] = { 72, 128, 190, 255 };

void fs_creator_register_palette(void)
{
    int pal[FS_CREATOR_WEDGES * CREATOR_BANDS], i, b;
    for (i = 0; i < FS_CREATOR_WEDGES; i++) {
        int rgb = fs_creator_wedge_rgb(i);
        for (b = 0; b < CREATOR_BANDS; b++) {
            int k = wedge_shade[b], r = (rgb >> 16) & 255;
            int g = (rgb >> 8) & 255, bl = rgb & 255;
            pal[i * CREATOR_BANDS + b] =
                ((r * k / 255) << 16) | ((g * k / 255) << 8) | (bl * k / 255);
        }
    }
    fsg_pal(pal, FS_CREATOR_WEDGES * CREATOR_BANDS);
}

static const int sin30[12] = { 0, 500, 866, 1000, 866, 500,
                               0, -500, -866, -1000, -866, -500 };

/* ---- small drawing helpers over fills -------------------------------- */

static void line_fill(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        fsg_fill(x0, y0, 1, 1, c);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

/* sin/cos for a tenth of a degree, from the 30-degree table, in 0.001 units.
 * Index 0 is +x, so angle 900 is straight up. */
static int isqrt_i(int v);

static void sincos10(int ang, int *s10, int *c10)
{
    int idx, f, a, b, id2;
    if (ang < 0) ang += 3600;
    ang %= 3600;
    idx = (ang / 300) % 12;
    f = ang % 300;
    a = sin30[idx]; b = sin30[(idx + 1) % 12];
    *s10 = a + (b - a) * f / 300;
    id2 = (idx + 3) % 12;
    a = sin30[id2]; b = sin30[(id2 + 1) % 12];
    *c10 = a + (b - a) * f / 300;

    /*
     * Interpolating (sin, cos) as a straight line between two 30-degree knots
     * gives the right DIRECTION but a vector only cos(15) = 0.966 long, so
     * every point between knots was drawn 3.4% too close to the centre: the
     * filled sectors ended short of the perimeter they were meant to meet,
     * leaving a dark ring of slivers at every corner. Scale the vector back to
     * unit length -- the divisions are only paid for the interpolated angles,
     * the twelve knots return early.
     */
    if (f) {
        int mag2 = *s10 * *s10 + *c10 * *c10;
        if (mag2 > 0) {
            int mag = isqrt_i(mag2);
            if (mag > 0) {
                *s10 = *s10 * 1000 / mag;
                *c10 = *c10 * 1000 / mag;
            }
        }
    }
}

static void dodec_point(int cx, int cy, int r, int ang10, int *ox, int *oy)
{
    int s10, c10;
    sincos10(ang10, &s10, &c10);
    *ox = cx + (r * c10) / 1000;
    *oy = cy - (r * s10) / 1000;
}

static int isqrt_i(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

/* A filled circle, one row per fill. Used for the beads: 1-px outlines on a
 * dark disc read as a wireframe diagram, which is what the first version
 * looked like. */
static void circle_fill(int cx, int cy, int r, int colour)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int half = isqrt_i(r * r - dy * dy);
        fsg_fill(cx - half, cy + dy, half * 2 + 1, 1, colour);
    }
}

static void rect_outline(int x, int y, int w, int h, int c)
{
    fsg_fill(x, y, w, 1, c);
    fsg_fill(x, y + h - 1, w, 1, c);
    fsg_fill(x, y, 1, h, c);
    fsg_fill(x + w - 1, y, 1, h, c);
}

/* A filled disc, one row per fill. 256 fills: fine for a screen that is
 * redrawn on a key press, not per frame. */
static int isqrt(int v)
{
    int r = 0;
    /* incremental: at most as many steps as the root, and r <= 255 here */
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

static void disc(int cx, int cy, int r, int colour)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int half = isqrt(r * r - dy * dy);
        fsg_fill(cx - half, cy + dy, half * 2 + 1, 1, colour);
    }
}

static void bead(int cx, int cy, int size, int colour)
{
    fsg_fill(cx - size / 2, cy - size / 2, size, size, colour);
}

/* sin/cos in degrees for the 12 positions, tenths of a unit. A table, so the
 * wheel geometry is exact and needs no libm on the camera. */
/* the ring's geometry, in one place: the static screen and the spin redraw
 * must agree or a bead would be erased where it was not drawn */
#define CREATOR_PANEL_X 392   /* the wheel's box ends here */
#define CREATOR_CX      196
#define CREATOR_CY      250
#define CREATOR_R_OUT   150
#define CREATOR_R_IN    58
#define CREATOR_BEAD_R  14      /* half the largest bead box                */
#define CREATOR_DISC_BG FSG_PANEL_DARK

static void ring_pos(int cx, int cy, int r, int i, int *ox, int *oy)
{
    int s = sin30[((i % 12) + 12) % 12];
    int c = sin30[((i + 3) % 12 + 12) % 12];
    *ox = cx + (r * c) / 1000;
    *oy = cy - (r * s) / 1000;   /* screen y grows downward */
}

static void bar(int x, int y, int w, int h, int value, int lo, int hi,
                int fill, int active)
{
    int span = hi - lo;
    int pos  = (span > 0) ? ((value - lo) * (w - 2)) / span : 0;
    if (pos < 0) pos = 0;
    if (pos > w - 2) pos = w - 2;

    fsg_fill(x, y, w, h, FSG_PANEL_DARK);
    fsg_fill(x + 1, y + 1, pos, h - 2, fill);
    rect_outline(x, y, w, h, active ? FSG_AMBER : FSG_LINE_DIM);
    /* centre tick: where "unchanged" sits */
    {
        int mid = (0 - lo) * (w - 2) / (span ? span : 1) + 1;
        fsg_fill(x + mid, y + 1, 1, h - 2, active ? FSG_AMBER_DIM : FSG_LINE);
    }
}

/* ---- header / footer -------------------------------------------------- */

static void header(const char *stock, const char *right, int tick)
{
    int sheen = FSG_RED_DEEP + (tick & 7);
    fsg_fill(0, 0, FSG_W, 44, FSG_RED_DEEP);
    fsg_fill(0, 42, FSG_W, 2, FSG_RED_DARK);
    fsg_fill(0, 0, 6, 44, FSG_AMBER);
    fsg_text(20, 9, FSG_T_MED, FSG_AMBER, FSG_RED_DEEP, "COLOR CREATOR");
    if (stock && stock[0])
        fsg_text(20, 30, FSG_T_SMALL, 0xE8C9A0, FSG_RED_DEEP, "%s", stock);
    fsg_text(600, 15, FSG_T_MED, FSG_WHITE, FSG_RED_DEEP, "%s", right);
    (void)sheen;
}

static void footer(const char *keys)
{
    fsg_fill(0, FSG_H - 44, FSG_W, 44, FSG_BG_DEEP);
    fsg_fill(0, FSG_H - 44, FSG_W, 1, FSG_LINE);
    fsg_text(16, FSG_H - 33, FSG_T_SMALL, FSG_GREY, FSG_BG_DEEP, "%s", keys);
}

const char *const fs_creator_tabs[3] = { "Saturation", "Luminance", "Hue" };
const int fs_creator_tab_field[3] = { 0, 2, 1 };

/*
 * Filled sectors: the radar is a colour wheel, not twelve dots. A scanline
 * fill is one fsg_fill per row per polygon, which is the cheapest way to put
 * ~39k coloured pixels on the panel without a per-pixel call.
 */
#define CREATOR_SLOT(i, b) FSG_PIX((i) * CREATOR_BANDS + (b))

static void poly_fill(const int *xs, const int *ys, int n, int colour)
{
    int i, j, y, ymin = ys[0], ymax = ys[0];

    for (i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    for (y = ymin; y <= ymax; y++) {
        int hit[8], nh = 0;
        for (i = 0, j = n - 1; i < n; j = i++) {
            if ((ys[i] > y) != (ys[j] > y)) {
                int dy = ys[i] - ys[j];
                if (nh < 8)
                    hit[nh++] = xs[j] +
                        (int)((long long)(y - ys[j]) * (xs[i] - xs[j]) / dy);
            }
        }
        for (i = 0; i < nh; i++)
            for (j = i + 1; j < nh; j++)
                if (hit[j] < hit[i]) { int t = hit[i]; hit[i] = hit[j]; hit[j] = t; }
        for (i = 0; i + 1 < nh; i += 2)
            fsg_fill(hit[i], y, hit[i + 1] - hit[i] + 1, 1, colour);
    }
}

/* one radial band of one wedge, as a quadrilateral */
static void sector_band(int cx, int cy, int r0, int r1, int a1, int a2, int colour)
{
    int xs[4], ys[4];
    dodec_point(cx, cy, r0, a1, &xs[0], &ys[0]);
    dodec_point(cx, cy, r1, a1, &xs[1], &ys[1]);
    dodec_point(cx, cy, r1, a2, &xs[2], &ys[2]);
    dodec_point(cx, cy, r0, a2, &xs[3], &ys[3]);
    poly_fill(xs, ys, 4, colour);
}

/* ---- the wheel -------------------------------------------------------- */

/* One wedge's bead. `sel` draws the halo and the inner disc. */
/*
 * The wheel as the reference UI draws it: a dodecagon whose vertex for each
 * wedge moves in or out with that wedge's value, a coloured perimeter showing
 * which colour each wedge is, and the twelve numbers around the rim. The whole
 * figure rotates with `offset_q16`, so the dial turns the shape.
 *
 * Values map to radius with the neutral mid-point at R_MID: 100 for saturation
 * and luminance, 0 for rotation, so "untouched" is a clean regular dodecagon.
 */
#define CREATOR_R_MID   90
#define CREATOR_R_SPAN  32  /* +-32 px over the full value range */
#define CREATOR_R_MAX   126    /* the coloured perimeter */

static int wedge_radius(const fs_creator_t *c, int i, int tab)
{
  int v   = (tab == 0) ? c->sat[i] : (tab == 1 ? c->rot[i] : c->lum[i]);
  int mid  = (tab == 1) ? 0 : 100;
  int span = (tab == 1) ? 180 : 100;
  int d = v - mid;
  if (d > span) d = span;
  if (d < -span) d = -span;
  return CREATOR_R_MID + (d * CREATOR_R_SPAN) / span;
}

/* angle of wedge i, in tenths of a degree, with index 0 at the top */
static int wedge_angle(int i, int offset_q16)
{
  int off10 = (int)(((long long)offset_q16 * 300) / 65536);
  return ((i * 300 - off10 + 900) % 3600 + 3600) % 3600;
}

void fsg_draw_creator_wheel(const fs_creator_t *c, int offset_q16)
{
  const int cx = CREATOR_CX, cy = CREATOR_CY;
  int i, px[FS_CREATOR_WEDGES], py[FS_CREATOR_WEDGES];
  char num[8];

  fs_creator_register_palette();

  /* clear the wheel's box only: the right-hand panel is untouched and stays
   * in the idle buffer, which is what makes a spin frame cheap */
  fsg_fill(0, FSG_CREATOR_RING_Y0, CREATOR_PANEL_X,
           FSG_CREATOR_RING_Y1 - FSG_CREATOR_RING_Y0, FSG_BG);

  disc(cx, cy, CREATOR_R_OUT, FSG_PANEL_DARK);

  /* the colour field: twelve wedges, each in four flat steps from the hub out
   * to the perimeter, so the disc reads as a wheel of colour the way the
   * reference does -- the value polygon is drawn over it */
  {
    /* the field ends ON the perimeter, not past it: colour outside the rim
     * line left ragged slivers at every dodecagon corner */
    int b, r0 = CREATOR_R_IN + 2, r1 = CREATOR_R_MAX;
    int step = (r1 - r0) / CREATOR_BANDS;
    for (b = 0; b < CREATOR_BANDS; b++) {
      int lo = r0 + b * step, hi = (b == CREATOR_BANDS - 1) ? r1 : lo + step;
      for (i = 0; i < FS_CREATOR_WEDGES; i++) {
        int a = wedge_angle(i, offset_q16);
        sector_band(cx, cy, lo, hi, a - 150, a + 150, CREATOR_SLOT(i, b));
      }
    }
  }
  disc(cx, cy, CREATOR_R_IN, FSG_BG_DEEP);

  for (i = 0; i < FS_CREATOR_WEDGES; i++)
    dodec_point(cx, cy, wedge_radius(c, i, c->field),
                wedge_angle(i, offset_q16), &px[i], &py[i]);

  /* the coloured perimeter, drawn on the fill's OWN outer chord. It used to
   * join the wedge centres, which is 15 degrees off the corners the fill ends
   * at -- two dodecagons a half step apart, and slivers of dark between them. */
  for (i = 0; i < FS_CREATOR_WEDGES; i++) {
    int x1, y1, x2, y2, a = wedge_angle(i, offset_q16);
    dodec_point(cx, cy, CREATOR_R_MAX, a - 150, &x1, &y1);
    dodec_point(cx, cy, CREATOR_R_MAX, a + 150, &x2, &y2);
    line_fill(x1, y1, x2, y2, CREATOR_SLOT(i, CREATOR_BANDS - 1));
    line_fill(x1, y1 + 1, x2, y2 + 1, CREATOR_SLOT(i, CREATOR_BANDS - 1));
  }

  /* the value polygon, then its dots: a dot needs a dark skirt or one that
   * lands on the perimeter -- same colour, same rows -- disappears into it */
  for (i = 0; i < FS_CREATOR_WEDGES; i++) {
    int j = (i + 1) % FS_CREATOR_WEDGES;
    /* a dark shadow under a white edge: white on the bright outer band is a
     * one-pixel line of near-equal luminance and disappears at arm's length */
    line_fill(px[i], py[i] + 1, px[j], py[j] + 1, FSG_BG_DEEP);
    line_fill(px[i], py[i], px[j], py[j], FSG_WHITE);
  }
  for (i = 0; i < FS_CREATOR_WEDGES; i++) {
    /* a ring with a dark centre, so the node still reads where it sits on the
     * brightest band of its own colour */
    circle_fill(px[i], py[i], i == c->sel ? 9 : 6,
                i == c->sel ? FSG_AMBER : CREATOR_SLOT(i, CREATOR_BANDS - 1));
    circle_fill(px[i], py[i], i == c->sel ? 5 : 3,
                i == c->sel ? FSG_AMBER : FSG_BG_DEEP);
  }

  /* the twelve numbers, outside the perimeter */
  for (i = 0; i < FS_CREATOR_WEDGES; i++) {
    int x, y;
    dodec_point(cx, cy, CREATOR_R_MAX + 21, wedge_angle(i, offset_q16), &x, &y);
    snprintf(num, sizeof num, "%d", i);
    fsg_text(x - 3, y - 5, FSG_T_SMALL,
             i == c->sel ? FSG_AMBER : FSG_GREY, FSG_PANEL_DARK, "%s", num);
  }

  /* the hub: which wedge is selected and its value in the active series */
  fsg_text(cx - 40, cy - 18, FSG_T_MED, FSG_WHITE, FSG_BG_DEEP, "%s",
           fs_creator_wedge_name(c->sel));
  {
    int v = (c->field == 0) ? c->sat[c->sel]
          : (c->field == 1) ? c->rot[c->sel] : c->lum[c->sel];
    snprintf(num, sizeof num, (c->field == 1) ? "%+d" : "%d", v);
    fsg_text(cx - 30, cy + 2, FSG_T_LARGE, FSG_AMBER, FSG_BG_DEEP, "%s", num);
  }

  /* a needle from the hub to the selected vertex. Without it a value change
   * moves one dot a couple of pixels, which is what "the point in the colour
   * wheel does not change" was: the radar was alive, the feedback was too
   * small to see. */
  {
    int nx, ny, ax, ay;
    int a = wedge_angle(c->sel, offset_q16);
    dodec_point(cx, cy, CREATOR_R_IN + 2, a, &ax, &ay);
    dodec_point(cx, cy, wedge_radius(c, c->sel, c->field), a, &nx, &ny);
    line_fill(ax, ay, nx, ny, FSG_AMBER);
    line_fill(ax, ay + 1, nx, ny + 1, FSG_AMBER);
  }

  /* the pointer the shape turns past: inside the perimeter, because the
   * selected wedge is always the one at twelve o'clock and its number sits
   * outside the perimeter -- a pointer out there hides the number being edited */
  {
    int y0 = cy - CREATOR_R_MAX + 4;
    fsg_fill(cx - 7, y0 + 6, 15, 3, FSG_AMBER);
    fsg_fill(cx - 5, y0 + 3, 11, 3, FSG_AMBER);
    fsg_fill(cx - 3, y0, 7, 3, FSG_AMBER);
  }
}

/*
 * The rotation entry point. The old bead ring kept per-bead positions in
 * `sp` so it could erase just those; the radar has no such trick -- a new
 * angle moves every vertex, edge and number -- so it repaints the wheel box
 * and the caller blits those rows. `sp` is accepted and ignored so the
 * animation code in film_sim.c keeps one call site for both screens.
 */
void fsg_draw_creator_beads(const fs_creator_t *c, int offset_q16,
                            fs_creator_spin_t *sp)
{
  (void)sp;
  fsg_draw_creator_wheel(c, offset_q16);
}

void fsg_draw_creator(const fs_creator_t *c)
{
    char right[24];
    char value[32];
    int i;

    fsg_fill(0, 0, FSG_W, FSG_H, FSG_BG);
    fsg_fill(0, 46, FSG_W, FSG_H - 90, FSG_BG);

    snprintf(right, sizeof right, "%d / %d", c->sel + 1, FS_CREATOR_WEDGES);
    header(c->stock, right, c->sel);

    /* the tabs use the fixed constants; the wheel registers the wedge palette
     * itself, in fsg_draw_creator_wheel(), and that is its only owner */

    /* the three series tabs */
    {
        int t, tx = 16;
        for (t = 0; t < 3; t++) {
            int w = 118;
            int on = (c->field == fs_creator_tab_field[t]);
            fsg_fill(tx, 48, w, 26, on ? FSG_RED_MID : FSG_PANEL_DARK);
            fsg_fill(tx, 48, w, 2, on ? FSG_AMBER : FSG_LINE_DIM);
            fsg_text(tx + 10, 54, FSG_T_SMALL, on ? FSG_AMBER : FSG_GREY,
                     on ? FSG_RED_MID : FSG_PANEL_DARK, "%s", fs_creator_tabs[t]);
            tx += w + 6;
        }
    }

    /* the dodecagon radar is drawn by the shared wheel renderer */
    fsg_draw_creator_wheel(c, c->sel * 65536);

    /* ---- the three dials, right-hand panel ---- */
    {
        const int x = 396, w = 296;
        int y = 82;
        static const char *const label[3] = { "SATURATION", "ROTATE", "LUMINANCE" };
        static const int lo[3] = { 0, -180, 0 };
        static const int hi[3] = { 200, 180, 200 };
        fsg_text(x, y - 34, FSG_T_LARGE, FSG_AMBER, FSG_BG, "%s",
                 fs_creator_wedge_name(c->sel));
        for (i = 0; i < 3; i++) {
            int v = (i == 0) ? c->sat[c->sel] : (i == 1 ? c->rot[c->sel] : c->lum[c->sel]);
            fsg_text(x, y, FSG_T_SMALL, i == c->field ? FSG_AMBER : FSG_GREY,
                     FSG_BG, "%s", label[i]);
            snprintf(value, sizeof value, (i == 1) ? "%+d" : "%d", v);
            fsg_text(x + w - 44, y, FSG_T_SMALL, FSG_WHITE, FSG_BG, "%s", value);
            bar(x, y + 18, w, 14, v, lo[i], hi[i],
                i == c->field ? FSG_AMBER : FSG_RED_MID, i == c->field);
            y += 58;
        }

        /* how many wedges have been moved: the one number that says whether
         * this is still a stock or a look of its own */
        {
            int moved = 0;
            for (i = 0; i < FS_CREATOR_WEDGES; i++)
                if (c->sat[i] != 100 || c->rot[i] != 0 || c->lum[i] != 100)
                    moved++;
            fsg_text(x, y + 6, FSG_T_SMALL, FSG_GREY, FSG_BG,
                     "WEDGES MOVED");
            snprintf(value, sizeof value, "%d / %d", moved, FS_CREATOR_WEDGES);
            fsg_text(x + w - 60, y + 6, FSG_T_SMALL,
                     moved ? FSG_WHITE : FSG_GREEN, FSG_BG, "%s", value);
        }
        if (c->saved) {
            char line[48];
            snprintf(line, sizeof line, "SAVED ML/FILM/%s.fs",
                     c->saved_name ? c->saved_name : "WHEEL");
            fsg_fill(x, y + 30, w, 20, FSG_GREEN_DARK);
            fsg_text(x + 8, y + 36, FSG_T_SMALL, FSG_GREEN, FSG_GREEN_DARK,
                     "%s", line);
        } else if (c->dirty) {
            fsg_text(x, y + 34, FSG_T_SMALL, FSG_AMBER, FSG_BG, "UNSAVED");
        }
        /* what PLAY will write, always, so the slot is never a surprise */
        if (!c->saved) {
            char line[48];
            snprintf(line, sizeof line, "PLAY writes %s.fs",
                     c->slot_name ? c->slot_name : "WHEEL");
            fsg_text(x, y + 58, FSG_T_SMALL, FSG_GREY, FSG_BG, "%s", line);
        }
    }

    footer("DIAL wedge  UP/DOWN value  SET series  PLAY save  Q tone  MENU back");
}

/* ---- the tone screen -------------------------------------------------- */

void fsg_draw_tone(const fs_creator_t *c)
{
    const int gx = 96, gy = 108, gw = 400, gh = 232;
    char value[32];
    int i;

    fsg_fill(0, 0, FSG_W, FSG_H, FSG_BG);
    header(c->stock, "TONE", c->shadow + c->highlight + 8);

    /* grid */
    for (i = 1; i < 4; i++) {
        fsg_fill(gx + gw * i / 4, gy, 1, gh, FSG_LINE_DIM);
        fsg_fill(gx, gy + gh * i / 4, gw, 1, FSG_LINE_DIM);
    }
    rect_outline(gx, gy, gw, gh, FSG_LINE);

    /*
     * The curve, as a run of short vertical fills: identity is the diagonal,
     * and each handle bends one end of it. Drawn from the same numbers the
     * recipe writes, so the shape on screen is the shape the engine gets.
     */
    {
        int x;
        for (x = 0; x < gw; x++) {
            int in  = x * 255 / gw;
            int out = in;
            /*
             * SHADOW + lifts the blacks, HIGHLIGHT + compresses (darkens) the
             * highlights -- the same sense as the engine's `black` and
             * `shoulder`, so the curve on this screen is the curve the recipe
             * will produce.
             *
             * The scale is 1 meaning 1 output level at full weight, so the
             * +/-60 range can move an end by up to 60 levels and no more. The
             * first version divided by 900 AND subtracted the highlight term,
             * which made a negative highlight BRIGHTEN the top end and pinned
             * the curve to the ceiling -- visible on the first render.
             */
            int t   = (in * 100) / 255;
            out += (c->shadow    * (100 - t) * (100 - t)) / 10000;
            out -= (c->highlight * t * t)             / 10000;
            if (out < 0) out = 0;
            if (out > 255) out = 255;
            fsg_fill(gx + x, gy + gh - 1 - (out * (gh - 2)) / 255, 1, 2,
                     FSG_AMBER);
        }
        /* identity reference */
        for (x = 0; x < gw; x += 6)
            fsg_fill(gx + x, gy + gh - 1 - (x * (gh - 2)) / gw, 1, 1, FSG_LINE);
    }

    fsg_text(gx, gy - 26, FSG_T_MED, FSG_WHITE, FSG_BG, "HIGHLIGHT / SHADOW");
    /* The active field is the bright one, like the wheel screen: two
     * readouts both drawn amber left no way to know which number UP/DOWN
     * was about to move. */
    fsg_text(gx + gw + 24, gy, FSG_T_SMALL, FSG_GREY, FSG_BG, "SHADOW");
    snprintf(value, sizeof value, "%+d", c->shadow);
    fsg_text(gx + gw + 24, gy + 18, FSG_T_LARGE,
             c->field == 0 ? FSG_AMBER : FSG_GREY_DIM, FSG_BG, "%s", value);
    fsg_text(gx + gw + 24, gy + 70, FSG_T_SMALL, FSG_GREY, FSG_BG, "HIGHLIGHT");
    snprintf(value, sizeof value, "%+d", c->highlight);
    fsg_text(gx + gw + 24, gy + 88, FSG_T_LARGE,
             c->field == 1 ? FSG_AMBER : FSG_GREY_DIM, FSG_BG, "%s", value);
    if (c->dirty)
        fsg_text(gx + gw + 24, gy + 150, FSG_T_SMALL, FSG_AMBER, FSG_BG, "UNSAVED");

    footer("UP/DOWN value    SET field    PLAY save    Q wheel    MENU back");
}

/* ---- keys ------------------------------------------------------------- */

void fs_creator_defaults(fs_creator_t *c, const char *stock)
{
    int i;
    memset(c, 0, sizeof *c);
    for (i = 0; i < FS_CREATOR_WEDGES; i++) {
        c->sat[i] = 100;
        c->rot[i] = 0;
        c->lum[i] = 100;
    }
    c->screen = 0;
    c->slot_name = "WHEEL";
    c->stock = stock;
}

/*
 * Takes the module's own action codes (see fs_action_of_key in film_sim.c),
 * not raw key codes, so the same handler works for the preview and the body.
 */
enum { A_LEFT = 1, A_RIGHT, A_UP, A_DOWN, A_SET, A_Q };

void fs_creator_step(fs_creator_t *c, int act)
{
    switch (act) {
    case A_LEFT:
        if (c->screen == 0)
            c->sel = (c->sel + FS_CREATOR_WEDGES - 1) % FS_CREATOR_WEDGES;
        break;
    case A_RIGHT:
        if (c->screen == 0)
            c->sel = (c->sel + 1) % FS_CREATOR_WEDGES;
        break;
    case A_UP:
    case A_DOWN:
        if (c->screen == 0) {
            int step = (act == A_UP) ? 1 : -1;
            if (c->field == 0) {
                c->sat[c->sel] += step * 10;
                if (c->sat[c->sel] < 0) c->sat[c->sel] = 0;
                if (c->sat[c->sel] > 200) c->sat[c->sel] = 200;
            } else if (c->field == 1) {
                c->rot[c->sel] += step * 15;
                if (c->rot[c->sel] < -180) c->rot[c->sel] = -180;
                if (c->rot[c->sel] > 180) c->rot[c->sel] = 180;
            } else {
                c->lum[c->sel] += step * 10;
                if (c->lum[c->sel] < 0) c->lum[c->sel] = 0;
                if (c->lum[c->sel] > 200) c->lum[c->sel] = 200;
            }
        } else {
            if (c->field == 0) {
                c->shadow += (act == A_UP) ? 1 : -1;
                if (c->shadow < -60) c->shadow = -60;
                if (c->shadow > 60) c->shadow = 60;
            } else {
                c->highlight += (act == A_UP) ? 1 : -1;
                if (c->highlight < -60) c->highlight = -60;
                if (c->highlight > 60) c->highlight = 60;
            }
        }
        c->dirty = 1;
        c->saved = 0;
        break;
    case A_SET:
        if (c->screen == 0) {
            int t;
            for (t = 0; t < 3; t++)
                if (fs_creator_tab_field[t] == c->field) break;
            c->field = fs_creator_tab_field[(t + 1) % 3];
        } else {
            c->field = (c->field + 1) % 2;
        }
        break;
    case A_Q:
        c->screen ^= 1;
        c->field = 0;
        break;
    default:
        break;
    }
}

/* ---- writing a recipe ------------------------------------------------ */

/* out level for an in level, the same arithmetic the tone screen draws */
static int tone_out(const fs_creator_t *c, int in)
{
    int t = in * 100 / 255;
    int out = in + (c->shadow * (100 - t) * (100 - t)) / 10000
                - (c->highlight * t * t)              / 10000;
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return out;
}

static int append(char *buf, int cap, int n, const char *fmt, ...)
{
    va_list ap;
    int wrote;
    if (n >= cap) return -1;
    va_start(ap, fmt);
    wrote = vsnprintf(buf + n, (size_t)(cap - n), fmt, ap);
    va_end(ap);
    if (wrote < 0 || n + wrote >= cap) return -1;
    return n + wrote;
}

int fs_creator_write(const fs_creator_t *c, char *buf, int cap, const char *name)
{
    static const int curve_in[5] = { 0, 64, 128, 192, 255 };
    int n = 0, i;
    char vec[160];
    int k;

#define PUT(...) do { n = append(buf, cap, n, __VA_ARGS__); if (n < 0) return -1; } while (0)

    PUT("VERSION   3\n");
    PUT("NAME      %s\n", name && name[0] ? name : "CUSTOM");
    PUT("DESC      MADE IN COLOR CREATOR\n");
    PUT("WB        AS_SHOT\n");

    /* tone: five points, the ends at the extremes */
    n = append(buf, cap, n, "CURVE     ");
    if (n < 0) return -1;
    for (k = 0; k < 5; k++)
        PUT("%s%d,%d", k ? " " : "", curve_in[k], tone_out(c, curve_in[k]));
    PUT("\n");
    /* the per-channel curves are identity: all the colour is in HUE_* */
    PUT("CURVE_R   0,0 128,128 255,255\n");
    PUT("CURVE_G   0,0 128,128 255,255\n");
    PUT("CURVE_B   0,0 128,128 255,255\n");

    /* the twelve wedges, as twelve values -- not six, so a bead every 30
     * degrees keeps its own knot */
    for (k = 0; k < 3; k++) {
        const char *key = (k == 0) ? "HUE_SAT" : (k == 1 ? "HUE_ROT" : "HUE_LUM");
        int len = 0;
        for (i = 0; i < FS_CREATOR_WEDGES; i++) {
            int v = (k == 0) ? c->sat[i] : (k == 1 ? c->rot[i] : c->lum[i]);
            len += snprintf(vec + len, sizeof vec - (size_t)len, "%s%d",
                            i ? "," : "", v);
        }
        PUT("%-9s %s\n", key, vec);
    }

    PUT("GRAIN     8\n");
    PUT("GRAIN_SIZE 10\n");
    PUT("HALATION  45 226 16\n");
#undef PUT
    return n;
}

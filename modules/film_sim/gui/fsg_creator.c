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

/* ---- small drawing helpers over fills -------------------------------- */

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
static const int sin30[12] = { 0, 500, 866, 1000, 866, 500,
                               0, -500, -866, -1000, -866, -500 };

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

/* ---- the wheel -------------------------------------------------------- */

void fsg_draw_creator(const fs_creator_t *c)
{
    const int cx = 196, cy = 250;
    const int r_out = 150, r_bead = 118, r_in = 58;
    char right[24];
    char value[32];
    int i;

    fsg_fill(0, 0, FSG_W, FSG_H, FSG_BG);
    fsg_fill(0, 46, FSG_W, FSG_H - 90, FSG_BG);

    snprintf(right, sizeof right, "%d / %d", c->sel + 1, FS_CREATOR_WEDGES);
    header(c->stock, right, c->sel);

    /* the wheel body, then a dimmer ring so the beads sit on something */
    disc(cx, cy, r_out, FSG_PANEL_DARK);
    disc(cx, cy, r_in - 8, FSG_BG_DEEP);

    /* dotted guide ray from the inner circle out to the selected bead */
    for (i = r_in + 6; i < r_bead - 10; i += 14) {
        int px, py;
        ring_pos(cx, cy, i, c->sel, &px, &py);
        fsg_fill(px - 1, py - 1, 3, 3, FSG_AMBER_DIM);
    }

    for (i = 0; i < FS_CREATOR_WEDGES; i++) {
        int px, py;
        int col = fs_creator_wedge_rgb(i);
        int is_sel = (i == c->sel);
        /* a wedge whose numbers all sit at neutral is drawn hollow */
        int neutral = (c->sat[i] == 100 && c->rot[i] == 0 && c->lum[i] == 100);

        ring_pos(cx, cy, r_bead, i, &px, &py);
        if (is_sel) {
            bead(px, py, 27, FSG_AMBER);
            bead(px, py, 21, FSG_BG_DEEP);
            bead(px, py, 17, col);
        } else if (neutral) {
            /* untouched: hollow, so a screen full of hollow beads reads as
             * "nothing authored yet" at a glance */
            bead(px, py, 19, FSG_BG_DEEP);
            rect_outline(px - 9, py - 9, 19, 19, col);
            rect_outline(px - 8, py - 8, 17, 17, col);
        } else {
            bead(px, py, 19, col);
            rect_outline(px - 10, py - 10, 21, 21, FSG_WHITE);
        }
    }

    /* Only the wedge name in the hub, and only when it fits: fsg_text fills
     * its background, so anything wider than the hole is a black bar across
     * the ring -- which is exactly what the first render did. */
    fsg_text(cx - 30, cy - 8, FSG_T_MED, FSG_WHITE, FSG_BG_DEEP, "%s",
             fs_creator_wedge_name(c->sel));

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
        if (c->dirty)
            fsg_text(x, y + 34, FSG_T_SMALL, FSG_AMBER, FSG_BG, "UNSAVED");
    }

    footer("DIAL wedge  UP/DOWN value  SET field  PLAY save  Q tone  MENU back");
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
                c->sat[c->sel] += step * 4;
                if (c->sat[c->sel] < 0) c->sat[c->sel] = 0;
                if (c->sat[c->sel] > 200) c->sat[c->sel] = 200;
            } else if (c->field == 1) {
                c->rot[c->sel] += step * 4;
                if (c->rot[c->sel] < -180) c->rot[c->sel] = -180;
                if (c->rot[c->sel] > 180) c->rot[c->sel] = 180;
            } else {
                c->lum[c->sel] += step * 4;
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
        break;
    case A_SET:
        c->field = (c->field + 1) % (c->screen == 0 ? 3 : 2);
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

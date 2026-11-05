/*
 * The Fuji-style white balance shift screen: a 19x19 grid, +-9 on each axis.
 *
 * This is the shape Fuji's own WB shift uses -- two axes rather than two
 * sliders -- because the two shifts are one decision. A blue cast and a
 * magenta cast are not independent quantities; the grid says so, and a pair
 * of number boxes does not.
 *
 * The values are pushed into the CAMERA's own white balance (PROP_WBS_BA /
 * PROP_WBS_GM), not into the look, which is what makes this a Fuji recipe
 * rather than a tint.
 */
#ifndef FSG_WB_H
#define FSG_WB_H

typedef struct
{
    /*
     * FUJI'S AXES, not Canon's.
     *
     * A Fuji recipe is written "White Balance: Daylight, +2 Red & -5 Blue",
     * so the numbers a user has in front of them are R and B -- not the
     * Blue/Amber and Green/Magenta that PROP_WBS_BA/GM speak. The screen
     * edits R and B so a published recipe can be typed in as written, and
     * the two are mapped onto the camera's axes on the way out:
     *
     *   Fuji Blue (+ = cooler)  ->  Canon B/A (- = blue)
     *   Fuji Red  (+ = warmer)  ->  Canon G/M (- = magenta)
     *
     * The scale is 1:1 because both are -9..+9. That mapping is a
     * reasonable structural match, NOT a measured equivalence: no Fuji body
     * was available to compare against, so treat a transcribed recipe as
     * approximate.
     */
    int r;           /* -9..+9  Fuji Red axis, the user's number  */
    int b;           /* -9..+9  Fuji Blue axis                      */
    int ba;          /* the same pair on Canon's axes, for display */
    int gm;
    int stock_ba;    /* the base stock's own shift, which this    */
    int stock_gm;    /* one is added to                           */
    int moved;       /* the crosshair has been moved            */
    int applied;     /* the current pair is in the camera        */
    int apply_ms;    /* age of the last APPLY, for the flash-off */
} fs_wb_t;

void fsg_draw_wb(const fs_wb_t *w);

#endif

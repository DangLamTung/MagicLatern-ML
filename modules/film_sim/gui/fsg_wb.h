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
    int ba;          /* -9..+9  offset the user is setting       */
    int gm;
    int stock_ba;    /* the base stock's own shift, which this    */
    int stock_gm;    /* one is added to                           */
    int moved;       /* the crosshair has been moved            */
    int applied;     /* the current pair is in the camera        */
    int apply_ms;    /* age of the last APPLY, for the flash-off */
} fs_wb_t;

void fsg_draw_wb(const fs_wb_t *w);

#endif

/*
 * The 12-colour creator: state and drawing.
 *
 * Twelve wedges, 30 degrees apart, matching the engine's SE_HUE_SECTORS --
 * not a coincidence, that is why the engine was widened. A wheel with a bead
 * every 30 degrees and only six knots would move two neighbours by half as
 * much as the bead being turned.
 *
 * Drawing goes through fsg_fill()/fsg_text() only, like the shelf, so the
 * same code paints the camera's 8-bit palette and the desktop preview.
 */
#ifndef FSG_CREATOR_H
#define FSG_CREATOR_H

#define FS_CREATOR_WEDGES 12

typedef struct
{
    /*
     * Per wedge, in the units the .fs file uses: saturation and luminance as
     * percent (100 = unchanged), rotation in degrees. -5..+5 would be too
     * coarse to author with; these are the same ranges a recipe has.
     */
    int sat[FS_CREATOR_WEDGES];
    int rot[FS_CREATOR_WEDGES];
    int lum[FS_CREATOR_WEDGES];

    int sel;        /* which wedge the dials act on, 0..11               */
    int field;      /* 0 = saturation, 1 = rotation, 2 = luminance       */
    int screen;     /* 0 = colour wheel, 1 = tone                        */
    int dirty;      /* something changed since the last save             */

    /* Tone screen. Signed, in recipe units. */
    int shadow;
    int highlight;

    const char *stock;   /* base stock name, shown in the header          */
} fs_creator_t;

void fs_creator_defaults(fs_creator_t *c, const char *stock);
void fs_creator_step(fs_creator_t *c, int key);   /* returns via .dirty */
void fsg_draw_creator(const fs_creator_t *c);

/*
 * The rotating ring.
 *
 * `offset_q16` is the wheel's position in 2^16 units of ONE wedge, so the
 * selected bead sits under a fixed pointer at the top and the rest of the
 * ring turns past it -- which is what a physical dial does, and what the
 * screen did not: it used to jump a whole 30 degrees per press.
 *
 * Only the beads move, so only the beads are redrawn: the previous centres
 * in `sp` are erased with the disc colour and the new ones drawn. The disc
 * behind them is flat, so that erase is exact rather than approximate, and a
 * frame costs ~12 small fills instead of the 301-fill disc.
 */
typedef struct { int x[FS_CREATOR_WEDGES], y[FS_CREATOR_WEDGES]; int valid; } fs_creator_spin_t;

/* The rows the ring occupies, so a spin frame copies back only those. */
#define FSG_CREATOR_RING_Y0 92
#define FSG_CREATOR_RING_Y1 408

void fsg_draw_creator_beads(const fs_creator_t *c, int offset_q16,
                            fs_creator_spin_t *sp);
void fsg_draw_tone(const fs_creator_t *c);

/*
 * Format the creator's state as a .fs recipe. Returns the length written, or
 * -1 if it did not fit. The tone screen becomes a CURVE, which is how every
 * recipe in films/ expresses tone -- there is no BLACK or SHOULDER key.
 */
int fs_creator_write(const fs_creator_t *c, char *buf, int cap,
                     const char *name);

/*
 * Wedge 0..11 -> its name, and its nominal colour.
 *
 * The colour is a PALETTE ENTRY, not an RGB value: the panel has 175 indices
 * and ml_color() sends anything it does not recognise to pure white, so the
 * first version drew twelve white squares on the camera while the desktop
 * preview showed twelve colours. fs_creator_register_palette() must be called
 * before drawing (fsg_draw_creator does it for you).
 */
const char *fs_creator_wedge_name(int i);
int         fs_creator_wedge_rgb(int i);
void        fs_creator_register_palette(void);

#endif

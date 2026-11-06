/*
 * creator_preview - render the 12-colour creator screens on the desktop.
 *
 * Same contract as preview.c: the drawing code is the camera's (fsg.c and
 * fsg_creator.c, through fsg_fill/fsg_text), the glyphs come from
 * render.py. Writes /tmp/fsg_creator*.ppm + .json.
 *
 *   ./creator_preview            both screens, defaults
 *   ./creator_preview edited     both screens after a few dial turns
 */
#include <stdio.h>
#include <string.h>

#include "gui/fsg.h"
#include "gui/fsg_creator.h"
#include "gui/fsg_wb.h"

extern unsigned char fsg_fb[FSG_H][FSG_W][3];
struct txt_item { int x, y, size, fg, bg; char s[128]; };
extern struct txt_item fsg_txt[512];
extern int fsg_txt_n;
void fsg_reset(void);

static void dump(const char *name)
{
    char path[256];
    FILE *f;

    snprintf(path, sizeof path, "/tmp/fsg_%s.ppm", name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", FSG_W, FSG_H);
    for (int y = 0; y < FSG_H; y++)
        fwrite(fsg_fb[y], 1, (size_t)FSG_W * 3, f);
    fclose(f);

    snprintf(path, sizeof path, "/tmp/fsg_%s.json", name);
    f = fopen(path, "w");
    fprintf(f, "[\n");
    for (int i = 0; i < fsg_txt_n; i++)
        fprintf(f,
                "{\"x\":%d,\"y\":%d,\"size\":%d,\"fg\":%d,\"bg\":%d,\"s\":\"%s\"}%s\n",
                fsg_txt[i].x, fsg_txt[i].y, fsg_txt[i].size, fsg_txt[i].fg,
                fsg_txt[i].bg, fsg_txt[i].s, i + 1 < fsg_txt_n ? "," : "");
    fprintf(f, "]\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    int edited = (argc > 1 && strcmp(argv[1], "edited") == 0);
    fs_creator_t c;

    fsg_reset();
    fs_creator_defaults(&c, "BASE: CLASSIC CHROME");
    dump("creator0");

    fsg_reset();
    fs_creator_defaults(&c, "BASE: CLASSIC CHROME");
    if (edited) {
        /* a look somebody actually authored: blue up, greens down and
         * rotated, spring lifted a little -- the shape of a real recipe */
        c.sel = 8;  c.field = 0;
        for (int i = 0; i < 6; i++) fs_creator_step(&c, 3 /* A_UP */);
        c.sel = 4;
        for (int i = 0; i < 9; i++) fs_creator_step(&c, 4 /* A_DOWN */);
        c.field = 1;
        for (int i = 0; i < 3; i++) fs_creator_step(&c, 3);
        c.field = 2;
        c.sel = 5;
        for (int i = 0; i < 2; i++) fs_creator_step(&c, 3);
        c.sel = 8;
        c.field = 0;
    }
    fsg_draw_creator(&c);
    {
        fs_creator_spin_t sp;
        sp.valid = 0;
        fsg_draw_creator_beads(&c, c.sel * 65536, &sp);
    }
    dump("creator1");

    /* the save confirmation, which the camera draws itself because a
     * NotifyBox is erased by the next repaint */
    c.saved = 1;
    c.saved_name = "WHEEL2";
    c.dirty = 0;
    fsg_reset();
    fsg_draw_creator(&c);
    dump("creator_saved");

    /* what a failed save looks like: drawn by the screen, not a NotifyBox */
    c.saved = 0; c.save_err = 1;
    fsg_reset(); fsg_draw_creator(&c);
    dump("creator_err");
    c.save_err = 0;

    /* the FX screen: what the look asks for */
    c.saved = 0; c.dirty = 1;
    c.screen = 2; c.field = 2;
    c.grain = 22; c.grain_size = 40; c.halation = 45; c.chrome = 2; c.fx_blue = 1;
    fsg_reset();
    fsg_draw_fx(&c);
    dump("creator_fx");

    /*
     * One spin sequence, nothing else drawing beads: a full repaint (valid=0)
     * and then three animation frames, each erasing the one before. Any ghost
     * square here is the erase failing; a second, unrelated bead draw earlier
     * in this file would fake one, which it did.
     */
    fsg_reset();
    fsg_draw_creator(&c);
    {
        fs_creator_spin_t sp;
        int f;
        sp.valid = 0;
        fsg_draw_creator_beads(&c, c.sel * 65536, &sp);
        for (f = 1; f <= 3; f++)
            fsg_draw_creator_beads(&c, c.sel * 65536 - (65536 / 4) * f, &sp);
    }
    dump("creator_spin");

    fsg_reset();
    c.screen = 1;
    c.field = 0;
    if (edited) {
        c.shadow = 12;
        c.highlight = -8;
    }
    fsg_draw_tone(&c);
    dump("creator2");

    /* the Fuji WB shift grid, moved off centre so the crosshair is visible */
    fsg_reset();
    {
        fs_wb_t w;
        /* a stock that already carries a balance, so the three numbers on
         * screen are all different and cannot be confused */
        w.stock_ba = -2;
        w.stock_gm = 1;
        /* a published recipe's numbers: +2 Red, -5 Blue */
        w.r = edited ? 2 : 0;
        w.b = edited ? -5 : 0;
        w.ba = -w.b;      /* Canon B/A = -Fuji Blue */
        w.gm = -w.r;
        w.moved = edited;
        w.applied = 0;
        w.apply_ms = 0;
        fsg_draw_wb(&w);
    }
    dump("wbshift");

    /* neutral: the state a fresh card is in */
    fsg_reset();
    {
        fs_wb_t w = { 0, 0, -2, 1, 0, 0, 0 };
        fsg_draw_wb(&w);
    }
    dump("wbshift0");

    /* applied: the crosshair turns green once the camera has the values */
    fsg_reset();
    {
        fs_wb_t w;
        w.stock_ba = -2; w.stock_gm = 1;
        w.r = 2; w.b = -5; w.ba = 5; w.gm = -2;
        w.moved = 1; w.applied = 1; w.apply_ms = 0;
        fsg_draw_wb(&w);
    }
    dump("wbshift1");

    printf("wrote /tmp/fsg_creator0..2 and /tmp/fsg_wbshift\n");
    return 0;
}

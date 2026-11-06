/*
 * preview.c - render film_sim's screens on the desktop.
 *
 * Writes, for each frame, a .ppm of the fills and a .json of the text runs.
 * render.py composites them into PNG. The drawing code is the camera's.
 */
#include <stdio.h>
#include <string.h>

#include "gui/fsg.h"
#include "gui/fs_spring.h"

extern unsigned char fsg_fb[FSG_H][FSG_W][3];
struct txt_item { int x, y, size, fg, bg; char s[128]; };
extern struct txt_item fsg_txt[512];
extern int fsg_txt_n;
void fsg_reset(void);

static void dump(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "/tmp/fsg_%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", FSG_W, FSG_H);
    for (int y = 0; y < FSG_H; y++)
        fwrite(fsg_fb[y], 1, FSG_W * 3, f);
    fclose(f);

    snprintf(path, sizeof path, "/tmp/fsg_%s.json", name);
    f = fopen(path, "w");
    fprintf(f, "[\n");
    for (int i = 0; i < fsg_txt_n; i++) {
        fprintf(f,
                "{\"x\":%d,\"y\":%d,\"size\":%d,\"fg\":%d,\"bg\":%d,\"s\":\"%s\"}%s\n",
                fsg_txt[i].x, fsg_txt[i].y, fsg_txt[i].size, fsg_txt[i].fg,
                fsg_txt[i].bg, fsg_txt[i].s, i + 1 < fsg_txt_n ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
}

static void stock(fsg_stock_t *s, const char *name, const char *code,
                  const char *brand, const char *kind, const char *desc,
                  const char *soup, int iso, int kelvin, int wbs_gm, int sat,
                  int mono)
{
    memset(s, 0, sizeof *s);
    s->name = name;
    s->code = code;
    s->brand = brand;
    s->kind = kind;
    s->desc = desc;
    s->soup = soup;
    s->iso = iso;
    s->kelvin = kelvin;
    s->wbs_gm = wbs_gm;
    s->saturation = sat;
    {
        fsg_film_t d;
        memset(&d, 0, sizeof d);
        d.brand = brand;
        d.iso = iso;
        d.kelvin = kelvin;
        d.wbs_gm = wbs_gm;
        d.saturation = sat;
        d.mono = mono;
        fsg_stock_palette(&d, s->palette);
    }
}

static int can_index(const char *name);

/* In stock-list order, exactly like the card: 15 films the artwork knows and
 * three it does not, so the fallback cells are visible in the preview. */
static const char *labels[] = {
    "Portra 400", "Portra 160", "Velvia 50", "CineStill 800T",
    "Superia 400", "Ektar 100", "HP5 Plus", "Tri-X 400",
    "Kodachrome 64", "Gold 200", "UltraMax 400", "Pro 400H",
    "C200", "Provia 100F", "Fomapan 100",
};
#define NLABELS ((int)(sizeof labels / sizeof labels[0]))
#define NSTOCKS 15

static void fill_strip(fsg_shelf_t *s)
{
    for (int i = 0; i < NLABELS; i++) {
        s->labels[i] = labels[i];
        s->art[i] = (signed char)can_index(labels[i]);
    }
    s->count = NLABELS;
}

/* Every other frame parks the strip on a film the artwork has no canister for,
 * which is also the frame that shows the cassette fallback. */
/*
 * Park the strip on the stock the panel is describing.
 *
 * This used to alternate the highlight between a real slot and one past the end
 * of the artwork, purely to get a fallback frame out of the same loop -- which
 * meant stock1.png showed Velvia 50 in the panel while the strip highlighted a
 * different film. The harness decides what the picture says; the module draws
 * both from s->sel, so a mismatch in a render is a mismatch in the harness.
 */
static void strip_sel(fsg_shelf_t *s, const char *name, int fallback)
{
    int idx = fallback;
    for (int i = 0; i < NLABELS; i++) {
        if (name && !strcmp(labels[i], name)) {
            idx = i;
            break;
        }
    }
    s->sel = idx;
    s->scroll = s->sel * 256;
}

/* The order of the records in FILMCAN.DAT, as read back from the file. */
static const struct { const char *name; int index; } can_map[] = {
    { "Portra 400", 0 }, { "Portra 160", 1 }, { "Portra 800", 2 },
    { "Ektar 100", 3 }, { "Gold 200", 4 }, { "ColorPlus 200", 5 },
    { "UltraMax 400", 6 }, { "Superia 400", 7 }, { "Pro 400H", 8 },
    { "CineStill 800T", 9 }, { "Velvia 50", 10 }, { "Ektachrome E100", 11 },
    { "Kodachrome 64", 12 }, { "Tri-X 400", 13 }, { "HP5 Plus", 14 },
    { "CineStill 50D", 15 }, { "Color 800", 16 }, { "C200", 17 },
    { "Provia 100F", 18 }, { "T-Max 100", 19 }, { "T-Max P3200", 20 },
    { "Delta 100", 21 }, { "Pan F Plus 50", 22 }, { "Acros 100", 23 },
    { "Fomapan 100", 24 }, { "XP2 Super", 25 },
};


/*
 * Is a band self-contained?
 *
 * The camera now repaints only the strip band (rows 352..445) and the header
 * band (rows 0..45) on an animation frame, and copies just those rows between
 * the two bitmap buffers. That is only sound if drawing the band alone
 * produces exactly what the full screen produces there -- this draws the whole
 * shelf, then wipes the framebuffer, draws one band, and compares.
 */
static unsigned char band_ref[FSG_H][FSG_W][3];
static struct txt_item band_ref_txt[512];
static int band_ref_n;

static int band_check(const fsg_shelf_t *s, int y0, int y1, const char *what,
                      void (*draw)(const fsg_shelf_t *))
{
    int y, x, c, i, bad = 0, txtd = 0;

    fsg_txt_n = 0;
    draw(s);
    for (y = y0; y < y1; y++)
        for (x = 0; x < FSG_W; x++)
            for (c = 0; c < 3; c++)
                band_ref[y][x][c] = fsg_fb[y][x][c];
    band_ref_n = 0;
    for (i = 0; i < fsg_txt_n; i++)
        if (fsg_txt[i].y >= y0 && fsg_txt[i].y < y1)
            band_ref_txt[band_ref_n++] = fsg_txt[i];

    /* wipe, then draw only the band */
    for (y = 0; y < FSG_H; y++)
        for (x = 0; x < FSG_W; x++)
            for (c = 0; c < 3; c++)
                fsg_fb[y][x][c] = 0x5A;
    fsg_txt_n = 0;
    draw(s);

    for (y = y0; y < y1; y++)
        for (x = 0; x < FSG_W; x++)
            for (c = 0; c < 3; c++)
                if (fsg_fb[y][x][c] != band_ref[y][x][c]) bad++;
    for (i = 0; i < fsg_txt_n; i++)
        if (strcmp(fsg_txt[i].s, band_ref_txt[i].s) ||
            fsg_txt[i].x != band_ref_txt[i].x || fsg_txt[i].y != band_ref_txt[i].y)
            txtd++;

    /* and nothing drawn outside the band would have been missed */
    printf("band %-7s rows %3d..%3d: %d px differ, %d text items differ"
           " (%d items, %d in band)\n",
           what, y0, y1 - 1, bad, txtd, fsg_txt_n, band_ref_n);
    return bad == 0 && txtd == 0 && fsg_txt_n == band_ref_n;
}

static int can_index(const char *name)
{
    for (int i = 0; i < (int)(sizeof can_map / sizeof can_map[0]); i++)
        if (!strcmp(name, can_map[i].name)) return can_map[i].index;
    return -1;
}

int main(int argc, char **argv)
{
    const char *can_path = argc > 1 ? argv[1] : FSG_CAN_PATH;
    /* one frame per stock, so the cassettes can be compared side by side:
     * Kodak yellow, Fuji green, CineStill red, Ilford cream, and the two
     * black-and-white cans that carry no colour at all. */
    struct { const char *name, *code, *brand, *kind, *desc, *soup;
             int iso, kelvin, gm, sat, mono; } films[] = {
      {"Portra 400", "P400", "Kodak", "Color Negative",
       "Warm skin, muted greens, soft highlights. The wedding film.",
       "Standard C-41", 400, 5200, 1, 1, 0},
      {"Velvia 50", "VEL50", "Fujifilm", "Reversal / Slide",
       "Hard contrast, deep shadows, greens that glow. Expose carefully.",
       "E-6", 50, 5200, 0, 2, 0},
      {"CineStill 800T", "800T", "CineStill", "Color Negative",
       "Tungsten balanced with a red halation around the highlights.",
       "Standard C-41", 800, 4000, 3, 2, 0},
      {"Fomapan 100", "FOMA", "FOMA", "Black & White",
       "Classic Czech emulsion: soft contrast, honest grain.",
       "Standard B&W", 100, 5500, 0, 0, 1},
      {"HP5 Plus", "HP5", "Ilford", "Black & White",
       "Forgiving, grainy, pushes to 1600 without complaint.",
       "Standard B&W", 400, 5500, 0, 0, 1},
      {"Tri-X 400", "TRI-X", "Kodak", "Black & White",
       "The photojournalist's film: punchy grain, deep blacks.",
       "Standard B&W", 400, 5500, 0, 0, 1},
    };
    const int nfilms = (int)(sizeof films / sizeof films[0]);

    /* the user's own artwork, so the preview shows the canister the camera will */
    if (fsg_can_load(can_path)) {
        printf("canisters: %d records from %s, thumbnails %s\n",
               fsg_can_count(), can_path, fsg_can_thumbs() ? "ok" : "missing");
    } else {
        printf("canisters: %s unusable, drawing the cassette\n", can_path);
    }

    for (int f = 0; f < nfilms; f++) {
        fsg_shelf_t s;
        memset(&s, 0, sizeof s);
        s.badge = "LEVEL 2  -  STYLE + GRADE";
        s.counter = "03 / 12";
        stock(&s.stock, films[f].name, films[f].code, films[f].brand,
              films[f].kind, films[f].desc, films[f].soup, films[f].iso,
              films[f].kelvin, films[f].gm, films[f].sat, films[f].mono);
        fill_strip(&s);
        s.grain = 8;
        s.strength = 100;
        strip_sel(&s, films[f].name, f);
        s.tick = 900u;
        /* the canister the real module would show for this stock */
        int can = can_index(films[f].name);
        if (can >= 0 && fsg_can_pick(can))
            fsg_can_reprogram();
        else {
            fsg_img_set(NULL, 0, 0);
            fsg_pal(s.stock.palette, FSG_NSLOT);
        }
        fsg_reset();
        fsg_draw_shelf(&s);
        char name[16];
        snprintf(name, sizeof name, "stock%d", f);
        dump(name);

        if (f == 0) {
            int ok1, ok2;
            fsg_reset();
            ok1 = band_check(&s, FSG_STRIP_Y, FSG_STRIP_Y + FSG_STRIP_H,
                             "strip", fsg_draw_shelf_strip);
            ok2 = band_check(&s, 0, FSG_HEADER_H, "header",
                             fsg_draw_shelf_header);
            printf("BAND CHECK %s\n", (ok1 && ok2) ? "PASS" : "FAIL");
            /* the full frame again, so the dumped pictures are the real ones */
            fsg_reset();
            fsg_draw_shelf(&s);
            dump(name);
        }
    }

    /* The strip gliding between two stocks.
     *
     * The motion is the camera's own spring (gui/fs_spring.h), stepped on the
     * 30 ms clock the shelf loop runs on, so these are the positions the panel
     * is actually painted at -- not an interpolation of the start and the end.
     * Three stocks of travel is the case fs_spring.h documents its constants
     * against. render.py --gif assembles the frames into an animation. */
    {
        /* CineStill 800T is three slots along the strip from Portra 400 -- the
         * travel fs_spring.h documents its constants against. The two indices
         * are different numbers: `fi` addresses the harness's film table, `li`
         * the strip's label order, and the highlight follows `li`. */
        const int fi = 2, li = 3;
        fs_spring_t sp;
        char counter[16];
        int n = 0;

        snprintf(counter, sizeof counter, "%02d / %02d", li + 1, NLABELS);
        memset(&sp, 0, sizeof sp);
        for (;;) {
            fsg_shelf_t s;
            memset(&s, 0, sizeof s);
            s.badge = "LEVEL 2  -  STYLE + GRADE";
            s.counter = counter;
            stock(&s.stock, films[fi].name, films[fi].code,
                  films[fi].brand, films[fi].kind, films[fi].desc,
                  films[fi].soup, films[fi].iso, films[fi].kelvin,
                  films[fi].gm, films[fi].sat, films[fi].mono);
            fill_strip(&s);
            s.grain = 8;
            s.strength = 100;
            strip_sel(&s, films[fi].name, fi);
            s.sel = li;
            s.scroll = sp.x >> 8;
            s.tick = 900u + (unsigned)n * 30u;
            {
                int can = can_index(films[fi].name);
                if (can >= 0 && fsg_can_pick(can))
                    fsg_can_reprogram();
                else {
                    fsg_img_set(NULL, 0, 0);
                    fsg_pal(s.stock.palette, FSG_NSLOT);
                }
            }
            fsg_reset();
            fsg_draw_shelf(&s);
            {
                char name[16];
                snprintf(name, sizeof name, "anim%02d", n);
                dump(name);
            }
            n++;
            if (!fs_spring_step(&sp, li * 65536, 30) || n >= 40)
                break;
        }
        printf("anim: %d frames, settled at %d.%02d frames\n", n, sp.x >> 16,
               ((sp.x >> 8) & 255) * 100 / 256);
    }

    /* the darkroom is the shelf drawer pointed at a frame */
    {
        fsg_shelf_t s;
        memset(&s, 0, sizeof s);
        s.title = "DARKROOM";
        s.photo = "IMG_3125.CR2";
        s.badge = "LEVEL 2  -  STYLE + GRADE";
        s.counter = "3/136";
        stock(&s.stock, films[0].name, films[0].code, films[0].brand,
              films[0].kind, films[0].desc, films[0].soup, films[0].iso,
              films[0].kelvin, films[0].gm, films[0].sat, films[0].mono);
        fill_strip(&s);
        strip_sel(&s, films[0].name, 0);
        s.grain = 8;
        s.strength = 100;
        s.hint_l = "press = develop    half-shutter = prev frame";
        s.hint_r = "MENU  exit";
        int can = can_index(films[0].name);
        if (can >= 0 && fsg_can_pick(can))
            fsg_can_reprogram();
        else {
            fsg_img_set(NULL, 0, 0);
            fsg_pal(s.stock.palette, FSG_NSLOT);
        }
        fsg_reset();
        fsg_draw_shelf(&s);
        dump("dark");
    }

    /* darkroom at four progress points */
    int pcts[4] = { 0, 12, 70, 100 };
    for (int i = 0; i < 4; i++) {
        fsg_dev_t d;
        memset(&d, 0, sizeof d);
        d.photo = "IMG_3125.CR2";
        d.stock = "Portra 400";
        d.brand = "Kodak";
        d.soup = "C-41";
        d.iso = 400;
        d.pct = pcts[i];
        d.stage = pcts[i] < 10 ? "Opening" : (pcts[i] < 70 ? "Decoding" : "Grading");
        d.index = i < 3 ? 1 : 2;
        d.total = 2;
        d.ok = i < 3 ? 0 : 1;
        d.done = pcts[i] == 100;
        d.tick = (unsigned)(i * 300 + 60);
        if (d.done) d.result = "DONE  -  FLM3125.JPG";
        fsg_reset();
        fsg_draw_develop(&d);
        char name[16];
        snprintf(name, sizeof name, "dev%d", i);
        dump(name);
    }
    return 0;
}

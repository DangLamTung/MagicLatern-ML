/**
 * film_lv - the Soup Engine look, running on LiveView.
 *
 * Why this is a separate module
 * -----------------------------
 * A previous version put the LiveView filter, picture-style slot handling and
 * a raw develop pipeline in one 3,600-line file.  The preview never worked and
 * nobody could tell which part was broken.  This module does exactly one job:
 * change the pixels LiveView shows, in real time.
 *
 * What does the grading
 * ---------------------
 * Not this file.  The look comes from the Soup Engine (se/engine in film_sim):
 * the same se_look compiled from the same .fs stock on the card, applied by the
 * same se_look_rows() that develops the JPEG.  That is the point - a preview
 * built from its own hand-rolled curves can only ever be a look-alike, and when
 * it disagrees with the develop nobody can tell which one is wrong.
 *
 * The one translation this file has to do is colour space.  LiveView arrives as
 * UYVY (one word holds U, Y0, V, Y1); the engine grades 8-bit interleaved RGB.
 * So each 8-row band is unpacked, graded, and packed back.  The conversion is
 * full-range BT.601 and its inverse, which round-trips within 2 of 255 over the
 * whole cube, so strength 0 is very nearly the untouched picture and the
 * residual is chroma subsampling LiveView already had.
 *
 * Chroma convention: U and V are SIGNED bytes, neutral at 0.  src/imgconv.h
 * takes its #else branch on this body, and those macros do not subtract 0x80 -
 * only the DIGIC 6/7/8 branch does, because that hardware stores u+0x80 on
 * purpose.  Magic Lantern's own conversion agrees: precompute_yuv2rgb() in
 * imgconv.c declares "int8_t U = u" before it indexes its table.
 *
 * That is measurable, and it was measured on a dumped plane
 * (ML/FILMSIM/LV_RAW.BIN, 720x480, YUV->RGB, out of 0..255 counted after):
 *
 *     read as signed    mean |U|,|V| 17.9   15.1% of pixels clip
 *     read as unsigned  mean |U|,|V| 110.1  99.1% of pixels clip
 *
 * 99% clipping is not a subtle cast error, it is a blue-cyan frame.
 *
 * Cost, measured on a host with the same integer code: 3.6 ms per 720x480 frame
 * for a monochrome stock with no halation, 11-17 ms for a colour stock with it.
 * The body's ARM11 runs roughly an order of magnitude slower, so expect single
 * digit to high-teens frame rate depending on the stock.  The Frame rate entry
 * caps how often the grade runs; the engine never blocks LiveView.
 */

/* stdlib.h must come before Magic Lantern's headers: mem.h redirects
 * malloc() with a function-like macro, and stdlib.h's own declaration would
 * then be rewritten into nonsense. */
#include <stdlib.h>

#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <config.h>
#include <vram.h>
#include <imgconv.h>
#include <bmp.h>
#include <zebra.h>
#include <propvalues.h>
#include <property.h>
#include <lens.h>
#include <fio-ml.h>

#include "../film_sim/se/engine/se.h"
#include "../film_sim/se/engine/look.h"
#include "../film_sim/se/engine/stock.h"

/* --------------------------------------------------------------- tuning */

static CONFIG_INT("film.lv.enable",   film_lv_enable,   0);
static CONFIG_INT("film.lv.stock",    film_lv_stock,    0);    /* index into ML/FILM */
static CONFIG_INT("film.lv.strength", film_lv_strength, 100);  /* 0..100 blend */
static CONFIG_INT("film.lv.grain",    film_lv_grain,    100);  /* % of the stock's own grain */
static CONFIG_INT("film.lv.fps",      film_lv_fps,      30);
static CONFIG_INT("film.lv.band",     film_lv_band,     0);    /* self test */

/* -------------------------------------------------------------- buffers */

#define LV_BAND      8            /* engine row band (one JPEG MCU row)     */
#define LV_WMAX      960          /* 6D LiveView is 720 wide; margin       */
#define LV_ARENA     (192 * 1024)
#define LV_STOCKTEXT (8 * 1024)
/* ML/FILM ships 33 recipes: a cap of 24 silently hid the last nine and left
 * them unreachable from the menu.  Headroom, not a count. */
#define LV_MAX_STOCKS 40
#define LV_NAME      44

static unsigned char  lv_rgb[LV_BAND * LV_WMAX * 3];
static char           lv_stocktext[LV_STOCKTEXT];
static char           lv_names[LV_MAX_STOCKS][LV_NAME];
static char           lv_name[LV_NAME] = "no stock";
static int            lv_nstocks = -1;

static void    *lv_arena_mem;
static se_arena lv_arena;
static se_look  lv_look;
static se_look_run *lv_run;
static se_stock lv_stock;          /* kept for the bake mapping */
static int      lv_ok;
static int      lv_width;
static int      lv_signature = -1;
static int      lv_valid;
static int      lv_prev_t;
static volatile unsigned int lv_frames;
static volatile int lv_last_ms;
static volatile int lv_last_state;
static volatile int lv_dirty;      /* the look must be rebuilt, off the filter */
static volatile int lv_task_run;
static int      lv_fail;           /* consecutive rebuilds that could not load */

static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* ---------------------------------------------------------------- stocks */

static int lv_ends_with(const char *s, const char *suffix)
{
    int n = strlen(s), m = strlen(suffix);
    if (m > n) return 0;
    return strcasecmp(s + n - m, suffix) == 0;
}

static void lv_scan_stocks(void)
{
    struct fio_file *file = alloc_fio_file();
    struct fio_dirent *de;
    int i, dup;

    /* -1, not 0: a failed scan must be retried, not cached.  The card is not
     * mounted when modules init, so the first FIO call here can fail and the
     * old code then said "none in ML/FILM" for the rest of the session. */
    lv_nstocks = -1;
    if (!file) return;

    de = FIO_FindFirstEx("ML/FILM", file);
    if (IS_ERROR(de)) { free(file); return; }

    lv_nstocks = 0;

    do {
        struct file_info fi = convert_fio_file_info(file);
        int pos;

        if (!fi.name[0] || fi.name[0] == '.') continue;
        if (!lv_ends_with(fi.name, ".fs")) continue;
        if (lv_nstocks >= LV_MAX_STOCKS) continue;

        dup = 0;
        for (i = 0; i < lv_nstocks; i++)
            if (streq(lv_names[i], fi.name)) dup = 1;
        if (dup) continue;

        pos = lv_nstocks;
        while (pos > 0 && strcasecmp(lv_names[pos - 1], fi.name) > 0) {
            memcpy(lv_names[pos], lv_names[pos - 1], LV_NAME);
            pos--;
        }
        memcpy(lv_names[pos], fi.name, LV_NAME);
        lv_names[pos][LV_NAME - 1] = 0;
        lv_nstocks++;
    } while (FIO_FindNextEx(de, file) == 0);

    FIO_FindClose(de);
    free(file);
}

static int lv_load_stock(int index)
{
    FILE *f;
    int n;
    char path[64];

    if (lv_nstocks < 0) lv_scan_stocks();
    if (lv_nstocks <= 0) return 0;
    index = clamp_i(index, 0, lv_nstocks - 1);

    snprintf(path, sizeof path, "ML/FILM/%s", lv_names[index]);
    f = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (!f) return 0;
    n = FIO_ReadFile(f, lv_stocktext, LV_STOCKTEXT);
    FIO_CloseFile(f);
    if (n <= 0) return 0;

    if (se_stock_parse(lv_stocktext, (size_t) n, &lv_stock) != SE_OK) return 0;
    snprintf(lv_name, sizeof lv_name, "%s", lv_stock.name);
    return 1;
}

/* -------------------------------------------------------------- the look */

static int lv_signature_now(void)
{
    return clamp_i(film_lv_stock, 0, 999)
         + clamp_i(film_lv_strength, 0, 100) * 1000
         + clamp_i(film_lv_grain, 0, 200) * 100000
         + (film_lv_enable ? 1 : 0) * 100000000;
}

static void lv_sync(void)
{
    int sig = lv_signature_now();

    if (sig == lv_signature && lv_ok) return;
    lv_signature = sig;
    lv_ok = 0;
    lv_run = NULL;

    if (!lv_arena_mem)
    {
        lv_arena_mem = malloc(LV_ARENA);
        if (!lv_arena_mem) return;
    }

    if (!lv_load_stock(film_lv_stock)) return;
    if (lv_width <= 0) lv_width = 720;

    se_look_from_stock(&lv_stock, 1, &lv_look);

    /* 100 = the grain the stock itself asks for, 0 = none, 200 = double */
    lv_look.grain = clamp_i(lv_look.grain * clamp_i(film_lv_grain, 0, 200) / 100, 0, 100);

    se_look_fit(&lv_look, lv_width, SE_LOOK_BUDGET);

    se_arena_init(&lv_arena, lv_arena_mem, LV_ARENA);
    if (se_look_begin(&lv_run, &lv_look, lv_width, &lv_arena) != SE_OK)
    {
        lv_run = NULL;
        return;
    }
    lv_ok = 1;
}

/* ------------------------------------------------------------ conversion */

/* UYVY -> interleaved RGB8, full-range BT.601.  Chroma is a signed byte, so it
 * is read through signed char and needs no bias removed. */
static void lv_unpack(const unsigned char *s, unsigned char *o, int width)
{
    int x;
    for (x = 0; x + 1 < width; x += 2)
    {
        int u  = (signed char) s[0];
        int y0 = (int) s[1];
        int v  = (signed char) s[2];
        int y1 = (int) s[3];

        o[0] = clamp8(y0 + ((359 * v + 128) >> 8));
        o[1] = clamp8(y0 - (( 88 * u + 183 * v + 128) >> 8));
        o[2] = clamp8(y0 + ((454 * u + 128) >> 8));
        o[3] = clamp8(y1 + ((359 * v + 128) >> 8));
        o[4] = clamp8(y1 - (( 88 * u + 183 * v + 128) >> 8));
        o[5] = clamp8(y1 + ((454 * u + 128) >> 8));

        s += 4;
        o += 6;
    }
}

typedef struct {
    unsigned char *dst;
    const unsigned char *src;
    int pitch;
    int width;          /* the whole row, so a clamped grade can copy the tail */
    int row;
    int row_end;
    int blend;
} lv_emit_t;

/*
 * graded RGB8 -> UYVY, blended against the untouched frame row.
 *
 * The blend weight is an 8-bit fraction computed once per row: this body has no
 * hardware divide, so a "/ 100" per component was four __aeabi_idiv calls per
 * pixel pair - about 700,000 divisions per frame.
 */
static void lv_pack(const unsigned char *rgb, const unsigned char *src,
                    unsigned char *dst, int width, int blend)
{
    int x;
    int w8 = clamp_i(blend, 0, 100) * 256 / 100;
    int k8 = 256 - w8;

    for (x = 0; x + 1 < width; x += 2)
    {
        const unsigned char *p = rgb + (size_t) x * 3;

        int y0 = ( 77 * p[0] + 150 * p[1] +  29 * p[2] + 128) >> 8;
        int y1 = ( 77 * p[3] + 150 * p[4] +  29 * p[5] + 128) >> 8;
        int u  = (-43 * p[0] -  85 * p[1] + 128 * p[2] + 128) >> 8;
        int v  = (128 * p[0] - 107 * p[1] -  21 * p[2] + 128) >> 8;

        u  = (u  * w8 + (int)(signed char) src[0] * k8) >> 8;
        y0 = (y0 * w8 + (int) src[1] * k8) >> 8;
        v  = (v  * w8 + (int)(signed char) src[2] * k8) >> 8;
        y1 = (y1 * w8 + (int) src[3] * k8) >> 8;

        dst[0] = (unsigned char)(signed char) clamp_i(u, -128, 127);
        dst[1] = (unsigned char) clamp8(y0);
        dst[2] = (unsigned char)(signed char) clamp_i(v, -128, 127);
        dst[3] = (unsigned char) clamp8(y1);

        src += 4;
        dst += 4;
    }
}

static int lv_emit(void *ctx, const unsigned char *rgb, int width)
{
    lv_emit_t *e = (lv_emit_t *) ctx;

    if (e->row >= 0 && e->row < e->row_end)
    {
        size_t off = (size_t) e->row * e->pitch;
        lv_pack(rgb, e->src + off, e->dst + off, width, e->blend);
        if (width < e->width)
            memcpy(e->dst + off + (size_t) width * 2,
                   e->src + off + (size_t) width * 2,
                   (size_t) (e->width - width) * 2);
    }
    e->row++;
    return 0;
}

/* Grade rows [row0,row1) of a UYVY frame through the engine look. */
static void lv_grade(const void *src, void *dst, int pitch, int row0, int row1)
{
    lv_emit_t e;
    int width, y;

    if (!lv_ok || !lv_run) return;

    width = pitch / 2;
    if (width > LV_WMAX) width = LV_WMAX;
    if (width <= 1) return;

    /* the halation delay line must start empty for every frame */
    se_arena_init(&lv_arena, lv_arena_mem, LV_ARENA);
    if (se_look_begin(&lv_run, &lv_look, width, &lv_arena) != SE_OK)
    {
        /* Not silent: the loader task re-fits the look for this width.  lv_ok
         * clears so the filter shows the untouched frame meanwhile, and the
         * filter's own guard stops it retrying a begin that keeps failing. */
        lv_run = NULL;
        lv_ok = 0;
        lv_dirty = 1;
        return;
    }

    e.dst = (unsigned char *) dst;
    e.src = (const unsigned char *) src;
    e.pitch = pitch;
    e.width = pitch / 2;
    e.row = row0;
    e.row_end = row1;
    e.blend = clamp_i(film_lv_strength, 0, 100);

    for (y = row0; y < row1; y += LV_BAND)
    {
        int n = row1 - y;
        int r;

        if (n > LV_BAND) n = LV_BAND;
        for (r = 0; r < n; r++)
            lv_unpack((const unsigned char *) src + (size_t) (y + r) * pitch,
                      lv_rgb + (size_t) r * width * 3, width);

        if (se_look_rows(lv_run, lv_rgb, n, lv_emit, &e) != SE_OK) break;
    }

    /* flushes whatever the halation delay line is still holding */
    se_look_end(lv_run, lv_emit, &e);
}

/* -------------------------------------------------------- display filter */

/* display_filter_step() points the panel at our buffer only for a frame the
 * module said it wrote (tweaks.c: "if (display_filter_buffer[0] &&
 * display_filter_written)").  film_lv never said so, so nothing it drew was
 * ever displayed - the module was a no-op on screen. */
extern void display_filter_mark_written(void);

static unsigned int lv_filter(unsigned int ctx)
{
    if (ctx == 0)
    {
        /*
         * Always claim the filter, and decide whether to do anything inside the
         * draw.  module.c caches whatever this returns and only calls a filter
         * whose cached value is non-zero, so returning 0 while disabled meant
         * switching the preview on did nothing until LiveView restarted - the
         * "it changes, but only after a while" report.
         */
        int active = film_lv_enable ? 1 : 0;
        if (!active) lv_valid = 0;
        lv_last_state = (active << 4) | ((lv_ok ? 1 : 0) << 3) | ((get_global_draw() ? 1 : 0) << 2)
                      | ((ext_monitor_hdmi || hdmi_code) ? 1 : 0);
        return 1;
    }

    struct display_filter_buffers * b = (struct display_filter_buffers *) ctx;
    struct vram_info * vr;
    int pitch, height, row0, row1, fps, t, due, started;

    if (!b->src_buf || !b->dst_buf) return 0;

    vr = get_yuv422_vram();
    pitch  = (vr && vr->pitch  > 0) ? vr->pitch  : 1440;
    height = (vr && vr->height > 0) ? vr->height : 480;

    /* the buffer width decides the look's line buffers, so re-fit if it moves */
    if (pitch / 2 != lv_width)
    {
        lv_width = pitch / 2;
        lv_signature = -1;
        lv_ok = 0;
        lv_run = NULL;
    }

    /* No card I/O in here: a rebuild is the loader task's job, this only says
     * the look is stale.  film_sim's own header says the same thing - never do
     * FIO in CBR_DISPLAY_FILTER. */
    if (lv_signature_now() != lv_signature) lv_dirty = 1;

    /*
     * This module claimed the filter, and ML points the panel at dst_buf for
     * every frame it thinks we drew - whether or not we wrote anything.  So dst
     * is filled on every call, even with the look off: a full-plane copy is the
     * only thing that keeps the picture right when the module is loaded but
     * disabled, and an empty dst_buf showed stale heap.
     */
    if (!film_lv_enable || !lv_ok || !lv_run)
    {
        if (b->src_buf != b->dst_buf)
            memcpy(b->dst_buf, b->src_buf, (size_t) height * pitch);
        lv_last_ms = 0;
        lv_frames++;
        lv_valid = 1;
        display_filter_mark_written();
        return 0;
    }

    fps = clamp_i(film_lv_fps, 5, 60);
    t = get_ms_clock();
    due = (t - lv_prev_t >= 1000 / fps * 3 / 4) || (t < lv_prev_t);
    if (!due && lv_valid) return 0;
    lv_prev_t = t;

    row0 = BM2LV_Y(os.y0);
    row1 = BM2LV_Y(os.y_max);
    if (row1 <= row0 || row0 < 0 || row1 > height) { row0 = 0; row1 = height; }

    if (b->src_buf != b->dst_buf)
    {
        if (row0 > 0) memcpy(b->dst_buf, b->src_buf, (size_t) row0 * pitch);
        if (row1 < height)
            memcpy((char *) b->dst_buf + (size_t) row1 * pitch,
                   (const char *) b->src_buf + (size_t) row1 * pitch,
                   (size_t) (height - row1) * pitch);
    }

    started = get_ms_clock();
    lv_grade(b->src_buf, b->dst_buf, pitch, row0, row1);
    lv_last_ms = get_ms_clock() - started;

    if (film_lv_band)
    {
        /* U=85 Y=105 V=107 is magenta with chroma read as signed; the old
         * 0x81FF81FF said magenta only under the unsigned reading and is a
         * plain grey band once the convention is right. */
        int rows = 48;
        if (rows > row1 - row0) rows = row1 - row0;
        for (int y = row0; y < row0 + rows; y++)
        {
            unsigned int * p = (unsigned int *) ((char *) b->dst_buf + (size_t) y * pitch);
            for (int i = 0; i < (pitch >> 2); i++) p[i] = 0x696B6955u;
        }
    }

    lv_frames++;
    lv_valid = 1;
    display_filter_mark_written();
    return 0;
}

/* ----------------------------------------------------------------- menu */

static int next_index(int cur, int n, int delta)
{
    int i;
    if (n <= 0) return 0;
    i = clamp_i(cur, 0, n - 1) + (delta < 0 ? -1 : 1);
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    return i;
}

static MENU_SELECT_FUNC(lv_stock_next)
{
    if (lv_nstocks < 0) lv_scan_stocks();
    film_lv_stock = next_index(film_lv_stock, lv_nstocks, delta);
    film_lv_enable = 1;
    film_lv_strength = 100;
    lv_valid = 0;
    lv_dirty = 1;
}

static MENU_UPDATE_FUNC(lv_stock_update)
{
    if (lv_nstocks < 0) lv_scan_stocks();
    if (lv_nstocks <= 0)
    {
        MENU_SET_VALUE("none in ML/FILM");
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING,
            "No .fs stock found. Put stock files in ML/FILM on the card and "
            "re-enter LiveView.");
        return;
    }
    MENU_SET_VALUE("%s%s", lv_name, film_lv_enable ? "" : " (off)");
    MENU_SET_WARNING(MENU_WARN_INFO,
        "SET: next stock from ML/FILM. Same file, same look code as the develop.");
}

static MENU_SELECT_FUNC(lv_onoff)
{
    film_lv_enable = !film_lv_enable;
    lv_valid = 0;
    lv_dirty = 1;
}

static MENU_UPDATE_FUNC(lv_onoff_update)
{
    MENU_SET_VALUE("%s", film_lv_enable ? "ON" : "OFF");
    MENU_SET_WARNING(MENU_WARN_INFO,
        "Master switch for the preview filter. If this is ON and the picture still "
        "does not change, check Developer > TEST band.");
}

static MENU_SELECT_FUNC(lv_strength_next)
{
    static const int steps[] = { 100, 75, 50, 25, 0 };
    int v = clamp_i(film_lv_strength, 0, 100), i, next = 0;
    for (i = 0; i < 5; i++)
        if (steps[i] == v) { next = (i + 1) % 5; break; }
    film_lv_strength = steps[next];
    lv_valid = 0;
}

static MENU_UPDATE_FUNC(lv_strength_update)
{
    MENU_SET_VALUE("%d%%", clamp_i(film_lv_strength, 0, 100));
    MENU_SET_WARNING(MENU_WARN_INFO,
        "Blends the graded frame back toward the untouched one. 100 is the full look.");
}

static MENU_SELECT_FUNC(lv_grain_next)
{
    static const int steps[] = { 100, 50, 0, 150, 200 };
    int v = clamp_i(film_lv_grain, 0, 200), i, next = 0;
    for (i = 0; i < 5; i++)
        if (steps[i] == v) { next = (i + 1) % 5; break; }
    film_lv_grain = steps[next];
    lv_valid = 0;
    lv_dirty = 1;
}

static MENU_UPDATE_FUNC(lv_grain_update)
{
    MENU_SET_VALUE("%d%% of stock", clamp_i(film_lv_grain, 0, 200));
    MENU_SET_WARNING(MENU_WARN_INFO,
        "Grain as a percentage of what the stock itself asks for. 0 disables it, "
        "which is also the fastest preview.");
}

static MENU_SELECT_FUNC(lv_band_toggle)
{
    film_lv_band = !film_lv_band;
    film_lv_enable = 1;
    lv_valid = 0;
}

static MENU_UPDATE_FUNC(lv_band_update)
{
    MENU_SET_VALUE("%s", film_lv_band ? "ON - magenta band" : "off");
    MENU_SET_WARNING(MENU_WARN_INFO,
        "SET paints a magenta band across the top of LiveView. No band means the "
        "display filter is not running and no look can appear.");
}

static MENU_UPDATE_FUNC(lv_state_update)
{
    int st = lv_last_state;
    MENU_SET_VALUE("%u f %dms en%d ok%d", lv_frames, lv_last_ms,
                   (st >> 4) & 1, (st >> 3) & 1);
    MENU_SET_WARNING(MENU_WARN_INFO,
        "frames, milliseconds for the last grade, enabled, look compiled. If frames "
        "stays 0 the filter is not being called.");
}

/*
 * Bake the stock into the ACTIVE User Def picture style.
 *
 * The numbers come from the stock itself rather than a parallel table: the
 * engine has already compiled the curves and the saturation when lv_sync ran,
 * so read the four sliders out of that. It is still a crude approximation - a
 * picture style has five global levers and a film has a curve per channel, a
 * hue-dependent saturation, halation and grain - and it says so.
 */
static volatile int lv_bake_ok = -1;

static int lv_look_saturation_slider(void)
{
    /* lv_look.sat is 256 = unchanged; a slider step is about a quarter */
    return clamp_i((lv_look.sat - 256) / 64, -4, 4);
}

static int lv_look_contrast_slider(void)
{
    /* how far the curve lifts a shadow and pulls a highlight, against linear */
    int lo = (int) lv_look.map[1][64]  - 64;
    int hi = (int) lv_look.map[1][192] - 192;
    return clamp_i((lo - hi) / 8, -4, 4);
}

static int lv_look_tone_slider(void)
{
    /* split tone: red up in the highlights is warmth, blue up is coolness */
    return clamp_i((lv_look.hi[0] - lv_look.hi[2]) / 2, -4, 4);
}

static void lv_bake(void)
{
    int slot = (lens_info.raw_picstyle == 0x21) ? 1
             : (lens_info.raw_picstyle == 0x22) ? 2
             : (lens_info.raw_picstyle == 0x23) ? 3 : 0;

    static const uint32_t set_prop[4] = { 0, 0x02060007, 0x02060008, 0x02060009 };
    int32_t v[6];

    if (!slot) { lv_bake_ok = 0; return; }

    v[0] = lv_look_contrast_slider();
    v[1] = 0;                                   /* sharpness: no source for it */
    v[2] = lv_look_saturation_slider();
    v[3] = lv_look_tone_slider();
    v[4] = (int32_t) 0xDEADBEEF;
    v[5] = (int32_t) 0xDEADBEEF;
    prop_request_change_wait(set_prop[slot], v, 24, 500);

    lv_bake_ok = 1;
}

static MENU_SELECT_FUNC(lv_bake_press)
{
    lv_bake();
}

static MENU_UPDATE_FUNC(lv_bake_update)
{
    int raw = lens_info.raw_picstyle;
    int is_user = (raw == 0x21 || raw == 0x22 || raw == 0x23);

    if (!is_user)
    {
        MENU_SET_VALUE("%s - select User Def first", lv_name);
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING,
            "Bake writes into a User Def picture style. Select User Def. 1, 2 or 3 "
            "in the Canon menu, then press SET here.");
        return;
    }

    MENU_SET_VALUE("%s -> con%+d sat%+d tone%+d", lv_name,
                   lv_look_contrast_slider(), lv_look_saturation_slider(),
                   lv_look_tone_slider());
    MENU_SET_WARNING(MENU_WARN_INFO,
        lv_bake_ok == 1
            ? "Written. film_sim > Film sim does the same thing from the XML stocks."
            : "Derived from the loaded .fs stock, not from a second table.");
}

static MENU_UPDATE_FUNC(lv_sim_update)
{
    MENU_SET_VALUE("%s", film_lv_enable ? "On" : "Off");
    MENU_SET_WARNING(MENU_WARN_INFO,
        "Applies the stock to the LiveView preview using the develop look code. "
        "Preview only - no JPEG and no raw file contains this.");
}

static MENU_UPDATE_FUNC(lv_fps_update)
{
    MENU_SET_VALUE("%d fps", clamp_i(film_lv_fps, 5, 60));
    MENU_SET_WARNING(MENU_WARN_INFO,
        "How often the preview is re-graded. Lower is cheaper on the CPU; "
        "Developer > State shows what a grade actually costs.");
}

static MENU_SELECT_FUNC(lv_fps_next)
{
    static const int fps[] = { 30, 24, 15, 10, 5 };
    int v = clamp_i(film_lv_fps, 0, 60), i, next = 0;
    for (i = 0; i < 5; i++)
        if (fps[i] <= v) { next = (i + 1) % 5; break; }
    film_lv_fps = fps[next];
    lv_valid = 0;
}

/*
 * A stock selector belongs in a normal ML menu: themed, wheel-navigable,
 * consistent with every other module.  The painted canister screen that used
 * to live here was replaced with this.
 */
static struct menu_entry film_lv_menu[] =
{
{
    .name = "Simulation",
    .select = lv_onoff,
    .update = lv_sim_update,
    .help = "Turn the film simulation on or off.",
},
{
    .name = "Preset",
    .select = lv_stock_next,
    .select_Q = lv_stock_next,
    .update = lv_stock_update,
    .help = "Film stock applied to the preview. Left/right steps through ML/FILM.",
    .help2 = "The .fs files on the card, graded by the same code as the develop.",
},
{
    .name = "Strength",
    .select = lv_strength_next,
    .update = lv_strength_update,
    .help = "How much of the stock is applied: 100, 75, 50, 25 or 0 percent.",
},
{
    .name = "Grain",
    .select = lv_grain_next,
    .update = lv_grain_update,
    .help = "Grain as a percentage of the stock's own, up to 200.",
},
{
    .name = "Frame rate",
    .select = lv_fps_next,
    .update = lv_fps_update,
    .help = "Preview refresh rate: 30, 24, 15, 10 or 5 frames per second.",
},
{
    .name = "Developer",
    .children = (struct menu_entry[])
    {
        { .name = "Bake to JPEG", .select = lv_bake_press, .update = lv_bake_update,
          .help = "Write this stock into the active User Def picture style.",
          .help2 = "Crude: a style has five global levers, a film has a curve per "
                   "channel plus halation and grain." },
        { .name = "TEST band", .select = lv_band_toggle, .update = lv_band_update,
          .help = "Magenta band proves the display filter is running." },
        { .name = "State", .update = lv_state_update,
          .help = "Frames processed, cost of a grade, and why the filter may be idle." },
        MENU_EOL,
    },
    .help = "Bake, diagnostics.",
},
    MENU_EOL,
};

/* ----------------------------------------------------------------- init */

/*
 * The look is built here, in a task, not in the display filter and not at
 * module init.
 *
 * Module init runs before the card is mounted: the first version scanned
 * ML/FILM there, the FIO call failed, and the failure was cached as "no stocks"
 * for the whole session.  film_sim reads its own card levers five seconds after
 * boot for exactly this reason and logs it.
 */
static void lv_load_task(void)
{
    int t;

    /* Wait for the card in 100 ms steps rather than one 5 s sleep: if the
     * module is disabled while this task is asleep, deinit returns and the
     * module's memory goes away under a task still executing its code. */
    for (t = 0; t < 50 && lv_task_run; t++)
        msleep(100);

    while (lv_task_run)
    {
        if (lv_dirty)
        {
            lv_dirty = 0;
            lv_sync();
            if (lv_ok)
            {
                lv_fail = 0;
            }
            else if (++lv_fail < 15)
            {
                /* the stock could not be read yet - card not mounted, file
                 * being replaced.  Retry on a 2 s clock, not on the menu's:
                 * a card read every tenth of a second is worse than a look
                 * that arrives two seconds late. */
                msleep(2000);
                if (lv_task_run) lv_dirty = 1;
            }
            /* 15 failures is a card with no .fs on it; stop and let the filter
             * show the untouched frame.  A menu change buys one more attempt. */
        }
        msleep(100);
    }
}

static unsigned int film_lv_init(void)
{
    /* film_lv used to add itself to "Film" as well, which ML core already
     * creates (menu.c menu_init) and film_sim also writes into - one tab with
     * two modules' entries in it.  A preview-only tool belongs in Modules. */
    menu_add("Modules", film_lv_menu, COUNT(film_lv_menu) - 1);

    lv_signature = -1;
    lv_dirty = 1;
    lv_task_run = 1;
    if (!task_create("lv_load", 0x1e, 0x2000, lv_load_task, 0))
    {
        lv_task_run = 0;
        lv_dirty = 0;
    }
    return 0;
}

static unsigned int film_lv_deinit(void)
{
    lv_task_run = 0;
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(film_lv_init)
    MODULE_DEINIT(film_lv_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_DISPLAY_FILTER, lv_filter, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(film_lv_enable)
    MODULE_CONFIG(film_lv_stock)
    MODULE_CONFIG(film_lv_strength)
    MODULE_CONFIG(film_lv_grain)
    MODULE_CONFIG(film_lv_fps)
    MODULE_CONFIG(film_lv_band)
MODULE_CONFIGS_END()

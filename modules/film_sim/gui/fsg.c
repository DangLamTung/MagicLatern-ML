/*
 * fsg.c - drawing for the Film Sim shelf and darkroom screens.
 *
 * Only fsg_fill() and fsg_text() touch the panel, so this file is compiled
 * unchanged for the camera (gui/fsg_ml.c) and for the desktop preview
 * (tools/gui_preview/fsg_host.c). That is deliberate: what you see rendered
 * on a Mac is the same code that draws on the 6D, pixel for pixel.
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "fsg.h"

/* ---- small helpers ----------------------------------------------------- */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* The camera's overlay is a 256-entry palette, not RGB, so the background is
 * four flat bands rather than a gradient: every colour in this file has to be
 * one of the constants in fsg.h with an exact camera mapping. */
static void vgrad(int x, int y, int w, int h, int top, int bot)
{
  (void)top; (void)bot;
  int h1 = h / 4, h2 = h / 2;
  fsg_fill(x, y, w, h1, FSG_BG2);
  fsg_fill(x, y + h1, w, h2 - h1, FSG_BG);
  fsg_fill(x, y + h2, w, h - h2, FSG_BG_DEEP);
}

/* Four corner marks instead of rounded corners: 3 px is enough to read as a
 * deliberate frame and costs four fills. */
static void corners(int x, int y, int w, int h, int rgb, int len)
{
  fsg_fill(x, y, len, 2, rgb);
  fsg_fill(x, y, 2, len, rgb);
  fsg_fill(x + w - len, y, len, 2, rgb);
  fsg_fill(x + w - 2, y, 2, len, rgb);
  fsg_fill(x, y + h - 2, len, 2, rgb);
  fsg_fill(x, y + h - len, 2, len, rgb);
  fsg_fill(x + w - len, y + h - 2, len, 2, rgb);
  fsg_fill(x + w - 2, y + h - len, 2, len, rgb);
}


int fsg_wrap(const char *text, char out[][48], int max_lines, int width)
{
  int line = 0, len = 0;
  if (!text || width < 8) return 0;
  out[0][0] = 0;
  for (const char *p = text; *p && line < max_lines; ) {
    while (*p == ' ') p++;
    const char *w = p;
    while (*p && *p != ' ') p++;
    int wl = (int)(p - w);
    if (wl > 47) wl = 47;
    if (len + (len ? 1 : 0) + wl > width) {
      if (len == 0) { /* single long word: hard break */
        for (int i = 0; i < wl && i < width; i++) out[line][i] = w[i];
        out[line][width] = 0;
        line++;
        if (line >= max_lines) break;
        len = 0;
        out[line][0] = 0;
        continue;
      }
      line++;
      if (line >= max_lines) break;
      len = 0;
    }
    if (len) out[line][len++] = ' ';
    for (int i = 0; i < wl && len < 47; i++) out[line][len++] = w[i];
    out[line][len] = 0;
  }
  return line + 1;
}

/* ---- shared chrome ----------------------------------------------------- */

static void top_bar(const char *title, const char *badge, const char *right,
                    unsigned tick)
{
  fsg_fill(0, 0, FSG_W, 46, FSG_RED_DEEP);
  fsg_fill(0, 44, FSG_W, 2, FSG_RED);
  /* A slow bar of the lighter red sweeps the header: one extra palette colour,
   * and enough movement that the bar does not look painted on. */
  {
    int sweep = (int)((tick / 9) % (unsigned)(FSG_W + 180)) - 180;
    if (sweep > -180 && sweep < FSG_W)
      fsg_fill(sweep, 0, 120, 44, FSG_RED_DARK);
  }
  fsg_text(20, 9, FSG_T_MED, FSG_AMBER, FSG_RED_DEEP, "%s", title);
  if (badge && badge[0])
    fsg_text(20, 30, FSG_T_SMALL, 0xE8C9A0, FSG_RED_DEEP, "%s", badge);
  if (right && right[0])
    fsg_text(600, 15, FSG_T_MED, FSG_WHITE, FSG_RED_DEEP, "%s", right);
}

static void hint_row(const char *l, const char *r)
{
  fsg_fill(0, 450, FSG_W, 30, FSG_BG);
  if (l) fsg_text(20, 456, FSG_T_SMALL, FSG_GREY, FSG_BG, "%s", l);
  if (r) fsg_text(560, 456, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG, "%s", r);
}

/* The palette-index blitter (below, with the picture code) is also what draws
 * the strip's thumbnails, so it is declared here. */
static void blit(int x, int y, const unsigned char *px, int w, int h);

/* ---- the film strip ---------------------------------------------------- */

#define STRIP_Y   FSG_STRIP_Y
#define STRIP_H   FSG_STRIP_H
#define FRAME_W   96
#define FRAME_H   66
#define FRAME_PITCH 108

static void perf_row(int y, int phase)
{
  for (int x = -24 + (phase % 24); x < FSG_W + 24; x += 24) {
    int cx = x;
    if (cx < 0 || cx + 12 > FSG_W) continue;
    fsg_fill(cx, y, 12, 7, FSG_PERF);
    fsg_fill(cx, y, 12, 1, FSG_PERF_HI);
  }
}

static void film_strip(const fsg_shelf_t *s)
{
  int n = s->count;
  if (n <= 0) return;
  if (n > FSG_MAX_LABELS) n = FSG_MAX_LABELS;

  fsg_fill(0, STRIP_Y, FSG_W, STRIP_H, FSG_BG2);
  fsg_fill(0, STRIP_Y, FSG_W, 1, FSG_LINE);
  fsg_fill(0, STRIP_Y + STRIP_H - 1, FSG_W, 1, FSG_LINE);

  /* Perforations are the film's own sprocket holes, so they move with the
   * strip and by exactly as much: one frame of travel is FRAME_PITCH pixels.
   * A `tick / 3` term used to be added on top, which is a pixel every 3 ms --
   * a whole 24 px period in 72 ms. Now that the strip is only redrawn while it
   * moves, that term would not scroll, it would jump on every repaint. */
  int phase = -(s->scroll * FRAME_PITCH) / 256;
  perf_row(STRIP_Y + 4, phase);
  perf_row(STRIP_Y + STRIP_H - 11, phase);

  /* Centre of the lit gate */
  const int cx = FSG_W / 2;
  int scr = s->scroll;                    /* 1/256 frame units */

  for (int i = 0; i < n; i++) {
    int dx = ((i * 256 - scr) * FRAME_PITCH) / 256;
    int x = cx + dx - FRAME_W / 2;
    if (x < -FRAME_W || x > FSG_W) continue;

    int sel = (i == s->sel);
    int y = sel ? STRIP_Y + 14 : STRIP_Y + 20;
    int h = sel ? STRIP_H - 26 : STRIP_H - 38;
    int body = sel ? FSG_PANEL_HI : FSG_PANEL;
    fsg_fill(x + 1, y + 2, FRAME_W, h, FSG_BLACK);          /* drop shadow */
    fsg_fill(x, y, FRAME_W, h, body);
    fsg_fill(x, y, FRAME_W, 1, sel ? FSG_AMBER : FSG_LINE_DIM);
    fsg_fill(x, y + h - 1, FRAME_W, 1, sel ? FSG_AMBER : FSG_LINE_DIM);
    fsg_fill(x, y, 1, h, sel ? FSG_AMBER : FSG_LINE_DIM);
    fsg_fill(x + FRAME_W - 1, y, 1, h, sel ? FSG_AMBER : FSG_LINE_DIM);

    /* The cell shows the film's own canister when the artwork palette is the
     * one loaded -- the thumbnails index the same palette as the big record, so
     * drawing them beside a cassette screen would show the wrong colours. No
     * artwork: the cell falls back to the film's name, as it always did. */
    int tw = 0, th = 0;
    int ai = s->art[i];
    const unsigned char *tp =
        (fsg_can_ready() && ai >= 0) ? fsg_can_thumb(ai, &tw, &th) : NULL;

    if (tp && tw > 0 && th > 0 && tw <= FRAME_W - 8 && th <= h) {
      blit(x + (FRAME_W - tw) / 2, y + (h - th) / 2, tp, tw, th);
      /* the name is already in the panel; only the number has to fit here */
      char num[8];
      snprintf(num, sizeof num, "%02d", i + 1);
      fsg_text(x + 4, y + h - 16, FSG_T_SMALL,
               sel ? FSG_WHITE : FSG_GREY_DIM, body, "%s", num);
    } else {
      const char *lbl = s->labels[i] ? s->labels[i] : "";
      char short_lbl[13];
      {   /* 12 characters, cut on a space when there is one, so "CineStill
           * 800T" becomes "CineStill" and not "CineStill 80" */
        int l = (int)strlen(lbl);
        if (l > 12) {
          l = 12;
          while (l > 4 && lbl[l - 1] != ' ') l--;
          if (l <= 4) l = 12;
        }
        memcpy(short_lbl, lbl, (size_t)l);
        short_lbl[l] = 0;
      }
      fsg_text(x + 6, y + 6, FSG_T_SMALL, sel ? FSG_AMBER : FSG_GREY, body,
               "%s", short_lbl);

      /* frame number, bottom right of the cell */
      char num[8];
      snprintf(num, sizeof num, "%02d", i + 1);
      fsg_text(x + FRAME_W - 22, y + h - 16, FSG_T_SMALL,
               sel ? FSG_WHITE : FSG_GREY_DIM, body, "%s", num);
    }

    if (sel) {
      /* gate marks above and below the chosen frame */
      fsg_fill(x + FRAME_W / 2 - 6, STRIP_Y + 2, 12, 3, FSG_AMBER);
      fsg_fill(x + FRAME_W / 2 - 6, STRIP_Y + STRIP_H - 6, 12, 3, FSG_AMBER);
      int glow = (int)((s->tick / 260) & 1);
      fsg_fill(x, y - 3, FRAME_W, 2, glow ? FSG_AMBER : FSG_AMBER_DIM);
    }
  }
}

/* ---- the film's own colours --------------------------------------------
 *
 * The shelf draws a cassette for the current stock, and the panel has no
 * per-stock colour of its own: ML's palette is nine hues and a grey ramp, which
 * is why the old tone ladder was identical for every film. fsg reserves 16
 * entries and fsg_pal() reprograms them, so this is where the film's look is
 * decided -- from data the stock already carries:
 *
 *   brand      -> hue of the canister body and the label stripe
 *   box speed  -> how dark the can (a 50 ASA body is light, a 3200 is dark)
 *   kelvin     -> warm / cool cast
 *   wbs_gm     -> green / magenta cast
 *   saturation -> how much colour there is; `mono` removes it all
 */
static int fs_rgb(int r, int g, int b) {
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (r << 16) | (g << 8) | b;
}

static int fs_luma(int c) {
  return (77 * ((c >> 16) & 255) + 150 * ((c >> 8) & 255) + 29 * (c & 255)) >> 8;
}

/* pct: 100 keeps the colour, 50 halves its brightness, 0 is black */
static int fs_bright(int c, int pct) {
  return fs_rgb(((c >> 16) & 255) * pct / 100, ((c >> 8) & 255) * pct / 100,
                (c & 255) * pct / 100);
}

/* shift the three channels, used for the white-balance cast */
static int fs_cast(int c, int dr, int dg, int db) {
  return fs_rgb(((c >> 16) & 255) + dr, ((c >> 8) & 255) + dg,
                (c & 255) + db);
}

/* pct: 0 makes it grey, 100 as it is, 200 doubles the distance from grey */
static int fs_saturate(int c, int pct) {
  int l = fs_luma(c);
  int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return fs_rgb(l + (r - l) * pct / 100, l + (g - l) * pct / 100,
                l + (b - l) * pct / 100);
}

static int fs_brand_colour(const char *brand) {
  if (!brand) return 0x8C8C99;
  if (!strncmp(brand, "Kodak", 5))       return 0xF0B32C;   /* yellow box */
  if (!strncmp(brand, "Fuji", 4))        return 0x1E8B4F;   /* green box  */
  if (!strncmp(brand, "Ilford", 6))      return 0xDCD6C6;   /* cream box  */
  if (!strncmp(brand, "CineStill", 9))   return 0xC0392B;   /* red can    */
  if (!strncmp(brand, "Lomography", 10)) return 0xE4572E;   /* orange     */
  if (!strncmp(brand, "Foma", 4))        return 0x2B6CB0;   /* blue       */
  if (!strncmp(brand, "Process", 7))     return 0x2AA198;   /* teal       */
  return 0x8C8C99;
}

void fsg_stock_palette(const fsg_film_t *f, int out[FSG_NSLOT]) {
  int brand = fs_brand_colour(f->brand);
  int speed = clampi(f->iso, 50, 3200);
  int kelvin = clampi(f->kelvin, 4000, 6800);

  int body_l = 100 - (speed - 50) * 45 / 3150;      /* 100 % at 50 ASA */
  int warm = (5200 - kelvin) * 100 / 1300;          /* + warm, - cool  */
  int gm = clampi(f->wbs_gm, -9, 9) * 12;

  int body = fs_bright(f->mono ? 0x9A9A9A : brand, body_l);
  body = fs_cast(body, warm / 6, gm / 7, -warm / 6);
  if (!f->mono)
    body = fs_saturate(body, 100 + clampi(f->saturation, -3, 3) * 14);

  int paper = fs_cast(0xF2EDE0, warm / 12, gm / 14, -warm / 12);
  int accent = f->mono ? fs_saturate(brand, 60) : fs_saturate(brand, 120);

  out[FSG_SL_BORDER]   = fs_bright(body, 20);
  out[FSG_SL_BODY_HI]  = fs_bright(body, 138);
  out[FSG_SL_BODY]     = body;
  out[FSG_SL_BODY_LO]  = fs_bright(body, 58);
  out[FSG_SL_CAP_HI]   = fs_cast(0xD9D9DD, warm / 10, gm / 12, -warm / 10);
  out[FSG_SL_CAP_LO]   = 0x8E8E96;
  out[FSG_SL_PAPER]    = paper;
  out[FSG_SL_INK]      = 0x14141A;
  out[FSG_SL_ACCENT]   = accent;
  out[FSG_SL_TONGUE]   = fs_bright(body, 42);
  out[FSG_SL_SHADOW]   = f->mono ? 0x18181A : fs_bright(fs_cast(body, -warm / 10, gm / 10, warm / 10), 52);
  out[FSG_SL_MID]      = f->mono ? 0x6E6E70 : body;
  out[FSG_SL_LIGHT]    = f->mono ? 0xD8D8DA : fs_saturate(fs_cast(paper, warm / 8, -gm / 10, -warm / 8), 70);
  out[FSG_SL_SPROCKET] = fs_bright(body, 28);
  out[FSG_SL_GLOW]     = fs_bright(fs_cast(paper, warm / 6, 0, -warm / 6), 96);
  out[FSG_SL_EXTRA]    = 0x101016;
}

/* ---- the stock picture ------------------------------------------------- */

static fsg_img_t cur_img;

void fsg_img_set(const unsigned char *px, int w, int h)
{
  cur_img.px = px;
  cur_img.w = px ? w : 0;
  cur_img.h = px ? h : 0;
}

/* Draw the picture with its top-left at x,y, one fill per run of equal
 * indices. Measured on the real artwork: about 25,000 fills for a 170x240
 * record (a flat run averages under two pixels), and bmp_fill is a memset per
 * row, so a frame costs single-digit milliseconds -- the shelf redraws on a
 * 70 ms clock, which leaves room. Merging runs downwards across rows was tried
 * and only saves 15%: the artwork is dithered, not flat.
 *
 * Index 255 is ML's transparent slot -- the artwork leaves its background as
 * 255 so the canister sits on the panel instead of on a printed rectangle, and
 * painting it would address palette entry 80+255, which does not exist. */
static void blit(int x, int y, const unsigned char *px, int w, int h)
{
  if (!px || w <= 0 || h <= 0) return;
  for (int yy = 0; yy < h; yy++) {
    const unsigned char *row = px + yy * w;
    int sx = 0;
    while (sx < w) {
      int v = row[sx], run = 1;
      while (sx + run < w && row[sx + run] == v) run++;
      if (v != 255) fsg_fill(x + sx, y + yy, run, 1, FSG_PIX(v));
      sx += run;
    }
  }
}

static void draw_picture(int x, int y)
{
  blit(x, y, cur_img.px, cur_img.w, cur_img.h);
}

/* ---- the stock itself --------------------------------------------------- */

static void slot_box(int x, int y, int w, int h, int slot)
{
  fsg_fill(x, y, w, h, FSG_SLOT(slot));
}

/*
 * A 35 mm cassette in the film's own colours, with three windows showing how
 * that particular stock renders shadows, midtones and highlights. The colours
 * come from fsg_pal(): brand for the body, box speed for how dark it is, the
 * recipe's white balance for the cast, so Portra and Velvia no longer draw as
 * the same grey ramp.
 */
static void cassette_image(const fsg_shelf_t *s, int x, int y)
{
  const int cw = 78;      /* cassette width  */
  const int cap = 11;     /* spool cap height */
  const int bh = 104;     /* body height      */
  const int by = y + cap;

  fsg_fill(x + 4, y + 5, cw, cap + bh, FSG_BLACK);   /* drop shadow */

  slot_box(x, y, cw, cap, FSG_SL_CAP_HI);
  slot_box(x, y + cap - 4, cw, 4, FSG_SL_CAP_LO);
  fsg_fill(x + 26, y + 2, 26, 5, FSG_SL_CAP_LO);     /* spool hub */

  slot_box(x, by, cw, bh, FSG_SL_BODY);
  slot_box(x + 1, by, 6, bh - 1, FSG_SL_BODY_HI);
  slot_box(x + cw - 10, by + 1, 9, bh - 2, FSG_SL_BODY_LO);
  slot_box(x, by + bh - 1, cw, 1, FSG_SL_BORDER);

  /* label: brand stripe, film code, box speed, DX patches */
  slot_box(x + 6, by + 12, cw - 16, 62, FSG_SL_PAPER);
  slot_box(x + 6, by + 12, cw - 16, 5, FSG_SL_ACCENT);
  fsg_text(x + 11, by + 21, FSG_T_SMALL, FSG_SLOT(FSG_SL_INK),
           FSG_SLOT(FSG_SL_PAPER), "%s", s->stock.code ? s->stock.code : "");
  fsg_text(x + 11, by + 34, FSG_T_MED, FSG_SLOT(FSG_SL_INK),
           FSG_SLOT(FSG_SL_PAPER), "%d", s->stock.iso);
  fsg_text(x + 11, by + 55, FSG_T_SMALL, FSG_SLOT(FSG_SL_INK),
           FSG_SLOT(FSG_SL_PAPER), "135");

  /* film tongue leaving the cassette, perforations and all */
  int ty = by + bh - 26;
  slot_box(x + cw - 2, ty, 30, 18, FSG_SL_TONGUE);
  for (int i = 0; i < 3; i++) {
    slot_box(x + cw + 3 + i * 9, ty + 2, 5, 4, FSG_SL_SPROCKET);
    slot_box(x + cw + 3 + i * 9, ty + 12, 5, 4, FSG_SL_SPROCKET);
  }
}

/* The board the canister sits on: exactly the record size the loader accepts,
 * so the photograph is drawn one pixel per pixel. */
#define PICT_W FSG_CAN_MAXW
#define PICT_H FSG_CAN_MAXH

/*
 * The stock's own canister, in the space the panel keeps for it.
 *
 * Preferred: the manufacturer's photograph from ML/data/FILMCAN.DAT, with its
 * own 170-colour palette loaded into the borrowed overlay entries -- so a Kodak
 * yellow box, a Fuji green one and a red CineStill all look like what is in the
 * hand. No artwork for this stock, no problem: the cassette drawing stands in,
 * in the film's own colours, so the screen never depends on a file being there.
 */
static void shelf_picture(const fsg_shelf_t *s, int x, int y)
{
  const int w = PICT_W, h = PICT_H;

  fsg_fill(x + 4, y + 4, w, h, FSG_BLACK);
  fsg_fill(x, y, w, h, FSG_PANEL_DARK);

  if (cur_img.px) {
    /* centred: the loader guarantees the record fits, so this is exact */
    draw_picture(x + (w - cur_img.w) / 2, y + (h - cur_img.h) / 2);
  } else {
    cassette_image(s, x + (w - 78) / 2, y + (h - 115) / 2);
  }

  fsg_fill(x, y, w, 1, FSG_LINE);
  fsg_fill(x, y + h - 1, w, 1, FSG_LINE);
  fsg_fill(x, y, 1, h, FSG_LINE);
  fsg_fill(x + w - 1, y, 1, h, FSG_LINE);
  /* corner marks, so the frame reads as a slide mount rather than a hole */
  corners(x, y, w, h, FSG_AMBER_DIM, 14);
}

/* ---- shelf ------------------------------------------------------------- */

void fsg_draw_shelf(const fsg_shelf_t *s)
{
  vgrad(0, 0, FSG_W, FSG_H, 0, 0);
  top_bar(s->title && s->title[0] ? s->title : "FILM SHELF", s->badge,
          s->counter, s->tick);

  /* --- one content panel, split by a rule ----------------------------- */
  const int px = 24, py = 56, pw = 672, ph = 282;
  fsg_fill(px + 3, py + 3, pw, ph, FSG_BLACK);
  fsg_fill(px, py, pw, ph, FSG_PANEL);
  fsg_fill(px, py, pw, 1, FSG_LINE);
  fsg_fill(px, py + ph - 1, pw, 1, FSG_LINE);
  fsg_fill(px, py, 1, ph, FSG_LINE);
  fsg_fill(px + pw - 1, py, 1, ph, FSG_LINE);
  corners(px, py, pw, ph, FSG_AMBER_DIM, 18);

  /* Three columns: what the film is on the left, what the recipe does in the
   * middle, and the canister itself on the right at its own 170x240 size, so
   * the photograph is never resampled on the camera. */
  const int divx = 350;
  fsg_fill(divx, py + 16, 1, ph - 32, FSG_LINE_DIM);

  /* left: identity and what the film does */
  const int lx = px + 16, lw = divx - lx - 12;
  fsg_text(lx, py + 12, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "EMULSION");
  fsg_text(lx, py + 30, FSG_T_LARGE, FSG_AMBER, FSG_PANEL, "%s",
           s->stock.name ? s->stock.name : "-");
  fsg_text(lx, py + 64, FSG_T_SMALL, FSG_GREY, FSG_PANEL, "%s  -  %s",
           s->stock.brand ? s->stock.brand : "",
           s->stock.kind ? s->stock.kind : "");
  fsg_text(lx, py + 82, FSG_T_SMALL, FSG_WHITE, FSG_PANEL, "ISO %d  -  135",
           s->stock.iso);
  if (s->photo && s->photo[0])
    fsg_text(lx, py + 100, FSG_T_SMALL, FSG_AMBER, FSG_PANEL, "%s", s->photo);
  else if (!cur_img.px)
    fsg_text(lx, py + 100, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL,
             "no canister art for this stock");

  fsg_fill(lx, py + 122, lw, 1, FSG_LINE_DIM);
  fsg_text(lx, py + 130, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "THE LOOK");
  {
    char lines[6][48];
    int l = fsg_wrap(s->stock.desc ? s->stock.desc : "", lines, 6, lw / 8);
    for (int i = 0; i < l; i++)
      fsg_text(lx, py + 148 + i * 19, FSG_T_SMALL, FSG_WHITE, FSG_PANEL,
               "%s", lines[i]);
  }

  /* middle: the numbers that make it that film, then the recipe */
  {
    const int mx = divx + 12;
    fsg_text(mx, py + 12, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "GRAIN");
    fsg_text(mx, py + 27, FSG_T_MED, FSG_WHITE, FSG_PANEL, "%d%%", s->grain);
    fsg_text(mx, py + 56, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "STRENGTH");
    fsg_text(mx, py + 71, FSG_T_MED, FSG_WHITE, FSG_PANEL, "%d%%",
             s->strength);
    fsg_text(mx, py + 100, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "ROLL");
    fsg_text(mx, py + 115, FSG_T_MED, FSG_WHITE, FSG_PANEL, "%d/%d",
             s->sel + 1, s->count);
    fsg_fill(mx, py + 144, 132, 1, FSG_LINE_DIM);
    fsg_text(mx, py + 152, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL, "RECIPE");
    fsg_text(mx, py + 168, FSG_T_SMALL, FSG_WHITE, FSG_PANEL, "WB %dK",
             s->stock.kelvin);
    fsg_text(mx, py + 186, FSG_T_SMALL, FSG_WHITE, FSG_PANEL,
             "G/M %+d  SAT %+d", s->stock.wbs_gm, s->stock.saturation);
    fsg_text(mx, py + 204, FSG_T_SMALL, FSG_AMBER, FSG_PANEL, "%s",
             s->stock.soup ? s->stock.soup : "Standard C-41");
  }

  /* right: the canister, as the manufacturer printed it */
  {
    const int rx = px + pw - PICT_W - 26;
    fsg_text(rx, py + 4, FSG_T_SMALL, FSG_GREY_DIM, FSG_PANEL,
             "THE FILM ITSELF");
    shelf_picture(s, rx, py + 22);
  }

  film_strip(s);
  /* The footer states the keys this camera actually delivers. */
  hint_row(s->hint_l ? s->hint_l
                     : "press = next film    Q = soup    SET = use",
           s->hint_r ? s->hint_r : "MENU = exit");
}

/* ---- darkroom ---------------------------------------------------------- */

void fsg_draw_shelf_header(const fsg_shelf_t *s)
{
  top_bar(s->title && s->title[0] ? s->title : "FILM SHELF", s->badge,
          s->counter, s->tick);
}

void fsg_draw_shelf_strip(const fsg_shelf_t *s)
{
  film_strip(s);
}

void fsg_draw_develop(const fsg_dev_t *d)
{
  vgrad(0, 0, FSG_W, FSG_H, 0, 0);

  char right[24];
  if (d->total > 0) snprintf(right, sizeof right, "%d / %d", d->index, d->total);
  else snprintf(right, sizeof right, "SINGLE");
  top_bar("DARKROOM", d->result ? "FINISHED" : "DEVELOPING", right, d->tick);

  /* --- tank ----------------------------------------------------------- */
  const int tx = 24, ty = 56, tw = 300, th = 300;
  fsg_fill(tx + 3, ty + 3, tw, th, FSG_BLACK);
  fsg_fill(tx, ty, tw, th, FSG_PANEL_DARK);
  fsg_fill(tx, ty, tw, 1, FSG_LINE);
  fsg_fill(tx, ty + th - 1, tw, 1, FSG_LINE);
  fsg_fill(tx, ty, 1, th, FSG_LINE);
  fsg_fill(tx + tw - 1, ty, 1, th, FSG_LINE);

  int pct = clampi(d->pct, 0, 100);
  int inner_h = th - 16;
  int liquid = (inner_h * pct) / 100;
  int ly = ty + 8 + (inner_h - liquid);
  if (liquid > 0) {
    fsg_fill(tx + 8, ly, tw - 16, liquid, d->done ? FSG_GREEN_DARK : FSG_RED_DEEP);
    /* surface line + a lighter skin just under it */
    fsg_fill(tx + 8, ly, tw - 16, 2, d->done ? FSG_GREEN : FSG_BUBBLE_HI);
    fsg_fill(tx + 8, ly + 2, tw - 16, 6, d->done ? FSG_GREEN_DARK : FSG_RED_MID);
    /* bubbles, deterministic so they animate smoothly */
    for (int i = 0; i < 14; i++) {
      int bx = tx + 14 + ((i * 71 + 23) % (tw - 28));
      int span = liquid > 8 ? liquid - 8 : 1;
      int by = ly + 8 + (int)(((d->tick / 3 + (unsigned)(i * 41)) % (unsigned)span));
      if (by > ty + 8 + inner_h - 6) continue;
      int r = 2 + (i % 3);
      fsg_fill(bx, by, r, r, (i & 1) ? FSG_BUBBLE : FSG_BUBBLE_HI);
    }
  }
  /* tank frame + tick marks every 10% */
  for (int i = 1; i < 10; i++) {
    int y = ty + 8 + (inner_h * i) / 10;
    fsg_fill(tx + tw - 14, y, 8, 1, FSG_LINE_DIM);
  }

  /* big percentage in the middle of the tank */
  {
    char buf[8];
    snprintf(buf, sizeof buf, "%d%%", pct);
    int bw = 118, bh = 46, bx = tx + tw - bw - 14, by = ty + 14;
    fsg_fill(bx, by, bw, bh, FSG_BLACK);
    fsg_fill(bx, by, bw, 1, FSG_AMBER_DIM);
    fsg_fill(bx, by + bh - 1, bw, 1, FSG_AMBER_DIM);
    fsg_fill(bx, by, 1, bh, FSG_AMBER_DIM);
    fsg_fill(bx + bw - 1, by, 1, bh, FSG_AMBER_DIM);
    fsg_text(bx + 14, by + 10, FSG_T_LARGE, FSG_AMBER, FSG_BLACK, "%s", buf);
  }

  /* --- right column --------------------------------------------------- */
  const int rx = 348;
  fsg_text(rx, 58, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG_RED, "FRAME");
  fsg_text(rx, 74, FSG_T_LARGE, FSG_WHITE, FSG_BG_RED, "%s",
           d->photo ? d->photo : "-");

  fsg_text(rx, 112, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG_RED, "EMULSION");
  fsg_text(rx, 126, FSG_T_MED, FSG_AMBER, FSG_BG_RED, "%s",
           d->stock ? d->stock : "-");
  fsg_text(rx, 152, FSG_T_SMALL, FSG_GREY, FSG_BG_RED, "%s  -  ISO %d  -  %s",
           d->brand ? d->brand : "", d->iso, d->soup ? d->soup : "");

  /* stage + bar */
  fsg_text(rx, 190, FSG_T_SMALL, FSG_GREY_DIM, FSG_BG_RED, "STAGE");
  fsg_text(rx, 204, FSG_T_MED, FSG_WHITE, FSG_BG_RED, "%s",
           d->stage ? d->stage : "Starting");
  {
    int bx = rx, by = 236, bw = 348, bh = 18;
    fsg_fill(bx, by, bw, bh, FSG_BLACK);
    fsg_fill(bx, by, bw, 1, FSG_LINE_DIM);
    fsg_fill(bx, by + bh - 1, bw, 1, FSG_LINE_DIM);
    int fw = (bw - 4) * pct / 100;
    if (fw > 0) {
      fsg_fill(bx + 2, by + 2, fw, bh - 4, d->done ? FSG_GREEN_DARK : FSG_RED);
      fsg_fill(bx + 2, by + 2, fw, 2, d->done ? FSG_GREEN : FSG_BUBBLE_HI);
    }
  }

  /* counts, only meaningful for a set */
  if (d->total > 0) {
    char buf[64];
    if (d->done)
      snprintf(buf, sizeof buf, "%d ok   %d skipped   %d failed", d->ok,
               d->skipped, d->failed);
    else
      snprintf(buf, sizeof buf, "frame %d of %d in the bath", d->index, d->total);
    fsg_text(rx, 272, FSG_T_SMALL, FSG_GREY, FSG_BG_RED, "%s", buf);
  }

  /* result line */
  if (d->result) {
    int good = d->failed == 0;
    fsg_fill(24, 372, 672, 56, good ? FSG_GREEN_DARK : FSG_RED_MID);
    fsg_fill(24, 372, 672, 1, good ? FSG_GREEN : FSG_RED);
    fsg_text(40, 388, FSG_T_LARGE, good ? FSG_GREEN : FSG_RED_HI, good ? FSG_GREEN_DARK : FSG_RED_MID,
             "%s", d->result);
  } else {
    fsg_fill(24, 372, 672, 56, FSG_PANEL);
    fsg_fill(24, 372, 672, 1, FSG_LINE_DIM);
    fsg_text(40, 386, FSG_T_MED, FSG_AMBER, 0x101017, "BATH IN PROGRESS");
    fsg_text(40, 410, FSG_T_SMALL, FSG_GREY, 0x101017,
             "Do not switch the camera off until this finishes.");
  }

  hint_row(d->done ? "done - press MENU" : "developing - do not switch off",
           d->total > 1 ? "queue" : "");
}

/*
 * lvverify - does the LiveView pipeline agree with the develop?
 *
 * The liveview path is: RGB -> UYVY 4:2:2 -> unpack -> se_look_rows -> pack.
 * The develop path is:   RGB -> se_look_rows.
 *
 * Both are run on the same real frame here, and the difference between them is
 * reported. Anything beyond a couple of levels is the colour-space round trip
 * doing something it should not, and the preview would no longer predict the
 * JPEG.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "se/engine/se.h"
#include "se/engine/cr2.h"
#include "se/engine/look.h"
#include "se/engine/stock.h"
#include "se/engine/jpeg_dec.h"
#include "se/engine/jpeg_enc.h"
#include "../../fs_yuvlook.h"

static unsigned char *g_rgb;      /* decoded frame, interleaved RGB8 */
static int g_w, g_h, g_rows;

static int collect(void *ctx, const unsigned char *rgb, int nrows, int width)
{
    (void)ctx;
    memcpy(g_rgb + (size_t)g_rows * width * 3, rgb, (size_t)nrows * width * 3);
    g_rows += nrows;
    return 0;
}

static int emit_copy(void *ctx, const unsigned char *row, int width)
{
    unsigned char **p = (unsigned char **)ctx;
    memcpy(*p, row, (size_t)width * 3);
    *p += (size_t)width * 3;
    return 0;
}

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void unpack(const unsigned char *s, unsigned char *o, int width)
{
    for (int x = 0; x + 1 < width; x += 2) {
        /* signed chroma, as the body stores it */
        int u = (signed char)s[0], y0 = s[1], v = (signed char)s[2], y1 = s[3];
        o[0] = clamp8(y0 + ((359 * v + 128) >> 8));
        o[1] = clamp8(y0 - ((88 * u + 183 * v + 128) >> 8));
        o[2] = clamp8(y0 + ((454 * u + 128) >> 8));
        o[3] = clamp8(y1 + ((359 * v + 128) >> 8));
        o[4] = clamp8(y1 - ((88 * u + 183 * v + 128) >> 8));
        o[5] = clamp8(y1 + ((454 * u + 128) >> 8));
        s += 4; o += 6;
    }
}

static void pack(const unsigned char *rgb, unsigned char *uyvy, int width)
{
    for (int x = 0; x + 1 < width; x += 2) {
        const unsigned char *p = rgb + (size_t)x * 3;
        int y0 = (77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8;
        int y1 = (77 * p[3] + 150 * p[4] + 29 * p[5] + 128) >> 8;
        int u = (-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8;
        int v = (128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8;
        uyvy[0] = (unsigned char)(signed char)(u < -128 ? -128 : u > 127 ? 127 : u);
        uyvy[1] = (unsigned char)clamp8(y0);
        uyvy[2] = (unsigned char)(signed char)(v < -128 ? -128 : v > 127 ? 127 : v);
        uyvy[3] = (unsigned char)clamp8(y1);
        uyvy += 4;
    }
}

/* run the look over a whole RGB frame, writing RGB out */
static int grade(const unsigned char *in, unsigned char *out, int w, int h,
                 const se_look *lk, unsigned char *arena_mem, size_t arena_sz)
{
    se_arena arena;
    se_look_run *run = NULL;
    unsigned char *band = malloc((size_t)w * 8 * 3);
    unsigned char *dst = out;

    se_arena_init(&arena, arena_mem, arena_sz);
    if (se_look_begin(&run, lk, w, &arena) != SE_OK) { free(band); return -1; }

    for (int y = 0; y < h; y += 8) {
        int n = (y + 8 <= h) ? 8 : h - y;
        memcpy(band, in + (size_t)y * w * 3, (size_t)n * w * 3);
        se_look_rows(run, band, n, emit_copy, &dst);
    }
    se_look_end(run, emit_copy, &dst);
    free(band);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s file.CR2 stock.fs\n", argv[0]); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *cbuf = malloc(n);
    if (fread(cbuf, 1, n, f) != (size_t)n) return 1;
    fclose(f);

    unsigned char *stext = NULL;
    { FILE *s = fopen(argv[2], "rb");
      fseek(s, 0, SEEK_END); long sn = ftell(s); fseek(s, 0, SEEK_SET);
      stext = malloc(sn);
      if (fread(stext, 1, sn, s) != (size_t)sn) return 1;
      fclose(s);
      se_stock st;
      if (se_stock_parse((const char *)stext, (size_t)sn, &st) != SE_OK) {
          fprintf(stderr, "bad stock\n"); return 1; }
      static se_look lk;
      se_look_from_stock(&st, 1, &lk);
      se_look_fit(&lk, 720, SE_LOOK_BUDGET);

      se_cr2_info info; memset(&info, 0, sizeof info);
      se_cr2_parse(cbuf, (size_t)n, &info);
      uint32_t poff = 0, plen = 0; int pw = 0, ph = 0;
      if (se_cr2_find_preview(cbuf, (size_t)n, &poff, &plen, &pw, &ph) != SE_OK) {
          fprintf(stderr, "no preview\n"); return 1; }

      int SCALE = 2;
      se_jdec_info ji;
      se_jpeg_dec_info(cbuf + poff, plen, &ji);
      se_jpeg_scaled_size(ji.width, ji.height, SCALE, &g_w, &g_h);
      g_rgb = malloc((size_t)g_w * g_h * 3);
      g_rows = 0;

      size_t asz = 4u << 20;
      unsigned char *amem = malloc(asz);
      se_arena arena; se_arena_init(&arena, amem, asz);
      if (se_jpeg_decode_scaled(cbuf + poff, plen, SCALE, &arena,
                                collect, NULL, &ji) != SE_OK) {
          fprintf(stderr, "decode failed\n"); return 1; }

      printf("frame      : %dx%d (scale %d), stock %s\n", g_w, g_h, SCALE, st.name);

      size_t fsz = (size_t)g_w * g_h * 3;
      unsigned char *dev = malloc(fsz);
      unsigned char *lv  = malloc(fsz);
      unsigned char *u   = malloc((size_t)g_w * g_h * 2);
      unsigned char *un  = malloc(fsz);
      unsigned char *lmo = malloc(asz);
      unsigned char *dmo = malloc(asz);

      /* path D: RGB -> look */
      grade(g_rgb, dev, g_w, g_h, &lk, dmo, asz);

      /* path L: RGB -> UYVY -> unpack -> look -> pack -> RGB */
      for (int y = 0; y < g_h; y++) {
          pack(g_rgb + (size_t)y * g_w * 3, u + (size_t)y * g_w * 2, g_w);
          unpack(u + (size_t)y * g_w * 2, un + (size_t)y * g_w * 3, g_w);
      }
      grade(un, lv, g_w, g_h, &lk, lmo, asz);
      for (int y = 0; y < g_h; y++)
          unpack(u + (size_t)y * g_w * 2, un + (size_t)y * g_w * 3, g_w);

      /* baseline: the cost of the colour space alone, with no look at all */
      long bw = 0, bn = 0; double bsum = 0;
      for (size_t i = 0; i < fsz; i++) {
          long d = (long)g_rgb[i] - (long)un[i];
          if (d < 0) d = -d;
          if (d > bw) bw = d;
          bsum += d; bn++;
      }

      long worst = 0, over8 = 0; double sum = 0; long n2 = 0;
      for (size_t i = 0; i < fsz; i++) {
          long d = (long)dev[i] - (long)lv[i];
          if (d < 0) d = -d;
          if (d > worst) worst = d;
          if (d > 8) over8++;
          sum += d; n2++;
      }
      printf("baseline  RGB -> UYVY 4:2:2 -> RGB, no look at all\n");
      printf("  worst %ld   mean %.3f   samples over 8: %.4f%%\n",
             bw, bsum / bn, 100.0 * (double)over8 / (double)bn);
      printf("develop vs liveview, same frame and stock\n");
      printf("  worst %ld   mean %.3f   samples over 8: %.4f%%\n",
             worst, sum / n2, 100.0 * (double)over8 / (double)n2);
      printf((sum / n2) < 1.5 ? "  VERDICT: the liveview path adds no error of its own\n"
                              : "  VERDICT: the liveview path DIVERGES from the develop\n");

      /*
       * The shot-time tables the encoder hook uses: bake them from the same
       * stock, apply them to UYVY in place, and see how far they land from
       * the develop. Grain and halation are excluded on both sides because
       * the tables cannot carry them and a difference there would be a
       * difference in what is being asked, not in how well it is done.
       */
      {
          se_look bare;
          unsigned char *tb  = malloc(fsz);
          unsigned char *uy  = malloc((size_t)g_w * g_h * 2);

          memcpy(&bare, &lk, sizeof bare);
          bare.grain = 0;
          bare.hal_amount = 0;
          grade(g_rgb, dev, g_w, g_h, &bare, dmo, asz);

          if (fs_yuvlook_bake(argv[2], 100) != 0) {
              fprintf(stderr, "bake failed\n");
          } else {
              for (int y = 0; y < g_h; y++)
                  pack(g_rgb + (size_t)y * g_w * 3, uy + (size_t)y * g_w * 2, g_w);
              fs_yuvlook_apply(uy, g_w, g_h, g_w * 2);
              for (int y = 0; y < g_h; y++)
                  unpack(uy + (size_t)y * g_w * 2, tb + (size_t)y * g_w * 3, g_w);

              long tw = 0, t8 = 0; double tsum = 0; long tn = 0;
              for (size_t i = 0; i < fsz; i++) {
                  long d = (long)dev[i] - (long)tb[i];
                  if (d < 0) d = -d;
                  if (d > tw) tw = d;
                  if (d > 8) t8++;
                  tsum += d; tn++;
              }
              printf("baked shot-time tables (%s) vs the develop, grain and halation off\n",
                     fs_yuvlook_name());
              printf("  worst %ld   mean %.3f   samples over 8: %.4f%%\n",
                     tw, tsum / tn, 100.0 * (double)t8 / (double)tn);
          }
          free(tb);
          free(uy);
      }
    }
    return 0;
}

/*
 * fsg_host.c - the two primitives from fsg.h, drawn into a plain RGB buffer.
 *
 * Text is not rasterised here: each call is appended to a JSON list so a short
 * Python pass can composite it with a real font. Everything else is the exact
 * pixel the camera would draw.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui/fsg.h"

unsigned char fsg_fb[FSG_H][FSG_W][3];

/* The camera programs these overlay entries with the current film's colours or
 * with the canister's own palette; here they are just stored, so the preview
 * shows the same pixels fsg_draw_shelf asked for. */
static int fsg_pal_rgb[FSG_PAL_N];

void fsg_pal(const int *rgb, int n)
{
    if (n > FSG_PAL_N) n = FSG_PAL_N;
    for (int i = 0; i < n; i++)
        fsg_pal_rgb[i] = rgb[i];
}

void fsg_pal_reset(void) { }

static int resolve(int rgb)
{
    if (FSG_IS_SLOT(rgb)) return fsg_pal_rgb[rgb & 0xFF];
    return rgb;
}

/* Heap for the strip's thumbnails, same contract as the camera's __mem_malloc. */
void *fsg_alloc(int n)
{
    return n > 0 ? malloc((size_t)n) : NULL;
}

void fsg_free(void *p) { free(p); }

/* File access for gui/fsg_dat.c, same contract as the camera's FIO layer. */
void *fsg_file_open(const char *path, int *size)
{
    FILE *f = fopen(path, "rb");
    if (size) *size = 0;
    if (!f) return NULL;
    if (size) {
        fseek(f, 0, SEEK_END);
        *size = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    return f;
}

int fsg_file_read(void *h, void *dst, int n)
{
    return h ? (int)fread(dst, 1, (size_t)n, (FILE *)h) : 0;
}

int fsg_file_seek(void *h, int off)
{
    return h && fseek((FILE *)h, off, SEEK_SET) == 0;
}

void fsg_file_close(void *h)
{
    if (h) fclose((FILE *)h);
}

struct txt_item {
    int x, y, size, fg, bg;
    char s[128];
};
struct txt_item fsg_txt[512];
int fsg_txt_n = 0;

long fsg_fill_calls;

void fsg_fill(int x, int y, int w, int h, int rgb)
{
    fsg_fill_calls++;
    rgb = resolve(rgb);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FSG_W) w = FSG_W - x;
    if (y + h > FSG_H) h = FSG_H - y;
    if (w <= 0 || h <= 0) return;
    {
        int i, keep = 0;
        for (i = 0; i < fsg_txt_n; i++) {
            struct txt_item *t = &fsg_txt[i];
            if (t->x >= x && t->x < x + w && t->y >= y && t->y < y + h)
                continue;   /* this fill covers its anchor: it is erased */
            fsg_txt[keep++] = *t;
        }
        fsg_txt_n = keep;
    }
    unsigned char r = (unsigned char)((rgb >> 16) & 255);
    unsigned char g = (unsigned char)((rgb >> 8) & 255);
    unsigned char b = (unsigned char)(rgb & 255);
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            fsg_fb[yy][xx][0] = r;
            fsg_fb[yy][xx][1] = g;
            fsg_fb[yy][xx][2] = b;
        }
}

void fsg_text(int x, int y, int size, int fg, int bg, const char *fmt, ...)
{
    if (fsg_txt_n >= (int)(sizeof fsg_txt / sizeof fsg_txt[0])) return;
    struct txt_item *t = &fsg_txt[fsg_txt_n++];
    t->x = x; t->y = y; t->size = size; t->fg = resolve(fg); t->bg = resolve(bg);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->s, sizeof t->s, fmt, ap);
    va_end(ap);
}

void fsg_reset(void) { fsg_txt_n = 0; }

/* Decode a 6D mRAW (3-component lossless JPEG) to PPM, for visual checking. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/cr2.h"
#include "engine/se.h"

static FILE *gin;
static int rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    (void)ctx;
    if (fseek(gin, off, SEEK_SET) != 0) return -1;
    return fread(dst, 1, n, gin) == n ? 0 : -1;
}
static unsigned char *img; static int W, H;
static int sink(void *ctx, int row, const unsigned char *rgb8, int width)
{
    (void)ctx;
    if (row >= H || width != W) return 0;
    memcpy(img + (size_t)row * W * 3, rgb8, (size_t)W * 3);
    return 0;
}
int main(int argc, char **argv)
{
    static unsigned char hdr[256 * 1024];
    se_cr2_info info; se_arena ar; static unsigned char mem[1 << 20];
    se_err e;
    if (argc < 3) { fprintf(stderr, "usage: %s in.CR2 out.ppm\n", argv[0]); return 2; }
    gin = fopen(argv[1], "rb");
    if (!gin) { perror("open"); return 1; }
    size_t n = fread(hdr, 1, sizeof hdr, gin);
    e = se_cr2_parse(hdr, n, &info);
    printf("parse: %s raw %dx%d comps? strip %d+%d\n", se_strerror(e),
           info.raw_width, info.raw_height, info.strip_offset, info.strip_bytes);
    if (e != SE_OK) return 1;
    W = info.raw_width; H = info.raw_height;
    img = calloc((size_t)W * H * 3, 1);
    se_arena_init(&ar, mem, sizeof mem);
    e = se_cr2_decode_mraw(rd, NULL, &info, sink, NULL, &ar);
    printf("mraw decode: %s\n", se_strerror(e));
    if (e != SE_OK) return 1;
    FILE *o = fopen(argv[2], "wb");
    fprintf(o, "P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, (size_t)W * H * 3, o);
    fclose(o);
    printf("wrote %s (%dx%d)\n", argv[2], W, H);
    return 0;
}

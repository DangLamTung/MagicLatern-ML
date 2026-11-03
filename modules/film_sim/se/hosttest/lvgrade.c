/*
 * lvgrade - apply the shot-time look to a buffer the camera actually produced.
 *
 * ML/FILMSIM/LV_RAW.BIN is Canon's own LiveView plane, copied out of VRAM by
 * fs_dump_lv() before this module touched it. This tool turns those bytes into
 * two pictures: the frame as the camera wrote it, and the same bytes after
 * fs_yuvlook_apply() -- the exact function the hook runs on the camera. If the
 * second one is the film look, the UYVY path works on real panel data and not
 * only on a synthetic frame.
 *
 *   ./lvgrade <LV_RAW.BIN> <pitch> <height> <stock.fs> <outprefix> [strength]
 *
 * Writes <outprefix>_canon.ppm and <outprefix>_lut.ppm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../fs_yuvlook.h"

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* UYVY 4:2:2 -> interleaved RGB8, the same conversion lookmatch uses. */
static void unpack(const unsigned char *s, unsigned char *o, int width)
{
    for (int x = 0; x + 1 < width; x += 2) {
        int u = s[0] - 128, y0 = s[1], v = s[2] - 128, y1 = s[3];
        o[0] = clamp8(y0 + ((359 * v + 128) >> 8));
        o[1] = clamp8(y0 - ((88 * u + 183 * v + 128) >> 8));
        o[2] = clamp8(y0 + ((454 * u + 128) >> 8));
        o[3] = clamp8(y1 + ((359 * v + 128) >> 8));
        o[4] = clamp8(y1 - ((88 * u + 183 * v + 128) >> 8));
        o[5] = clamp8(y1 + ((454 * u + 128) >> 8));
        s += 4;
        o += 6;
    }
}

static int write_ppm(const char *path, const unsigned char *uyvy, int pitch,
                     int width, int height)
{
    unsigned char *row = (unsigned char *)malloc((size_t)width * 3);
    FILE *f = fopen(path, "wb");
    if (!f || !row) {
        free(row);
        if (f) fclose(f);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++) {
        unpack(uyvy + (size_t)y * pitch, row, width);
        fwrite(row, 1, (size_t)width * 3, f);
    }
    fclose(f);
    free(row);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s <LV_RAW.BIN> <pitch> <height> <stock.fs> <outprefix> "
                "[strength]\n", argv[0]);
        return 2;
    }
    const char *in = argv[1];
    int pitch  = atoi(argv[2]);
    int height = atoi(argv[3]);
    const char *stock = argv[4];
    const char *prefix = argv[5];
    int strength = (argc > 6) ? atoi(argv[6]) : 100;
    int width = pitch / 2;

    FILE *f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < (long)pitch * height) {
        fprintf(stderr, "%s: %ld bytes, need %d\n", in, sz, pitch * height);
        fclose(f);
        return 1;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)pitch * height);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)pitch * height, f) != (size_t)pitch * height) {
        fclose(f);
        free(buf);
        return 1;
    }
    fclose(f);

    printf("%s: %dx%d pixels, pitch %d, %ld bytes\n", in, width, height, pitch, sz);

    /* Keep the untouched copy for the before/after pair. */
    unsigned char *canon = (unsigned char *)malloc((size_t)pitch * height);
    if (!canon) { free(buf); return 1; }
    memcpy(canon, buf, (size_t)pitch * height);

    char path[512];
    snprintf(path, sizeof path, "%s_canon.ppm", prefix);
    if (write_ppm(path, canon, pitch, width, height) != 0) return 1;
    printf("wrote %s\n", path);

    if (fs_yuvlook_bake(stock, strength) != 0) {
        fprintf(stderr, "bake %s failed\n", stock);
        return 1;
    }
    printf("baked %s at strength %d\n", fs_yuvlook_name(), strength);

    uint32_t ms = fs_yuvlook_apply(buf, width, height, pitch);
    if (ms) printf("applied in %u ms\n", (unsigned)ms);
    snprintf(path, sizeof path, "%s_lut.ppm", prefix);
    if (write_ppm(path, buf, pitch, width, height) != 0) return 1;
    printf("wrote %s\n", path);

    /* How far did the look move the frame? A no-op look is the failure this
     * tool exists to catch, and the mean says so in one number. */
    {
        long long sum = 0;
        int max = 0, n = 0;
        for (long i = 0; i < (long)pitch * height; i++) {
            int d = (int)buf[i] - (int)canon[i];
            if (d < 0) d = -d;
            sum += d;
            if (d > max) max = d;
            n++;
        }
        printf("bytes changed: mean %.2f, max %d over %d bytes\n",
               (double)sum / (double)n, max, n);
    }

    free(canon);
    free(buf);
    return 0;
}

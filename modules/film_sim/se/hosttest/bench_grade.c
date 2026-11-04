/*
 * bench_grade - the two grade engines, side by side on the same frame.
 *
 * One LiveView frame is graded twice:
 *
 *   fs_core_process_rows()  the preview path, tables from fs_presets[]
 *   fs_yuvlook_apply()      the shot path, tables baked from a .fs recipe
 *
 * and the answers compared. They are different looks -- the preview picks one
 * of eight presets by stock and the shot is graded by the recipe -- so this
 * measures both what each costs and how far apart they are, which is the
 * question behind "can LiveView reuse the shot's tables".
 *
 *   ./bench_grade LV_RAW.BIN 1440 480 stock.fs [preset] [iterations]
 *
 * LV_RAW.BIN is Canon's own LiveView plane, 691,200 bytes at 1440 x 480,
 * copied out by fs_dump_lv() before this module touched it.
 *
 * The milliseconds are this PC's, not the 6D's: use the ratio, not the value.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../fs_core.h"
#include "../../fs_yuvlook.h"

extern int fs_yuvlook_bake(const char *stock_path, int strength);

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s LV_RAW.BIN pitch height stock.fs [preset] [n]\n", argv[0]);
        return 2;
    }
    int pitch = atoi(argv[2]);
    int height = atoi(argv[3]);
    const char *stock = argv[4];
    int preset = (argc > 5) ? atoi(argv[5]) : 1;
    int n = (argc > 6) ? atoi(argv[6]) : 20;
    size_t total = (size_t)pitch * (size_t)height;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    unsigned char *src = malloc(total);
    unsigned char *dst = malloc(total);
    unsigned char *alt = malloc(total);
    if (!src || !dst || !alt || fread(src, 1, total, f) != total) {
        fprintf(stderr, "read failed or out of memory\n");
        return 1;
    }
    fclose(f);

    if (fs_yuvlook_bake(stock, 100) != 0) {
        fprintf(stderr, "bake %s failed\n", stock);
        return 1;
    }
    if (preset < 0 || preset >= FS_PRESET_COUNT) preset = 1;

    fs_lut_t lut;
    fs_core_build_luts(&lut, &fs_presets[preset], 100, 0);
    unsigned int rnd = 0x9E3779B9u;   /* the seed the module uses */

    double t0 = now_ms();
    for (int i = 0; i < n; i++)
        fs_core_process_rows(&lut, src, dst, pitch, 0, height, &rnd);
    double t_preview = (now_ms() - t0) / n;

    t0 = now_ms();
    for (int i = 0; i < n; i++) {
        memcpy(alt, src, total);
        fs_yuvlook_apply(alt, pitch / 2, height, pitch);
    }
    double t_shot = (now_ms() - t0) / n;

    long long sum = 0;
    int max = 0, over8 = 0, nsamp = 0;
    for (size_t i = 0; i < total; i++) {
        int d = (int)dst[i] - (int)alt[i];
        if (d < 0) d = -d;
        sum += d;
        if (d > max) max = d;
        if (d > 8) over8++;
        nsamp++;
    }

    printf("frame %dx%d (%zu bytes), stock %s, preset %d\n",
           pitch / 2, height, total, fs_yuvlook_name(), preset);
    printf("  preview  fs_core_process_rows  %7.3f ms/frame (this PC)\n", t_preview);
    printf("  shot     fs_yuvlook_apply+copy %7.3f ms/frame (this PC)\n", t_shot);
    printf("  the two looks differ: mean %.2f, max %d, %.1f%% of samples over 8\n",
           (double)sum / nsamp, max, 100.0 * over8 / nsamp);
    return 0;
}

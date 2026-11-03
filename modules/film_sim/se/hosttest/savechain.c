/* Does the look reach the JPEG through se_cam_save_yuv()?
 *   1. bake the stock exactly as the hook does
 *   2. apply it in place to a real UYVY frame
 *   3. save that buffer through the shipped se_cam_save_yuv()
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "se_cam.h"
#include "../fs_yuvlook.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int w = atoi(argv[3]), h = atoi(argv[4]);
    int stride = w * 2;
    unsigned char *plain = malloc((size_t)stride * h);
    unsigned char *graded = malloc((size_t)stride * h);
    if (!f || fread(plain, 1, (size_t)stride * h, f) != (size_t)stride * h) return 2;
    fclose(f);
    memcpy(graded, plain, (size_t)stride * h);

    if (fs_yuvlook_bake(argv[5], 100) != 0) { printf("bake failed\n"); return 3; }
    printf("baked: '%s'\n", fs_yuvlook_name());
    fs_yuvlook_apply(graded, w, h, stride);

    if (se_cam_save_yuv(plain,  w, h, stride, argv[2], 92) != SE_OK) { printf("save plain failed\n"); return 4; }
    {
        char p2[512]; snprintf(p2, sizeof p2, "%s_graded%s", argv[2], strrchr(argv[2], '.'));
        if (se_cam_save_yuv(graded, w, h, stride, p2, 92) != SE_OK) { printf("save graded failed\n"); return 5; }
        printf("wrote %s and %s\n", argv[2], p2);
    }
    return 0;
}

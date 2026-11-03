/* Does se_cam_keep_yuv() copy exactly the rows se_cam_save_yuv() reads?
 * Encode the same frame twice -- once straight from the source, once from the
 * kept copy -- and require byte-identical files. If they differ, the copy is
 * not the frame the encoder was pointed at, and the _F file would silently be
 * a different picture. */
#include <stdio.h>
#include <stdlib.h>
#include "se_cam.h"
int main(int argc, char **argv)
{
    int w = atoi(argv[3]), h = atoi(argv[4]), step = atoi(argv[5]), asp = atoi(argv[6]);
    int stride = w * 2;
    unsigned char *buf = malloc((size_t)stride * h);
    FILE *f = fopen(argv[1], "rb");
    if (!buf || !f || fread(buf, 1, (size_t)stride * h, f) != (size_t)stride * h) return 2;
    fclose(f);
    se_cam_set_aspect(asp);
    if (se_cam_save_yuv(buf, w, h, stride, argv[2], 92, step) != SE_OK) return 3;
    if (se_cam_keep_yuv(buf, w, h, stride, step) != SE_OK) { printf("keep failed\n"); return 4; }
    printf("kept %dx%d\n", se_cam_kept_w(), se_cam_kept_h());
    if (se_cam_save_kept(argv[7], 92) != SE_OK) return 5;
    return 0;
}

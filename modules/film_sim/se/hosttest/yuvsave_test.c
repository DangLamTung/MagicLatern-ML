#include <stdio.h>
#include <stdlib.h>
#include "se_cam.h"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int w = atoi(argv[3]), h = atoi(argv[4]), stride = w * 2;
    unsigned char *buf = malloc((size_t)stride * h);
    if (!buf || !f || fread(buf, 1, (size_t)stride * h, f) != (size_t)stride * h) return 2;
    fclose(f);
    if (argc > 5) se_cam_set_aspect(atoi(argv[5]));
    { se_err e = se_cam_save_yuv(buf, w, h, stride, argv[2], 92, argc > 6 ? atoi(argv[6]) : 1);
      printf("-> %d %s  out %dx%d\n", (int)e, e ? se_strerror(e) : "ok",
             se_cam_save_last_w(), se_cam_save_last_h()); return e ? 1 : 0; }
}

/* Host tool: run the camera's develop colour path on a standalone JPEG.
 *
 *   ./grade_jpeg photo.jpg stock.fs out.jpg
 */
#include <stdio.h>
#include "se/se_cam.h"

static void note(void *ctx, const char *msg) { (void)ctx; fprintf(stderr, "  engine: %s\n", msg); }
static void progress(void *ctx, const char *stage, int pct) { (void)ctx; (void)stage; (void)pct; }

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s photo.jpg stock.fs out.jpg\n", argv[0]);
        return 2;
    }
    se_err e = se_cam_grade_jpeg(argv[1], argv[2], argv[3], progress, note, NULL);
    printf("%s: %s\n", argv[1], se_strerror(e));
    return e == SE_OK ? 0 : 1;
}

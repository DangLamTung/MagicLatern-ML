/* Host tool: run the camera's develop colour path on a standalone JPEG.
 *
 *   ./grade_jpeg photo.jpg stock.fs out.jpg [lut.bin]
 *
 * The optional LUT is the packed form cube2bin.py writes, applied the way the
 * develop applies it: to the decoded rows, before the look. That is what makes
 * a .fs and its exported .cube comparable on one frame -- fs2cube writes the
 * cube from a recipe, this checks the two land in the same place.
 */
#include <stdio.h>
#include "se/se_cam.h"

static void note(void *ctx, const char *msg) { (void)ctx; fprintf(stderr, "  engine: %s\n", msg); }
static void progress(void *ctx, const char *stage, int pct) { (void)ctx; (void)stage; (void)pct; }

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s photo.jpg stock.fs out.jpg [lut.bin]\n",
                argv[0]);
        return 2;
    }
    /* Before the develop opens the file: the loader caches on this path. */
    se_cam_lut_path(argc > 4 ? argv[4] : NULL);
    se_err e = se_cam_grade_jpeg(argv[1], argv[2], argv[3], progress, note, NULL);
    printf("%s: %s\n", argv[1], se_strerror(e));
    return e == SE_OK ? 0 : 1;
}

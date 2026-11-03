/*
 * hosttest - run the camera develop on a desktop.
 *
 *   make -C .../se/hosttest
 *   ./se_cam_test photo.CR2 stocks/velvia.fs out.jpg
 *
 * The point is not to develop the photo -- tools in soup-engine do that --
 * but to execute se/se_cam.c itself: its 256 KB header window, its arena
 * budget, its callbacks, its output naming and its cleanup on failure. A
 * passing run whose JPEG is byte-identical to the engine's own reference
 * render is the evidence that the camera build's develop path is correct.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "se/se_cam.h"

static void progress(void *ctx, const char *stage, int percent)
{
    (void)ctx;
    if (percent == 0 || percent == 100 || percent % 25 == 0)
        printf("  %-10s %3d%%\n", stage, percent);
}

/* The engine's own log lines, so a test run shows which recipe was loaded
 * rather than asserting it in prose. */
static void note(void *ctx, const char *msg)
{
    (void)ctx;
    printf("  engine: %s\n", msg);
}

int main(int argc, char **argv)
{
    se_err e;

    if (argc < 4) {
        fprintf(stderr, "usage: %s file.CR2 stock.fs out.jpg\n", argv[0]);
        return 2;
    }

    /* optional 4th argument: framing, so the crop can be tested on the host */
    if (argc > 4)
        se_cam_set_aspect(atoi(argv[4]));

    /* optional 5th: "pad", to letterbox a wide framing into a 3:2 frame */
    if (argc > 5 && !strcmp(argv[5], "pad"))
        se_cam_set_pad32(1);

    /* optional 6th: strength of the look, 0..100 */
    if (argc > 6)
        se_cam_set_strength(atoi(argv[6]));

    /* optional 7th: saturation boost, 100 = the stock's own SAT */
    if (argc > 7)
        se_cam_set_satboost(atoi(argv[7]));

    e = se_cam_develop(argv[1], argv[2], argv[3], progress, note, NULL);
    printf("develop: %s\n", se_strerror(e));

    return e == SE_OK ? 0 : 1;
}

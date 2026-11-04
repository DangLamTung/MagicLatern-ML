/*
 * springtest - the width of one animation step, in shelves.
 *
 * The shelf used to ease with `step = d/3` per tick. This runs the spring the
 * shelf now uses (gui/fs_spring.h, the same header the module compiles) at the
 * loop's real rate and prints, per move, how long it takes to arrive and how
 * far the strip travels in its single largest frame.
 *
 * That number is the whole complaint: an exponential ease has no velocity, so
 * its largest step is the first one -- a third of the move, which at three
 * stocks away is a whole shelf width in 70 ms.
 *
 *   ./springtest            the spring, 30 ms frames
 *   ./springtest 3 70       three shelves, 70 ms frames (the old clock)
 */
#include <stdio.h>
#include <stdlib.h>

#include "../../gui/fs_spring.h"

static void run(float shelves, int dt_ms)
{
    fs_spring_t s = {0, 0};
    int target = (int)(shelves * 65536.0f + 0.5f);
    int prev = 0, worst = 0, frames = 0;
    int old_worst = 0, old_frames = 0;

    while (frames < 400) {
        int moving = fs_spring_step(&s, target, dt_ms);
        int step = s.x - prev;
        if (step < 0) step = -step;
        if (step > worst) worst = step;
        prev = s.x;
        frames++;
        if (!moving) break;
    }

    /* the easing this replaced, for the same move on the old clock */
    {
        int d = (int)(shelves * 256.0f + 0.5f);
        while (d != 0 && old_frames < 400) {
            int step = d / 3;
            if (!step) step = d > 0 ? 1 : -1;
            d -= step;
            old_frames++;
            if (step < 0) step = -step;
            if (step > old_worst) old_worst = step;
        }
    }

    printf("%4.0f shelves, %2d ms frames: spring %3d frames (%4d ms)  biggest step %5.1f%%\n",
           shelves, dt_ms, frames, frames * dt_ms, 100.0 * worst / target);
    printf("%4.0f shelves, %2d ms frames: was    %3d frames (%4d ms)  biggest step %5.1f%%\n",
           shelves, 70, old_frames, old_frames * 70,
           100.0 * old_worst / (shelves * 256.0));
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        run((float)atof(argv[1]), atoi(argv[2]));
        return 0;
    }
    run(1, 30);
    run(3, 30);
    run(10, 30);
    printf("\nat the old 70 ms clock:\n");
    run(1, 70);
    run(3, 70);
    return 0;
}

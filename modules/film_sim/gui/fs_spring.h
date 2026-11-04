/*
 * fs_spring.h - the shelf's motion: one damped spring, in fixed point.
 *
 * The strip used to ease with `step = d/3` every tick: exponential, no
 * velocity, so the first tick of a move was already a third of the way there.
 * Measured, three stocks away, the first tick jumped a whole shelf width and
 * then crawled -- 1260 ms to arrive, and it never looked like anything
 * physical.
 *
 * A spring instead: position and velocity, integrated in 8 ms sub-steps so a
 * late frame cannot fling it. Quick turns of the dial pile into one glide
 * because the target moves under an already-moving velocity, which is the
 * whole reason the strip reads smooth.
 *
 * Constants are M-Film's own (ml-dev/filmos/fos_ui.c): 2.1 Hz, zeta 0.86.
 * The integration is theirs too; the arithmetic is integer because this body
 * has no FPU and the shelf runs on the same core as a develop.
 *
 *   w  = 2*pi*2.1                      K1 = w^2   * h   * 2^16 = 91278
 *   h  = 8 ms                          K2 = 2*zeta*w*h  * 2^16 = 11899
 *   zeta = 0.86                        H  = h           * 2^16 = 524
 *
 * One frame of strip travel is 2^16; the strip itself is 108 px per frame, so
 * a unit is 0.0016 px.
 *
 * Settling: within 0.0015 frame and 0.02 frame/s snaps to the target, so the
 * strip is at rest exactly on the selected stock and the caller can stop
 * repainting. Without that the last few units would tick forever.
 *
 * Lives in a header because se/hosttest/springtest.c runs this exact code: the
 * step profile it prints is the evidence, not a re-implementation of it.
 */
#ifndef FS_SPRING_H_INCLUDED
#define FS_SPRING_H_INCLUDED

typedef struct {
  int x; /* position, 2^16 per frame */
  int v; /* velocity, 2^16 frames per second */
} fs_spring_t;

#define FS_SPRING_K1 91278
#define FS_SPRING_K2 11899
#define FS_SPRING_H  524
#define FS_SPRING_QUIET_D 98   /* 0.0015 frame        */
#define FS_SPRING_QUIET_V 1311 /* 0.02 frame per second */

/* Step towards `target` by `dt_ms`. Returns 1 while still moving. */
static inline int fs_spring_step(fs_spring_t *s, int target, int dt_ms)
{
  int left;

  if (!s)
    return 0;
  /* A stalled frame (a develop, a card write) must not become a huge step. */
  if (dt_ms > 60)
    dt_ms = 60;
  if (dt_ms < 0)
    dt_ms = 0;

  for (left = dt_ms; left > 0; left -= 8) {
    long long d = (long long)target - s->x;
    /* Both products overflow int32 at real velocities (11899 * 334347 is
     * 4.0e9), and an int-wrapped damping term turns the spring into an
     * oscillator -- which is exactly what the first version did. */
    s->v += (int)(((long long)FS_SPRING_K1 * d) >> 16)
          - (int)(((long long)FS_SPRING_K2 * s->v) >> 16);
    s->x += (int)(((long long)s->v * FS_SPRING_H) >> 16);
  }

  {
    int d = target - s->x;
    if (d < FS_SPRING_QUIET_D && d > -FS_SPRING_QUIET_D &&
        s->v < FS_SPRING_QUIET_V && s->v > -FS_SPRING_QUIET_V) {
      s->x = target;
      s->v = 0;
      return 0;
    }
  }
  return 1;
}

#endif

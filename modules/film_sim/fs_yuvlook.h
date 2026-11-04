#ifndef _fs_yuvlook_h_
#define _fs_yuvlook_h_

/*
 * The shot-time look, baked from a .fs recipe into three tables and applied to
 * the UYVY frame Canon's encoder is about to read. See fs_yuvlook.c for what
 * this can and cannot express (grain and halation stay in the develop).
 */

/* 0 = baked, ready to apply. `strength` is 0..100, the same slider the
 * develop honours. */
int  fs_yuvlook_bake(const char *stock_path, int strength);

int  fs_yuvlook_ready(void);
const char *fs_yuvlook_name(void);

/* The baked tables, for host tools that plot them. se_yuvtbl lives in
 * engine/look.h, which a caller includes first when it wants the contents. */
#include "se/engine/look.h"
const se_yuvtbl *fs_yuvlook_table(void);

/* Millisecs spent, 0 when nothing was done. */
uint32_t fs_yuvlook_apply(unsigned char *buf, int width, int height, int stride);

#endif

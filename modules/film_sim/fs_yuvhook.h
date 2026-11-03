#ifndef _fs_yuvhook_h_
#define _fs_yuvhook_h_

/*
 * Read-only probe of the frame Canon is about to encode.
 *
 * Florin's Film Roll does not decode the saved JPEG: it patches two firmware
 * entry points and grades the uncompressed YUV buffer in RAM, then lets the
 * camera's own encoder write the file. This module can do the same, but the
 * buffer layout has to be established from the camera itself before anything
 * is written to it -- so this part only observes: it records the register
 * state of the patched functions a few times and copies one band of the
 * candidate buffer into RAM, which film_sim then writes to the card once the
 * shot is over.
 *
 * Nothing in here modifies camera memory other than the two hook jumps, and
 * both are refused unless the word at the address is the expected prologue,
 * so a wrong address cannot be patched.
 */

int  fs_yuvhook_install(void);        /* 0 = at least one site patched */
void fs_yuvhook_uninstall(void);
int  fs_yuvhook_sites(void);          /* how many sites are patched */
int  fs_yuvhook_ready(void);          /* a band is captured and not written */
int  fs_yuvhook_write(const char *path);   /* 1 = written */
/*
 * Grade the captured frame in place. `rows` is the frame height that belongs to
 * the measured stride; 0 disables grading. The time it took is reported by
 * fs_yuvhook_grade_ms() after the dump is written.
 */
void fs_yuvhook_grade(int rows);
uint32_t fs_yuvhook_grade_ms(void);

uint32_t fs_yuvhook_hook_ms(void);  /* get_ms_clock() when the hook last ran */
int fs_yuvhook_hook_shots(void);    /* pictures seen by the hook */
int fs_yuvhook_hook_total_ms(void);     /* duration of the last hook call */
int fs_yuvhook_hook_total_max(void);    /* worst hook call seen */
int fs_yuvhook_dcache_used(void);       /* 1 = graded through the cached alias */

/* Frames graded since install. A count, because the last duration is sticky
 * and cannot say whether the frame in hand was graded. */
int fs_yuvhook_graded(void);

int  fs_yuvhook_calls(void);          /* total hook calls since install */
/* What the last in-shot grade actually touched: the real buffer, its size and
 * the geometry chosen for it. A count of shots is not enough -- see the note in
 * fs_yuvhook.c about a run that reported 3648 rows and changed nothing. */
int  fs_yuvhook_nobuf(void);
uint32_t fs_yuvhook_last_buf(void);
uint32_t fs_yuvhook_last_size(void);
int  fs_yuvhook_last_w(void);
int  fs_yuvhook_last_h(void);
int  fs_yuvhook_last_stride(void);
int  fs_yuvhook_last_rows(void);      /* rows actually graded, not the config */
uint32_t fs_yuvhook_unknown(void);    /* suite size that was not a known plane */
int  fs_yuvhook_chunks(void);         /* chunk count in the last suite */
uint32_t fs_yuvhook_chunk_size(int i);
int  fs_yuvhook_chunk_used(void);     /* index of the chunk chosen, -1 = none */
int  fs_yuvhook_sniff_mask(void);     /* bit i set = chunk i is a picture */
int  fs_yuvhook_suite_at(void);       /* 0 = r1, 1 = r0, 2 = r3, -1 = none */
int  fs_yuvhook_bare_pic(void);       /* 1 = graded a bare DRAM frame pointer */
uint32_t fs_yuvhook_site_reg(int site, int i);  /* site 0 = jpeg, 1 = ssraw */
int  fs_yuvhook_site_diag(int site);
int  fs_yuvhook_pic_quality(void);            /* 1 graded, 2 not a picture,
                                                 * 3 no stride, 4 nothing safe */
int  fs_yuvhook_site_calls(int ssraw);   /* 0 = YuvToJpegPath, 1 = CompleteSsrawToYuv */
int  fs_yuvhook_site_graded(int ssraw);
uint32_t fs_yuvhook_last_pc(void);
uint32_t fs_yuvhook_last_reg(int i);

/*
 * Read-only probe of the still-JPEG path (0xFF16CFB8, the function that logs
 * sssExecuteMem1ToJpegPath). It records the words of the camera's own job
 * struct once per shot and writes them out; it grades nothing.
 */
int  fs_yuvhook_install_mem1(void);
int  fs_yuvhook_mem1_ready(void);
int  fs_yuvhook_mem1_calls(void);
int  fs_yuvhook_enc_calls(void);
int  fs_yuvhook_mem1_write(const char *path);
const char *fs_yuvhook_note(void);    /* one line, for the log */

#endif

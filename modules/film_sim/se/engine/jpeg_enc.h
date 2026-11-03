/*
 * jpeg_enc.h - baseline JPEG encoder, streaming and integer-only.
 *
 * Writes 4:4:4 baseline sequential JPEG. Rows are pushed in 8-line strips
 * so the encoder never holds more than one MCU row, which is what lets the
 * whole pipeline run in a band rather than a frame.
 *
 * Integer throughout: the IJG "islow" DCT, and reciprocal multiplies in
 * place of divides, because the ARM946E-S has neither an FPU nor a divide
 * instruction.
 */
#ifndef SE_JPEG_ENC_H
#define SE_JPEG_ENC_H

#include "se.h"

/* Sink for encoded bytes. Returns 0 on success. */
typedef int (*se_jpeg_sink)(void *ctx, const uint8_t *data, size_t len);

/* One MCU is 16x16 pixels: four 8x8 luma blocks and one 8x8 block each for Cb
 * and Cr, i.e. 4:2:0 -- the sampling every camera JPEG uses. A 4:4:4 stream is
 * legal JPEG but this body's playback decoder will not display it ("cannot
 * playback"), so the encoder does what the camera does. */
/*
 * 16x8 MCU, 4:2:2: two luma blocks side by side, one chroma block each.
 *
 * This is what the body itself writes (IMG_3267.JPG: SOF0 component 1 = 2x1),
 * and 4:2:0 was the one structural difference left after the Exif was fixed.
 * Vertical chroma subsampling bought nothing but a decoder the camera has never
 * had to accept from its own files.
 */
#define SE_JPEG_MCU      16
#define SE_JPEG_MCU_ROWS 8

/*
 * Metadata written as an EXIF APP1 block. Two reasons this matters: the
 * shot keeps its exposure data, and ColorSpace=sRGB is tagged explicitly.
 * An untagged JPEG gets colour-managed by whatever the viewer guesses,
 * which silently changes the picture.
 */
typedef struct {
    const char *make;
    const char *model;
    const char *software;    /* we put the film stock name here */
    const char *datetime;    /* "YYYY:MM:DD HH:MM:SS" */
    float exposure_time;     /* seconds; 0 to omit */
    float aperture;          /* f-number; 0 to omit */
    int   iso;               /* 0 to omit */
    int   exposure_program;  /* 0x8822: 0 = not recorded, else the CR2's own value */
    int   metering_mode;     /* 0x9207: 0 = unknown */
    int   flash;             /* 0x9209: 0 = did not fire */
} se_jpeg_exif;

typedef struct {
    se_jpeg_sink sink;
    void        *ctx;

    int width, height;
    int quality;

    uint8_t  qt[2][64];        /* natural order    */
    uint32_t qrecip[2][64];    /* Q16 reciprocals  */

    /* bit assembly */
    uint32_t bitbuf;
    int      bitcnt;

    int      dcpred[3];

    /* one MCU row of input, 8 lines of RGB8 */
    uint8_t *strip;            /* 8 * width * 3, supplied by the caller */
    int      strip_rows;

    int      rows_done;
    int      failed;

    /*
 * The APP1 payload is padded to this length so the file can be patched in
 * place afterwards with an Exif block that also carries an IFD1 thumbnail.
 * The thumbnail cannot be written when the header goes out -- the picture has
 * not been encoded yet -- and inserting bytes later would mean rewriting the
 * whole file. A fixed-size segment lets us seek back and overwrite it.
 *
 * Canon's playback indexes a JPEG by its Exif thumbnail: a file without IFD1
 * was written to the card but never listed on the camera. That is the bug this
 * reserve exists for.
 */
#define SE_JPEG_EXIF_RESERVE 12288

/* small output staging buffer so the sink sees few, large writes */
    uint8_t  out[4096];
    int      outpos;

    uint8_t  exifbuf[1024];
    int      exif_reserved;   /* the padded APP1 was written at a known offset */
} se_jpeg;

/*
 * This struct is ~5 KB. Allocate it from an arena, never on the stack:
 * Magic Lantern task stacks are small and an overflow there corrupts
 * Canon's memory rather than faulting cleanly.
 */

/*
 * `strip` must point at SE_JPEG_MCU_ROWS * width * 3 bytes of scratch owned by
 * the caller (16 rows, twice what 4:4:4 needed, because chroma is averaged over
 * a 2x2 group and the whole band has to be present before its MCUs can be
 * coded in order). It must stay valid until se_jpeg_end().
 */
se_err se_jpeg_begin(se_jpeg *j, int width, int height, int quality,
                     uint8_t *strip, const se_jpeg_exif *exif,
                     se_jpeg_sink sink, void *ctx);

/* Push `nrows` rows of 8-bit interleaved RGB. Any count is accepted. */
se_err se_jpeg_write_rows(se_jpeg *j, const uint8_t *rgb, int nrows);

se_err se_jpeg_end(se_jpeg *j);

/*
 * Build a complete APP1 payload ("Exif\0\0" + TIFF) that carries IFD0, the
 * Exif sub-IFD and an IFD1 thumbnail: `thumb` is a baseline JPEG of the image
 * at tw x th. Returns the payload length, or 0 if it does not fit in cap.
 */
int se_jpeg_exif_thumb(uint8_t *out, int cap, const se_jpeg_exif *x,
                       int width, int height,
                       const uint8_t *thumb, int tlen, int tw, int th);

/* Encode-stage counters; see jpeg_enc.c. */
extern uint32_t se_je_n_blocks, se_je_n_flat, se_je_n_coef;

#endif /* SE_JPEG_ENC_H */

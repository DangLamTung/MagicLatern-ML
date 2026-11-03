/*
 * Stubs that let se/se_cam.c - the real camera glue, not a copy - be built
 * and run on a desktop. Only the four Magic Lantern file routines are
 * replaced; the window size, arena size, callback plumbing, output naming
 * and error handling all run exactly as they do on the body.
 */
#ifndef HOSTTEST_FIO_ML_H
#define HOSTTEST_FIO_ML_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#define O_SYNC 0

FILE    *FIO_OpenFile(const char *name, unsigned mode);
int      FIO_ReadFile(FILE *f, void *dst, size_t n);
int      FIO_WriteFile(FILE *f, const void *src, size_t n);
void     FIO_CloseFile(FILE *f);
int64_t  FIO_SeekSkipFile(FILE *f, int64_t pos, int whence);
FILE    *FIO_CreateFile(const char *name);
int      FIO_RemoveFile(const char *name);
int      FIO_GetFileSize(const char *name, unsigned int *size);

#endif

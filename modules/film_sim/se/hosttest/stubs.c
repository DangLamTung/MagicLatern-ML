#include <stdio.h>
#include <stdint.h>

#include "fio-ml.h"

FILE *FIO_OpenFile(const char *p, unsigned m) { (void)m; return fopen(p, "rb"); }
int   FIO_ReadFile(FILE *f, void *d, size_t n) { return (int)fread(d, 1, n, f); }
int   FIO_WriteFile(FILE *f, const void *d, size_t n) { return (int)fwrite(d, 1, n, f); }
void  FIO_CloseFile(FILE *f) { fclose(f); }
int64_t FIO_SeekSkipFile(FILE *f, int64_t pos, int whence)
{
    return fseeko(f, pos, whence) ? -1 : ftello(f);
}
FILE *FIO_CreateFile(const char *p) { return fopen(p, "wb"); }
int   FIO_RemoveFile(const char *p) { return remove(p); }
int   FIO_GetFileSize(const char *p, unsigned int *size)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    *size = (unsigned int)ftell(f);
    fclose(f);
    return 0;
}

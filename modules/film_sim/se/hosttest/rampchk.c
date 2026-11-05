/* rampchk: count luminance inversions on a neutral ramp, per stock.
 * A film curve is monotonic; if the output luma goes backwards as the input
 * rises, something downstream of the curve is modifying neutral grey. */
#include <stdio.h>
#include <string.h>
#include "se/engine/stock.h"
#include "se/engine/look.h"
int main(int argc, char **argv)
{
  int a, total = 0;
  for (a = 1; a < argc; a++) {
    FILE *f = fopen(argv[a], "rb");
    char text[8192];
    size_t n;
    se_stock st;
    se_look lk;
    se_yuvtbl t;
    int i, inv = 0, worst = 0;
    if (!f) continue;
    n = fread(text, 1, sizeof text, f);
    fclose(f);
    if (se_stock_parse(text, n, &st) != SE_OK) continue;
    se_look_from_stock(&st, 1, &lk);
    se_look_bake_yuv(&lk, &t);
    for (i = 1; i < 256; i++)
      if (t.y[i] < t.y[i - 1]) {
        inv++;
        if (t.y[i - 1] - t.y[i] > worst) worst = t.y[i - 1] - t.y[i];
      }
    total += inv;
    if (inv) printf("%-16s inversions %3d  worst -%d\n", st.name, inv, worst);
  }
  printf("TOTAL inversions across %d stocks: %d\n", argc - 1, total);
  return 0;
}

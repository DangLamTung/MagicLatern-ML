/*
 * creatorwrite - does the creator's .fs survive the engine's own parser?
 *
 * Builds a state, formats it, parses it back with se_stock_parse and compares
 * all twelve values of all three hue vectors. A lossy writer would be a
 * twelve-colour wheel that silently becomes a six-colour one at save time,
 * which is the fault the engine change was made to avoid.
 *
 *   ./creatorwrite
 */
#include <stdio.h>
#include <string.h>

#include "../../gui/fsg_creator.h"

#include "../engine/se.h"
#include "../engine/stock.h"

int main(void)
{
    fs_creator_t c;
    char buf[2048];
    se_stock st;
    int i, fail = 0, n;

    fs_creator_defaults(&c, "BASE: CLASSIC CHROME");

    /* every wedge different, so a mix-up between them cannot pass */
    for (i = 0; i < FS_CREATOR_WEDGES; i++) {
        c.sat[i] = 60 + i * 7;          /* 60 .. 137 */
        c.rot[i] = -30 + i * 5;         /* -30 .. 25 */
        c.lum[i] = 70 + i * 3;          /* 70 .. 103 */
    }
    c.shadow = 12;
    c.highlight = -8;

    n = fs_creator_write(&c, buf, sizeof buf, "WHEELTEST");
    if (n < 0) { puts("write failed"); return 1; }
    printf("wrote %d bytes:\n----\n%s----\n\n", n, buf);

    if (se_stock_parse(buf, (size_t)n, &st) != SE_OK) {
        puts("FAIL: the engine's parser rejected it");
        return 1;
    }
    printf("parsed as '%s', %d sectors\n", st.name,
           (int)(sizeof st.hue_sat / sizeof st.hue_sat[0]));

    if (strcmp(st.name, "WHEELTEST")) { puts("FAIL: NAME lost"); fail = 1; }

#define CHECK(vec, field, label) do {                                        \
        for (i = 0; i < FS_CREATOR_WEDGES; i++) {                            \
            int want = c.field[i];                                           \
            float f  = st.vec[i];                                            \
            int got  = (int)(f >= 0.0f ? f + 0.5f : f - 0.5f);               \
            if (got != want) {                                               \
                printf("FAIL: %s[%d] wrote %d, engine read %d\n",             \
                       label, i, want, got);                                 \
                fail = 1;                                                    \
            }                                                                \
        }                                                                    \
    } while (0)

    CHECK(hue_sat, sat, "SAT");
    CHECK(hue_rot, rot, "ROT");
    CHECK(hue_lum, lum, "LUM");
#undef CHECK

    printf("%s: 36 values round-tripped\n", fail ? "FAILED" : "PASS");
    return fail;
}

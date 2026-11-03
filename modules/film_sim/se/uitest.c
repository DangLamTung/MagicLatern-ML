/*
 * Host renderer for the ported Soup Engine UI.
 *
 * Renders the film picker with OUR stock table and writes the same 4bpp
 * overlay buffer the camera would get, so the layout can be reviewed and
 * bounds-checked before it ever runs on a 6D.  Must be compiled with
 * SE_HOSTED; it is not part of the module build.
 *
 *   cc -std=c99 -DSE_HOSTED -I../se -o uitest uitest.c ../se/ui.c ../se/ui_font.c
 *   ./uitest outdir
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../se/ui.h"

/* the stocks the module will offer, taken from film_table.h at build time */
#include "../../film_table.h"

static uint8_t fb_buf[SE_UI_FB_SIZE];

static void write_fb(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.fb", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite(fb_buf, 1, SE_UI_FB_SIZE, f);
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "out";
    mkdir(dir, 0755);

    static se_ui_stock stocks[FILM_COUNT];
    for (int i = 0; i < FILM_COUNT; i++)
    {
        stocks[i].name = film_table[i].name;
        stocks[i].desc = film_table[i].description;
    }

    se_ui ui;
    se_ui_init(&ui);
    se_ui_fb fb = { fb_buf };
    se_ui_set_stocks(&ui, stocks, FILM_COUNT);

    /* every stock on the picker, so each icon and description is drawn */
    for (int i = 0; i < FILM_COUNT; i++)
    {
        ui.stock = i;
        ui.screen = SE_SCREEN_STOCK;
        se_ui_draw(&ui, &fb);
        char nm[64];
        snprintf(nm, sizeof nm, "stock_%02d", i);
        write_fb(dir, nm);
    }

    /* the other three screens */
    ui.screen = SE_SCREEN_SETTINGS;
    se_ui_draw(&ui, &fb);   write_fb(dir, "settings");

    ui.screen = SE_SCREEN_WORKING;
    ui.percent = 62; ui.stage = "DEMOSAIC"; ui.done = 4; ui.total = 12;
    snprintf(ui.file, sizeof ui.file, "IMG_1234.CR2");
    se_ui_draw(&ui, &fb);   write_fb(dir, "working");

    ui.screen = SE_SCREEN_DONE;
    ui.last_seconds = 486;
    se_ui_draw(&ui, &fb);   write_fb(dir, "done");

    /* bounds check: every pixel the UI writes must be inside the buffer */
    printf("\n%d stocks rendered, 4 screens\n", FILM_COUNT);
    return 0;
}

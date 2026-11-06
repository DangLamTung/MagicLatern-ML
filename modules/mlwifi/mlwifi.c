/*
 * mlwifi - reach the 6D's own WiFi stack.
 *
 * STATUS: ladder rung 1. This module does not transfer anything yet. Read
 * docs/wifi-functions.md before touching it; the short version:
 *
 *   - the camera has a complete WiFi stack (NWCOM: FTP transfer, discovery,
 *     connection monitoring, WFT HTTP) and Canon's own GUI drives it
 *   - none of it is exported in 6D_116.sym, so no module can import it by name
 *   - the addresses below were found by disassembly of ROM1 at 0xFF000000,
 *     base and md5 recorded in docs/wifi-functions.md
 *   - every WiFi function is identified by the strings it logs through
 *     Canon's routine at 0x67C8, passed PC-relative, which is why no pointer
 *     to any of those strings exists in the image
 *
 * The code here deliberately CALLS NOTHING. Rung 1 is: report that the address
 * is there and what surrounds it. Turning an integer into a function pointer
 * and jumping to it without knowing the signature is how a camera gets bricked.
 *
 * Next rung, with the evidence this module prints: call NWCOM_Init and log its
 * return. That function names itself in its own log output, so a return value
 * plus the NWCOM lines appearing in FILMSIM.LOG would be the first proof the
 * stack can be driven at all.
 */
#include "dryos.h"
#include "module.h"
#include "menu.h"
#include "bmp.h"
#include "config.h"

/* Addresses from the ROM1 disassembly. See docs/wifi-functions.md. */
#define MLW_NWCOM_INIT      0xFF261714u   /* [NWCOM] NWCOM Init */
#define MLW_NWCOM_XMITCBR   0xFF2616E8u   /* TransmissionChangeCBR */
#define MLW_NWCOM_STATUSCBR 0xFF261B00u   /* NetworkStatusCBR */
#define MLW_NWCOM_WIZARD    0xFF261F70u   /* GUID / FriendlyName / discovery */
#define MLW_NWCOM_ORDER     0xFF262358u   /* transfer order handler */
#define MLW_NWCOM_FTP       0xFF261974u   /* FTP transfer control */
#define MLW_NWCOM_CONN      0xFF2634B8u   /* ChangeLogicalConnect */
#define MLW_NWCOM_RECONN    0xFF2638C4u   /* BeaconLoss / ReConnect */
#define MLW_NWCOM_GUIEVT    0xFF266334u   /* NWCOM GUIEvent */
#define MLW_LOG_CALL        0x000067C8u   /* Canon's logging routine */

/* Flash window only: every address above is in it, and reading unmapped RAM
 * is how a module takes the camera down. */
#define MLW_FLASH_LO 0xFF000000u
#define MLW_FLASH_SPAN 0x01000000u

static int mlw_readable(uint32_t addr)
{
    return (uint32_t)(addr - MLW_FLASH_LO) < MLW_FLASH_SPAN;
}

static uint32_t mlw_first_word(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

static void mlw_probe_task(void *unused)
{
    (void)unused;
    struct { const char *name; uint32_t addr; } t[] = {
        { "NWCOM_Init",      MLW_NWCOM_INIT     },
        { "TransmissionCBR", MLW_NWCOM_XMITCBR  },
        { "StatusCBR",       MLW_NWCOM_STATUSCBR},
        { "Wizard",          MLW_NWCOM_WIZARD   },
        { "Order",           MLW_NWCOM_ORDER    },
        { "FTP",             MLW_NWCOM_FTP      },
        { "LogicalConnect",  MLW_NWCOM_CONN     },
        { "ReConnect",       MLW_NWCOM_RECONN   },
        { "GUIEvent",        MLW_NWCOM_GUIEVT   },
    };
    int i, ok = 0;

    printf("probe: begin");
    for (i = 0; i < COUNT(t); i++) {
        if (!mlw_readable(t[i].addr)) {
            printf("probe: %s 0x%08x outside the flash window", t[i].name, t[i].addr);
            continue;
        }
        /* first word of each function: the ARM prologue. 0xE92Dxxxx is
         * `push {..., lr}`, which is what every one of these starts with. */
        uint32_t w = mlw_first_word(t[i].addr);
        int prologue = (w & 0x0FFF0000u) == 0x092D0000u && (w & 0x4000u);
        printf("probe: %-16s 0x%08x first word 0x%08x %s",
                t[i].name, t[i].addr, w, prologue ? "prologue" : "NOT a prologue");
        if (prologue) ok++;
    }
    printf("probe: %d of %d look like functions; logging routine at 0x%08x",
            ok, COUNT(t), MLW_LOG_CALL);
    NotifyBox(2000, "mlwifi: %d/%d WiFi functions found", ok, COUNT(t));
}

static void mlw_probe(void *priv, int delta)
{
    (void)priv; (void)delta;
    task_create("mlw_probe", 0x1e, 0x1000, mlw_probe_task, 0);
}

static struct menu_entry mlwifi_menu[] = {
    {
        .name = "Probe WiFi",
        .select = mlw_probe,
        .icon_type = IT_ACTION,
        .help = "Check that the nine WiFi functions found by disassembly are "
                "present and start with an ARM prologue. Calls nothing, so the "
                "worst case is a note in the log. Read docs/wifi-functions.md "
                "for what each address is and how it was found.",
    },
    MENU_EOL,
};

static unsigned int mlwifi_init(void)
{
    menu_add("WiFi", mlwifi_menu, COUNT(mlwifi_menu) - 1);
    return 0;
}

static unsigned int mlwifi_deinit(void)
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(mlwifi_init)
    MODULE_DEINIT(mlwifi_deinit)
MODULE_INFO_END()

/* fw.h - the firmware calls gbc.c needs that are not in the mocor-sdk API table. The addresses belong to the mmi_app
 * image that mocor-sdk patches: see docs/api-addresses.md there. */
#ifndef FW_H
#define FW_H
#include <stdint.h>
#include "f286_api.h"

#define FW_T(a, type) ((type)((a) | 1u))   /* Thumb function in mmi_app */
#define FW_A(a, type) ((type)(a))          /* ARM veneer */

#define FW_MS_NOW() (*(volatile const uint32_t *)0x4023000cu)   /* free-running ms counter */

static inline uint32_t fw_backlight_state(void)
{
    return *(const volatile uint32_t *)(0x81704134u + 0x24u);
}
static inline int fw_get_draw_param(const f286_api_t *api, uint32_t *pitch, uint32_t *w, uint32_t *h, uint16_t **buf)
{
    GUI_LCD_DEV_INFO dev;
    (void)api;
    dev.lcd_id = 0;
    dev.block_id = 0;
    return FW_T(0x88247ba0u, int (*)(const GUI_LCD_DEV_INFO *, uint32_t *, uint32_t *, uint32_t *, uint16_t **))
        (&dev, pitch, w, h, buf);
}
static inline uint32_t fw_layer_type(const f286_api_t *api)    /* 3 = RGB565 */
{
    GUI_LCD_DEV_INFO dev;
    (void)api;
    dev.lcd_id = 0;
    dev.block_id = 0;
    return FW_T(0x880873ecu, uint32_t (*)(const GUI_LCD_DEV_INFO *))(&dev);
}
/* inclusive rect, as the firmware wants it */
static inline void fw_store_update_rect(const f286_api_t *api, int l, int t, int r, int b)
{
    GUI_LCD_DEV_INFO dev;
    GUI_RECT_T rc;
    (void)api;
    dev.lcd_id = 0;
    dev.block_id = 0;
    rc.left = (int16_t)l; rc.top = (int16_t)t; rc.right = (int16_t)r; rc.bottom = (int16_t)b;
    FW_T(0x88067788u, void (*)(const GUI_LCD_DEV_INFO *, GUI_RECT_T))(&dev, rc);
}
#endif

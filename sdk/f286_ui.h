/* f286_ui.h - app-side helpers over the v3 stock-UI API (no stub changes needed to edit).
 * Rules (phone-relevant):
 *  - Never initialise a struct holding a pointer to a literal with a constant initialiser
 *    (`MMI_STRING_T s = { u"x", 1 }`): with -fropi that bakes an unrelocated address. Assign
 *    the fields at run time (ui_str) instead.
 *  - Keep everything naturally aligned: the kernel sets SCTLR.A, so an unaligned access resets
 *    the phone. build.py passes +strict-align (zig cc ignores -mno-unaligned-access). */
#ifndef F286_UI_H
#define F286_UI_H

#include "f286_api.h"
#include "app_rt.h"

static inline void ui_str(MMI_STRING_T *s, const uint16_t *w) { s->wstr_ptr = (uint16_t *)w; s->wstr_len = wlen(w); }

/* window table builder: ui_t t; ui_begin(&t, handler, id); ui_title(...); ui_softkey(...); ui_list(...) */
typedef struct { uint32_t w[24]; int n; } ui_t;
static inline void ui_put(ui_t *t, uint32_t v) { t->w[t->n++] = v; }
static inline void ui_begin(ui_t *t, MMI_WIN_FUNC handler, uint32_t win_id)
{
    t->n = 0;
    ui_put(t, F286_WT_FUNC); ui_put(t, (uint32_t)handler);
    ui_put(t, F286_WT_ID); ui_put(t, win_id);
}
static inline void ui_softkey(ui_t *t, uint32_t l, uint32_t m, uint32_t r)
{ ui_put(t, F286_WT_SOFTKEY); ui_put(t, l); ui_put(t, m); ui_put(t, r); }
static inline void ui_title(ui_t *t, uint32_t text_id) { ui_put(t, F286_WT_TITLE); ui_put(t, text_id); }
static inline void ui_list(ui_t *t, uint32_t ctrl) { ui_put(t, F286_WT_LISTBOX); ui_put(t, F286_LIST_TEXT); ui_put(t, ctrl); }
static inline void ui_text(ui_t *t, uint32_t ctrl) { ui_put(t, F286_WT_TEXT); ui_put(t, ctrl); }
static inline void ui_edit(ui_t *t, uint32_t max_len, uint32_t ctrl) { ui_put(t, F286_WT_EDIT_TEXT); ui_put(t, max_len); ui_put(t, ctrl); }
/* kind: F286_WT_EDIT_TEXT, F286_WT_EDIT_PHONENUM or F286_WT_EDIT_DIGITAL */
static inline void ui_edit_kind(ui_t *t, uint32_t kind, uint32_t max_len, uint32_t ctrl) { ui_put(t, kind); ui_put(t, max_len); ui_put(t, ctrl); }
static inline MMI_HANDLE_T ui_open(const f286_api_t *api, ui_t *t) { ui_put(t, F286_WT_END); return api->MMK_CreateWin(t->w, 0); }

/* list: call ui_list_begin in MSG_OPEN_WINDOW, then ui_list_add per row, then ui_list_end.
 * sk_* are the softkey text ids shown while the row is focused (0 = F286_TXT_NULL). */
static inline void ui_list_begin(const f286_api_t *api, uint32_t ctrl, uint32_t max_items)
{ api->GUILIST_SetMaxItem(ctrl, max_items, 0); }
static inline void ui_list_add(const f286_api_t *api, uint32_t ctrl, const uint16_t *text,
                               uint32_t sk_l, uint32_t sk_m, uint32_t sk_r)
{
    GUILIST_ITEM_T item;
    GUILIST_ITEM_DATA_T data;
    zero(&item, sizeof item);
    zero(&data, sizeof data);
    data.content[0].ptr_or_id = text;
    data.content[0].len = wlen(text);
    data.content[0].type = 2;
    data.softkey_id[0] = sk_l ? sk_l : F286_TXT_NULL;
    data.softkey_id[1] = sk_m ? sk_m : F286_TXT_NULL;
    data.softkey_id[2] = sk_r ? sk_r : F286_TXT_NULL;
    item.style_ptr = api->GUILIST_GetStyle(F286_LIST_STYLE_TEXT);
    item.data = &data;
    api->GUILIST_AppendItem(ctrl, &item);
}
static inline void ui_list_end(const f286_api_t *api, MMI_HANDLE_T win, uint32_t ctrl)
{ api->MMK_SetAtvCtrl(win, ctrl); }

/* message box with up to two lines; image 0x400000f1 is the stock warning icon */
static inline void ui_alert(const f286_api_t *api, const uint16_t *line1, const uint16_t *line2)
{
    MMI_STRING_T a, b;
    ui_str(&a, line1);
    if (line2) ui_str(&b, line2);
    api->MMIPUB_OpenAlertWinByTextPtr(0, &a, line2 ? &b : 0, 0x400000f1u, 0, 0, 1, 0);
}
/* the same, shown for *time_ms (0 = the stock default, under a second on the phone) */
static inline void ui_alert_ms(const f286_api_t *api, const uint32_t *time_ms, const uint16_t *line1, const uint16_t *line2)
{
    MMI_STRING_T a, b;
    ui_str(&a, line1);
    if (line2) ui_str(&b, line2);
    api->MMIPUB_OpenAlertWinByTextPtr(time_ms, &a, line2 ? &b : 0, 0x400000f1u, 0, 0, 1, 0);
}
#endif

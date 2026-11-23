/*
 * Wasp Nest — P4 UI (LVGL) — "cyberpunk-kawaii" theme
 *
 * A STATIC splash (no scrolling): a central wasp core with tool nodes wired to
 * it by neon lines over a deep-indigo field. Pastel-neon palette (magenta / cyan
 * / mint) — deliberately distinct from Honeycomb's honey comb. Tapping a node
 * opens a full-screen tool overlay with a back button.
 *
 * Threading: every LVGL touch is wrapped in bsp_display_lock()/unlock().
 */
#include "hc_ui.h"
#include "hc_mode_manager.h"
#include "hc_wifi_recon.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"

#if __has_include("bsp/esp-bsp.h")
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

#define TAG "hc_ui"

/* ---- cyberpunk-kawaii palette ---- */
#define HC_BG       0x0C0A17   /* deep indigo-black field        */
#define HC_HDR      0x0C0A17   /* header                          */
#define HC_PANEL    0x1A1333   /* panel / chip violet             */
#define HC_HONEY    0xFF49D1   /* primary — neon magenta          */
#define HC_AMBER    0xFFC24F   /* warn / "waiting"                */
#define HC_CYAN     0x56E0FF   /* accent — neon cyan              */
#define HC_MINT     0x7CF5C2   /* third accent — kawaii mint      */
#define HC_DIMLINE  0x3A2E66   /* node connector lines            */
#define HC_LHONEY   0xFFC7F0   /* light-pink label                */
#define HC_LCYAN    0xC2F2FF   /* light-cyan label                */
#define HC_DIMTXT   0x9186C2   /* muted lavender text             */
#define HC_STRIPE   0x241A46   /* mascot dark stripe              */
#define HC_NODE     0x160F2E   /* node / core fill                */

static bool s_active;

#if HAVE_LVGL

#define HDR_H   70

static lv_obj_t *s_mode_lbl;
static lv_obj_t *s_frames_lbl;
static uint32_t  s_frame_count;

enum { TOOL_WIFI, TOOL_BLE, TOOL_EXPLORER, TOOL_CAPTURE, TOOL_BENCH, TOOL_CONFIG };

typedef struct {
    const char *icon;
    const char *name;
    uint32_t    color;
    uint32_t    lcolor;
    int         id;
} hc_node_t;

/* Six nodes orbit the core, colour-cycled magenta / cyan / mint. */
static const hc_node_t s_nodes[] = {
    {LV_SYMBOL_WIFI,      "wi-fi",    HC_HONEY, HC_LHONEY, TOOL_WIFI},
    {LV_SYMBOL_BLUETOOTH, "ble",      HC_CYAN,  HC_LCYAN,  TOOL_BLE},
    {LV_SYMBOL_SHUFFLE,   "802.15.4", HC_MINT,  HC_LCYAN,  TOOL_EXPLORER},
    {LV_SYMBOL_SAVE,      "capture",  HC_HONEY, HC_LHONEY, TOOL_CAPTURE},
    {LV_SYMBOL_DOWNLOAD,  "bench",    HC_CYAN,  HC_LCYAN,  TOOL_BENCH},
    {LV_SYMBOL_SETTINGS,  "config",   HC_MINT,  HC_LCYAN,  TOOL_CONFIG},
};
#define N_NODES (sizeof(s_nodes) / sizeof(s_nodes[0]))

/* Connector-line point storage must persist (LVGL keeps the pointer). */
static lv_point_precise_t s_npts[N_NODES][2];

static lv_obj_t *bare(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

static lv_obj_t *dot(lv_obj_t *p, int sz, int x, int y, uint32_t col, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, sz, sz);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

/* A cute wasp, drawn from primitives, centred in a d x d core. */
static void build_wasp(lv_obj_t *core, int d)
{
    int cx = d / 2;

    /* wings (translucent cyan) */
    dot(core, 34, cx - 42, 44, HC_CYAN, LV_OPA_40);
    dot(core, 34, cx + 8,  44, HC_CYAN, LV_OPA_40);

    /* body */
    lv_obj_t *body = lv_obj_create(core);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 62, 76);
    lv_obj_set_pos(body, cx - 31, 56);
    lv_obj_set_style_radius(body, 28, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(HC_HONEY), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);

    for (int i = 0; i < 2; i++) {       /* stripes */
        lv_obj_t *s = lv_obj_create(body);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 62, 9);
        lv_obj_set_pos(s, 0, 22 + i * 20);
        lv_obj_set_style_bg_color(s, lv_color_hex(HC_STRIPE), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    }

    /* kawaii face: eyes + rosy cheeks */
    dot(core, 10, cx - 16, 78, HC_STRIPE, LV_OPA_COVER);
    dot(core, 10, cx + 6,  78, HC_STRIPE, LV_OPA_COVER);
    dot(core, 12, cx - 24, 92, 0xFF8FD8, LV_OPA_50);
    dot(core, 12, cx + 12, 92, 0xFF8FD8, LV_OPA_50);
}

static lv_obj_t *status_chip(lv_obj_t *parent, const char *label, const char *val, uint32_t vcol)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 96, 46);
    lv_obj_set_style_bg_color(chip, lv_color_hex(HC_PANEL), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 12, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x2E2452), 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_all(chip, 4, 0);

    lv_obj_t *l = lv_label_create(chip);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_hex(HC_DIMTXT), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *v = lv_label_create(chip);
    lv_label_set_text(v, val);
    lv_obj_set_style_text_color(v, lv_color_hex(vcol), 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, 0);
    return v;
}

/* ---- tool views: tap a node -> full-screen overlay with a back button ---- */
static lv_obj_t  *s_overlay;
static bool       s_tool_pending;
static lv_timer_t *s_tool_timer;
static lv_obj_t  *s_dash_psram;
static lv_obj_t  *s_dash_heap;
static lv_obj_t  *s_dash_uptime;
static lv_obj_t  *s_wifi_tbl;

static void close_tool(lv_event_t *e)
{
    (void)e;
    if (s_tool_timer) { lv_timer_delete(s_tool_timer); s_tool_timer = NULL; }
    if (s_overlay)    { lv_obj_delete_async(s_overlay); s_overlay = NULL; }
    s_dash_psram = s_dash_heap = s_dash_uptime = NULL;
    s_wifi_tbl = NULL;
    s_tool_pending = false;
}

static lv_obj_t *tool_shell(const char *title)
{
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(HC_BG), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(s_overlay);
    lv_obj_set_size(back, 120, 48);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 12);
    lv_obj_set_style_bg_color(back, lv_color_hex(HC_PANEL), 0);
    lv_obj_set_style_border_color(back, lv_color_hex(HC_HONEY), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_add_event_cb(back, close_tool, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  back");
    lv_obj_set_style_text_color(bl, lv_color_hex(HC_HONEY), 0);
    lv_obj_center(bl);

    lv_obj_t *tt = lv_label_create(s_overlay);
    lv_label_set_text(tt, title);
    lv_obj_set_style_text_color(tt, lv_color_hex(HC_HONEY), 0);
    lv_obj_align(tt, LV_ALIGN_TOP_LEFT, 160, 24);
    return s_overlay;
}

static lv_obj_t *dash_row(lv_obj_t *parent, const char *k, const char *v, int y)
{
    lv_obj_t *kl = lv_label_create(parent);
    lv_label_set_text(kl, k);
    lv_obj_set_style_text_color(kl, lv_color_hex(HC_DIMTXT), 0);
    lv_obj_align(kl, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *vl = lv_label_create(parent);
    lv_label_set_text(vl, v);
    lv_obj_set_style_text_color(vl, lv_color_hex(HC_LHONEY), 0);
    lv_obj_align(vl, LV_ALIGN_TOP_LEFT, 200, y);
    return vl;
}

static void dash_timer_cb(lv_timer_t *t)
{
    (void)t;
    char buf[48];
    if (s_dash_psram) {
        snprintf(buf, sizeof(buf), "%u / %u KB",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
        lv_label_set_text(s_dash_psram, buf);
    }
    if (s_dash_heap) {
        snprintf(buf, sizeof(buf), "%u KB free",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        lv_label_set_text(s_dash_heap, buf);
    }
    if (s_dash_uptime) {
        unsigned long s = (unsigned long)(esp_timer_get_time() / 1000000ULL);
        snprintf(buf, sizeof(buf), "%luh %02lum %02lus", s / 3600, (s % 3600) / 60, s % 60);
        lv_label_set_text(s_dash_uptime, buf);
    }
}

static void open_config(void)
{
    lv_obj_t *ov = tool_shell("device");
    lv_obj_t *body = lv_obj_create(ov);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(80), LV_PCT(70));
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 44, 96);

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    char buf[48];
    int y = 0;
    const int dy = 42;

    dash_row(body, "CHIP", "ESP32-P4", y); y += dy;
    snprintf(buf, sizeof(buf), "%d", ci.cores);
    dash_row(body, "CORES", buf, y); y += dy;
    snprintf(buf, sizeof(buf), "rev %d", ci.revision);
    dash_row(body, "SILICON", buf, y); y += dy;

    const esp_app_desc_t *ad = esp_app_get_description();
    dash_row(body, "FIRMWARE", ad ? ad->version : "?", y); y += dy;

    s_dash_psram  = dash_row(body, "PSRAM", "...", y); y += dy;
    s_dash_heap   = dash_row(body, "HEAP", "...", y); y += dy;
    s_dash_uptime = dash_row(body, "UPTIME", "...", y); y += dy;

    dash_timer_cb(NULL);
    s_tool_timer = lv_timer_create(dash_timer_cb, 1000, NULL);
}

/* ---- wifi recon: live AP table, pulled from hc_wifi_recon once a second ---- */
static void wifi_refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_wifi_tbl) return;
    static hc_wifi_ap_t aps[48];
    size_t n = hc_wifi_recon_snapshot(aps, sizeof(aps) / sizeof(aps[0]));
    lv_table_set_row_count(s_wifi_tbl, (uint32_t)n + 1);
    char b[24];
    for (size_t i = 0; i < n; i++) {
        uint32_t row = (uint32_t)i + 1;
        const char *ssid = (aps[i].ssid_len && !(aps[i].flags & HC_WIFI_F_HIDDEN))
                           ? aps[i].ssid : "<hidden>";
        lv_table_set_cell_value(s_wifi_tbl, row, 0, ssid);
        snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
                 aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2],
                 aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5]);
        lv_table_set_cell_value(s_wifi_tbl, row, 1, b);
        snprintf(b, sizeof(b), "%u", aps[i].channel);
        lv_table_set_cell_value(s_wifi_tbl, row, 2, b);
        snprintf(b, sizeof(b), "%d", aps[i].rssi);
        lv_table_set_cell_value(s_wifi_tbl, row, 3, b);
    }
}

static void open_wifi(void)
{
    lv_obj_t *ov = tool_shell("wi-fi recon");

    lv_obj_t *hint = lv_label_create(ov);
    lv_label_set_text(hint, "APs seen (strongest first) \xC2\xB7 updates live");
    lv_obj_set_style_text_color(hint, lv_color_hex(HC_DIMTXT), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 160, 56);

    s_wifi_tbl = lv_table_create(ov);
    lv_obj_set_size(s_wifi_tbl, LV_PCT(94), LV_PCT(72));
    lv_obj_align(s_wifi_tbl, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_color(s_wifi_tbl, lv_color_hex(HC_BG), 0);
    lv_obj_set_style_bg_opa(s_wifi_tbl, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wifi_tbl, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_tbl, lv_color_hex(HC_PANEL), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_wifi_tbl, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_wifi_tbl, lv_color_hex(HC_LHONEY), LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_wifi_tbl, lv_color_hex(0x2E2452), LV_PART_ITEMS);

    lv_table_set_column_count(s_wifi_tbl, 4);
    lv_table_set_column_width(s_wifi_tbl, 0, 360);
    lv_table_set_column_width(s_wifi_tbl, 1, 240);
    lv_table_set_column_width(s_wifi_tbl, 2, 70);
    lv_table_set_column_width(s_wifi_tbl, 3, 90);
    lv_table_set_cell_value(s_wifi_tbl, 0, 0, "SSID");
    lv_table_set_cell_value(s_wifi_tbl, 0, 1, "BSSID");
    lv_table_set_cell_value(s_wifi_tbl, 0, 2, "CH");
    lv_table_set_cell_value(s_wifi_tbl, 0, 3, "RSSI");

    wifi_refresh_cb(NULL);
    s_tool_timer = lv_timer_create(wifi_refresh_cb, 1000, NULL);
}

static void open_msg(const char *title, const char *msg, uint32_t color)
{
    lv_obj_t *ov = tool_shell(title);
    lv_obj_t *m = lv_label_create(ov);
    lv_label_set_text(m, msg);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(m, lv_color_hex(color), 0);
    lv_obj_center(m);
}

static void async_open_tool(void *p)
{
    s_tool_pending = false;
    if (s_overlay) return;
    int id = (int)(intptr_t)p;
    switch (id) {
        case TOOL_WIFI:     open_wifi(); break;
        case TOOL_CONFIG:   open_config(); break;
        case TOOL_BLE:      open_msg("ble recon", "coming soon", HC_LCYAN); break;
        case TOOL_CAPTURE:  open_msg("capture", "pcap to SD\ncoming soon", HC_LHONEY); break;
        case TOOL_BENCH:    open_msg("usb bench", "coming soon", HC_LHONEY); break;
        default:            open_msg("802.15.4", "waiting on the C6 radio", HC_DIMTXT); break;
    }
}

static void node_click_cb(lv_event_t *e)
{
    if (s_overlay || s_tool_pending) return;
    s_tool_pending = true;
    lv_async_call(async_open_tool, lv_event_get_user_data(e));
}

static void build_node(lv_obj_t *parent, int cx, int cy, const hc_node_t *n)
{
    const int w = 168, h = 96;
    lv_obj_t *nd = lv_obj_create(parent);
    lv_obj_remove_style_all(nd);
    lv_obj_set_size(nd, w, h);
    lv_obj_set_pos(nd, cx - w / 2, cy - h / 2);
    lv_obj_set_style_radius(nd, 18, 0);
    lv_obj_set_style_bg_color(nd, lv_color_hex(HC_NODE), 0);
    lv_obj_set_style_bg_opa(nd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(nd, lv_color_hex(n->color), 0);
    lv_obj_set_style_border_width(nd, 2, 0);

    lv_obj_t *ic = lv_label_create(nd);
    lv_label_set_text(ic, n->icon);
    lv_obj_set_style_text_color(ic, lv_color_hex(n->color), 0);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t *nm = lv_label_create(nd);
    lv_label_set_text(nm, n->name);
    lv_obj_set_style_text_color(nm, lv_color_hex(n->lcolor), 0);
    lv_obj_align(nm, LV_ALIGN_CENTER, 0, 16);

    lv_obj_add_flag(nd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nd, node_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)n->id);
}

#endif /* HAVE_LVGL */

esp_err_t hc_ui_init(void *lv_display)
{
    if (!lv_display) { ESP_LOGW(TAG, "headless UI"); return ESP_OK; }
#if HAVE_LVGL
    if (!bsp_display_lock(1000)) return ESP_FAIL;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(HC_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(scr);

    int W = lv_obj_get_width(scr);
    int H = lv_obj_get_height(scr);
    if (W < 200 || H < 200) { W = 1280; H = 720; }   /* fallback */

    int cx = W / 2;
    int cy = H / 2 + 30;                              /* leave room for the title */
    int r  = (H < W ? H : W) * 37 / 100;              /* orbit radius */

    /* connector lines first (behind everything) */
    for (size_t k = 0; k < N_NODES; k++) {
        double a = (-90.0 + 60.0 * (double)k) * 3.14159265 / 180.0;
        int nx = cx + (int)(r * cos(a));
        int ny = cy + (int)(r * sin(a));
        s_npts[k][0].x = cx; s_npts[k][0].y = cy;
        s_npts[k][1].x = nx; s_npts[k][1].y = ny;
        lv_obj_t *ln = lv_line_create(scr);
        lv_obj_set_pos(ln, 0, 0);
        lv_line_set_points(ln, s_npts[k], 2);
        lv_obj_set_style_line_color(ln, lv_color_hex(HC_DIMLINE), 0);
        lv_obj_set_style_line_width(ln, 2, 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
    }

    /* core: halo + wasp */
    int d = 172;
    lv_obj_t *halo = lv_obj_create(scr);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, d + 40, d + 40);
    lv_obj_set_pos(halo, cx - (d + 40) / 2, cy - (d + 40) / 2);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(halo, lv_color_hex(HC_HONEY), 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_20, 0);

    lv_obj_t *core = lv_obj_create(scr);
    lv_obj_remove_style_all(core);
    lv_obj_set_size(core, d, d);
    lv_obj_set_pos(core, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core, lv_color_hex(HC_NODE), 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(core, lv_color_hex(HC_HONEY), 0);
    lv_obj_set_style_border_width(core, 3, 0);
    build_wasp(core, d);

    /* nodes (on top of the lines) */
    for (size_t k = 0; k < N_NODES; k++) {
        double a = (-90.0 + 60.0 * (double)k) * 3.14159265 / 180.0;
        int nx = cx + (int)(r * cos(a));
        int ny = cy + (int)(r * sin(a));
        build_node(scr, nx, ny, &s_nodes[k]);
    }

    /* title (top-left) */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WASP NEST");
    lv_obj_set_style_text_color(title, lv_color_hex(HC_HONEY), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "recon \xC2\xB7 audit \xC2\xB7 tab5");
    lv_obj_set_style_text_color(sub, lv_color_hex(HC_CYAN), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 20, 40);

    /* status chips (top-right) */
    lv_obj_t *frames_v = status_chip(scr, "SEEN", "0", HC_CYAN);
    lv_obj_align(lv_obj_get_parent(frames_v), LV_ALIGN_TOP_RIGHT, -14, 12);
    s_frames_lbl = frames_v;

    lv_obj_t *c6_v = status_chip(scr, "C6 RADIO", "waiting", HC_AMBER);
    lv_obj_align(lv_obj_get_parent(c6_v), LV_ALIGN_TOP_RIGHT, -120, 12);
    s_mode_lbl = c6_v;

    bsp_display_unlock();
    s_active = true;
    ESP_LOGI(TAG, "wasp-nest UI up (%dx%d)", W, H);
#endif
    return ESP_OK;
}

void hc_ui_add_frame(const hc_dissected_t *row)
{
#if HAVE_LVGL
    if (!s_active || !row) return;
    s_frame_count++;
    if (bsp_display_lock(50)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_frame_count);
        lv_label_set_text(s_frames_lbl, buf);
        bsp_display_unlock();
    }
#else
    (void)row;
#endif
}

void hc_ui_set_energy(const hc_energy_result_t *e) { (void)e; }
void hc_ui_add_scan(const hc_scan_result_t *s)     { (void)s; }

void hc_ui_set_mode(hc_radio_mode_t mode, bool ok)
{
#if HAVE_LVGL
    if (!s_active) return;
    const char *n = mode == HC_MODE_SNIFFER ? "sniffer" :
                    mode == HC_MODE_WIFI_SNIFFER ? "wifi-recon" :
                    mode == HC_MODE_WIFI ? "wifi" : "idle";
    if (bsp_display_lock(50)) {
        lv_label_set_text(s_mode_lbl, ok ? n : "fail");
        lv_obj_set_style_text_color(s_mode_lbl, lv_color_hex(ok ? HC_HONEY : HC_AMBER), 0);
        bsp_display_unlock();
    }
#else
    (void)mode; (void)ok;
#endif
}

void hc_ui_set_flash_progress(const hc_flash_progress_t *p) { (void)p; }
void hc_ui_log(const char *line) { (void)line; }

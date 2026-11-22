/*
 * Wasp Nest — P4 UI (LVGL) — "cyberpunk waspnest" theme
 *
 * A pannable comb of hexagonal tiles over a warm-charcoal field, with a pinned
 * header (bumblebee mark + status chips). Honey-gold primary, soft cyan accent.
 * Tiles are hexagon OUTLINES drawn with lv_line (stable across LVGL 9.x); the
 * whole comb lives in a scrollable container you can drag in any direction.
 *
 * Orientation: native for now. Landscape rotation lands in a follow-up once the
 * comb is confirmed on-panel (DSI direct-mode rotation is handled separately).
 *
 * Threading: every LVGL touch is wrapped in bsp_display_lock()/unlock().
 */
#include "hc_ui.h"
#include "hc_mode_manager.h"
#include "hc_wifi_recon.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_random.h"

#if __has_include("bsp/esp-bsp.h")
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

#define TAG "hc_ui"

/* ---- waspnest palette ---- */
#define HC_BG       0x15110A   /* warm charcoal field            */
#define HC_HDR      0x15110A   /* header strip                   */
#define HC_PANEL    0x1C160D   /* status chip                    */
#define HC_HONEY    0xFFC233   /* primary                        */
#define HC_AMBER    0xF5A623   /* "waiting" amber                */
#define HC_CYAN     0x38E0E0   /* accent                         */
#define HC_DIMLINE  0x4A3A16   /* empty comb-cell outline        */
#define HC_LHONEY   0xE8C98A   /* honey label                    */
#define HC_LCYAN    0x9EE6E6   /* cyan label                     */
#define HC_DIMTXT   0x8A7A52   /* muted text                     */
#define HC_STRIPE   0x1A1206   /* bee stripes                    */

static bool s_active;

#if HAVE_LVGL

/* Hexagon dimensions (flat-top) and comb spacing. */
#define HEX_W   150
#define HEX_H   130
#define HEX_AX  112            /* column advance = 0.75 * W       */
#define HEX_AY  130            /* row advance    = H              */
#define HDR_H   70
#define COMB_OX 16
#define COMB_OY (HDR_H + 12)
#define COMB_COLS 15
#define COMB_ROWS 8
/* Hidden "buzz" game lives behind this unlabeled cell (pan right to find it). */
#define SECRET_COL 11
#define SECRET_ROW 2

/* One shared outline shape; every cell reuses it (relative points). */
static const lv_point_precise_t HEXP[] = {
    {37, 0}, {113, 0}, {150, 65}, {113, 130}, {37, 130}, {0, 65}, {37, 0}
};

static lv_obj_t *s_mode_lbl;
static lv_obj_t *s_frames_lbl;
static uint32_t  s_frame_count;


enum { TOOL_BENCH, TOOL_EXPLORER, TOOL_CAPTURE, TOOL_DECODE, TOOL_MESH, TOOL_CONFIG, TOOL_WIFI };

typedef struct {
    int col, row;
    const char *icon;
    const char *name;
    uint32_t line_color;
    uint32_t text_color;
    int id;
} hc_tile_t;

/* The six live tools, clustered near the comb's center so the hive extends in
 * every direction from the start. Every other cell is an empty comb cell. */
static const hc_tile_t s_tiles[] = {
    {6, 3, LV_SYMBOL_LIST,     "decode",   HC_HONEY, HC_LHONEY, TOOL_DECODE},
    {7, 3, LV_SYMBOL_DOWNLOAD, "bench",    HC_HONEY, HC_LHONEY, TOOL_BENCH},
    {8, 3, LV_SYMBOL_SAVE,     "capture",  HC_HONEY, HC_LHONEY, TOOL_CAPTURE},
    {6, 4, LV_SYMBOL_SETTINGS, "config",   HC_HONEY, HC_LHONEY, TOOL_CONFIG},
    {7, 4, LV_SYMBOL_SHUFFLE,  "mesh",     HC_CYAN,  HC_LCYAN,  TOOL_MESH},
    {8, 4, LV_SYMBOL_WIFI,     "explorer", HC_CYAN,  HC_LCYAN,  TOOL_EXPLORER},
    {9, 3, LV_SYMBOL_WIFI,     "wifi",     HC_CYAN,  HC_LCYAN,  TOOL_WIFI},
};

static const hc_tile_t *find_tile(int col, int row)
{
    for (size_t i = 0; i < sizeof(s_tiles) / sizeof(s_tiles[0]); i++)
        if (s_tiles[i].col == col && s_tiles[i].row == row) return &s_tiles[i];
    return NULL;
}

static lv_obj_t *bare(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

/* Little bumblebee, built from a few styled objects. */
static void build_bee(lv_obj_t *parent)
{
    lv_obj_t *bee = bare(parent);
    lv_obj_set_size(bee, 34, 30);

    lv_obj_t *wl = lv_obj_create(bee);
    lv_obj_remove_style_all(wl);
    lv_obj_set_size(wl, 15, 11);
    lv_obj_set_pos(wl, 1, 0);
    lv_obj_set_style_radius(wl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wl, lv_color_hex(0xCFEFEF), 0);
    lv_obj_set_style_bg_opa(wl, LV_OPA_60, 0);

    lv_obj_t *wr = lv_obj_create(bee);
    lv_obj_remove_style_all(wr);
    lv_obj_set_size(wr, 15, 11);
    lv_obj_set_pos(wr, 18, 0);
    lv_obj_set_style_radius(wr, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wr, lv_color_hex(0xCFEFEF), 0);
    lv_obj_set_style_bg_opa(wr, LV_OPA_60, 0);

    lv_obj_t *body = lv_obj_create(bee);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 22, 24);
    lv_obj_set_pos(body, 6, 6);
    lv_obj_set_style_radius(body, 11, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(HC_HONEY), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *s = lv_obj_create(bee);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 22, 3);
        lv_obj_set_pos(s, 6, 12 + i * 6);
        lv_obj_set_style_bg_color(s, lv_color_hex(HC_STRIPE), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    }
}

static lv_obj_t *status_chip(lv_obj_t *parent, const char *label, const char *val, uint32_t vcol)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 92, 44);
    lv_obj_set_style_bg_color(chip, lv_color_hex(HC_PANEL), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 10, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x2A2213), 0);
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

/* ---- tool views: tap a tile -> full-screen overlay with a back button ---- */
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
    lv_obj_set_style_radius(back, 10, 0);
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
    lv_obj_t *ov = tool_shell("wifi recon");

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
    lv_obj_set_style_border_color(s_wifi_tbl, lv_color_hex(0x2A2213), LV_PART_ITEMS);

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
        case TOOL_CONFIG: open_config(); break;
        case TOOL_WIFI:   open_wifi(); break;
        case TOOL_BENCH:  open_msg("usb bench", "coming soon", HC_LHONEY); break;
        default:          open_msg("tool", "waiting on the C6 radio\nflash it via the M5 downloader", HC_DIMTXT); break;
    }
}

static void tile_click_cb(lv_event_t *e)
{
    if (s_overlay || s_tool_pending) return;
    s_tool_pending = true;
    lv_async_call(async_open_tool, lv_event_get_user_data(e));
}

/* ---- hidden "buzz" mini-game: flap the bee through the comb ---- */
#define BEE_X   320
#define BEE_SZ  38
#define PIPEW   100
#define GAPH    250
#define GSPEED  7
#define GRAV    1
#define FLAPV   (-15)
#define NPIPE   3
#define GAME_W  1280
#define GAME_H  720

static lv_obj_t *s_bee;
static lv_obj_t *s_ptop[NPIPE], *s_pbot[NPIPE];
static int  s_pipex[NPIPE], s_pipegap[NPIPE];
static bool s_scored[NPIPE];
static int  s_bee_y, s_bee_vy, s_score;
static bool s_game_over;
static lv_obj_t *s_score_lbl, *s_over_lbl;

static void game_place_pipe(int i)
{
    s_pipegap[i] = 150 + (int)(esp_random() % 320);
    lv_obj_set_pos(s_ptop[i], s_pipex[i], 0);
    lv_obj_set_size(s_ptop[i], PIPEW, s_pipegap[i] - GAPH / 2);
    lv_obj_set_pos(s_pbot[i], s_pipex[i], s_pipegap[i] + GAPH / 2);
    lv_obj_set_size(s_pbot[i], PIPEW, GAME_H - (s_pipegap[i] + GAPH / 2));
}

static void game_reset(void)
{
    s_bee_y = GAME_H / 2;
    s_bee_vy = 0;
    s_score = 0;
    s_game_over = false;
    for (int i = 0; i < NPIPE; i++) {
        s_pipex[i] = GAME_W + 120 + i * 440;
        s_scored[i] = false;
        game_place_pipe(i);
    }
    lv_obj_set_pos(s_bee, BEE_X, s_bee_y);
    lv_label_set_text(s_score_lbl, "0");
    lv_obj_add_flag(s_over_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void game_tick(lv_timer_t *t)
{
    (void)t;
    if (s_game_over) return;

    s_bee_vy += GRAV;
    if (s_bee_vy > 22) s_bee_vy = 22;
    s_bee_y += s_bee_vy;
    lv_obj_set_y(s_bee, s_bee_y);

    for (int i = 0; i < NPIPE; i++) {
        s_pipex[i] -= GSPEED;
        if (s_pipex[i] < -PIPEW) {
            int maxx = 0;
            for (int j = 0; j < NPIPE; j++) if (s_pipex[j] > maxx) maxx = s_pipex[j];
            s_pipex[i] = maxx + 440;
            s_scored[i] = false;
            game_place_pipe(i);
        } else {
            lv_obj_set_x(s_ptop[i], s_pipex[i]);
            lv_obj_set_x(s_pbot[i], s_pipex[i]);
        }
        if (!s_scored[i] && s_pipex[i] + PIPEW < BEE_X) {
            s_scored[i] = true;
            s_score++;
            char b[8];
            snprintf(b, sizeof(b), "%d", s_score);
            lv_label_set_text(s_score_lbl, b);
        }
        if (BEE_X + BEE_SZ > s_pipex[i] && BEE_X < s_pipex[i] + PIPEW) {
            if (s_bee_y < s_pipegap[i] - GAPH / 2 || s_bee_y + BEE_SZ > s_pipegap[i] + GAPH / 2)
                s_game_over = true;
        }
    }
    if (s_bee_y < 0 || s_bee_y + BEE_SZ > GAME_H) s_game_over = true;

    if (s_game_over) {
        char b[48];
        snprintf(b, sizeof(b), "game over\nscore %d\ntap to retry", s_score);
        lv_label_set_text(s_over_lbl, b);
        lv_obj_remove_flag(s_over_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static void game_flap(lv_event_t *e)
{
    (void)e;
    if (s_game_over) game_reset();
    else s_bee_vy = FLAPV;
}

static void open_game(void)
{
    lv_obj_t *ov = tool_shell("buzz");
    lv_obj_add_event_cb(ov, game_flap, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < NPIPE; i++) {
        s_ptop[i] = lv_obj_create(ov);
        lv_obj_remove_style_all(s_ptop[i]);
        lv_obj_set_style_bg_color(s_ptop[i], lv_color_hex(HC_PANEL), 0);
        lv_obj_set_style_bg_opa(s_ptop[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_ptop[i], lv_color_hex(HC_HONEY), 0);
        lv_obj_set_style_border_width(s_ptop[i], 2, 0);
        s_pbot[i] = lv_obj_create(ov);
        lv_obj_remove_style_all(s_pbot[i]);
        lv_obj_set_style_bg_color(s_pbot[i], lv_color_hex(HC_PANEL), 0);
        lv_obj_set_style_bg_opa(s_pbot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_pbot[i], lv_color_hex(HC_HONEY), 0);
        lv_obj_set_style_border_width(s_pbot[i], 2, 0);
    }

    s_bee = lv_obj_create(ov);
    lv_obj_remove_style_all(s_bee);
    lv_obj_set_size(s_bee, BEE_SZ, BEE_SZ);
    lv_obj_set_style_radius(s_bee, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bee, lv_color_hex(HC_HONEY), 0);
    lv_obj_set_style_bg_opa(s_bee, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_bee, lv_color_hex(HC_STRIPE), 0);
    lv_obj_set_style_border_width(s_bee, 3, 0);

    s_score_lbl = lv_label_create(ov);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(HC_HONEY), 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 22);

    s_over_lbl = lv_label_create(ov);
    lv_obj_set_style_text_align(s_over_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_over_lbl, lv_color_hex(HC_CYAN), 0);
    lv_obj_center(s_over_lbl);

    game_reset();
    s_tool_timer = lv_timer_create(game_tick, 30, NULL);
}

static void async_open_game(void *p)
{
    (void)p;
    s_tool_pending = false;
    if (s_overlay) return;
    open_game();
}

static void game_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_overlay || s_tool_pending) return;
    s_tool_pending = true;
    lv_async_call(async_open_game, NULL);
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

    /* --- pannable comb --- */
    lv_obj_t *cont = bare(scr);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(cont, LV_DIR_ALL);
    lv_obj_set_scroll_snap_x(cont, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *field = bare(cont);
    lv_obj_set_size(field, COMB_OX * 2 + COMB_COLS * HEX_AX + HEX_W / 4,
                           COMB_OY + COMB_ROWS * HEX_AY + HEX_H);

    for (int c = 0; c < COMB_COLS; c++) {
        for (int r = 0; r < COMB_ROWS; r++) {
            int x = COMB_OX + c * HEX_AX;
            int y = COMB_OY + r * HEX_AY + ((c & 1) ? HEX_H / 2 : 0);
            const hc_tile_t *t = find_tile(c, r);
            bool secret = (c == SECRET_COL && r == SECRET_ROW);

            /* Decorative cells (the vast majority) are just a dim hex outline.
             * Draw the line straight onto the field with no wrapper object —
             * that halves the comb's object count and keeps the LVGL pool from
             * filling up, which otherwise makes a heavy overlay's allocations
             * fail. Only interactive cells get a wrapper for their click area
             * and centered labels. */
            if (!t && !secret) {
                lv_obj_t *hex = lv_line_create(field);
                lv_obj_set_pos(hex, x, y);
                lv_line_set_points(hex, HEXP, 7);
                lv_obj_set_style_line_color(hex, lv_color_hex(HC_DIMLINE), 0);
                lv_obj_set_style_line_width(hex, 2, 0);
                lv_obj_set_style_line_rounded(hex, true, 0);
                continue;
            }

            lv_obj_t *cell = bare(field);
            lv_obj_set_pos(cell, x, y);
            lv_obj_set_size(cell, HEX_W, HEX_H);

            lv_obj_t *hex = lv_line_create(cell);
            lv_line_set_points(hex, HEXP, 7);
            lv_obj_set_style_line_color(hex, lv_color_hex(t ? t->line_color : 0x7A6322), 0);
            lv_obj_set_style_line_width(hex, t ? 3 : 2, 0);
            lv_obj_set_style_line_rounded(hex, true, 0);

            if (t) {
                lv_obj_t *ic = lv_label_create(cell);
                lv_label_set_text(ic, t->icon);
                lv_obj_set_style_text_color(ic, lv_color_hex(t->line_color), 0);
                lv_obj_align(ic, LV_ALIGN_CENTER, 0, -14);

                lv_obj_t *nm = lv_label_create(cell);
                lv_label_set_text(nm, t->name);
                lv_obj_set_style_text_color(nm, lv_color_hex(t->text_color), 0);
                lv_obj_align(nm, LV_ALIGN_CENTER, 0, 14);

                /* tappable: a short tap opens the tool, a drag still pans */
                lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(cell, tile_click_cb, LV_EVENT_CLICKED,
                                    (void *)(intptr_t)t->id);
            } else {
                /* hidden game cell: a faint honey tint + dot as the only hint */
                lv_obj_t *dot = lv_label_create(cell);
                lv_label_set_text(dot, ".");
                lv_obj_set_style_text_color(dot, lv_color_hex(HC_HONEY), 0);
                lv_obj_center(dot);
                lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(cell, game_click_cb, LV_EVENT_CLICKED, NULL);
            }
        }
    }

    /* --- pinned header (on top of the comb) --- */
    lv_obj_t *hdr = bare(scr);
    lv_obj_set_size(hdr, LV_PCT(100), HDR_H);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(HC_HDR), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(hdr, 14, 0);
    lv_obj_set_style_pad_right(hdr, 14, 0);

    build_bee(hdr);
    lv_obj_t *bee_anchor = lv_obj_get_child(hdr, 0);
    lv_obj_align(bee_anchor, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "waspnest");
    lv_obj_set_style_text_color(title, lv_color_hex(HC_HONEY), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 44, -9);

    lv_obj_t *sub = lv_label_create(hdr);
    lv_label_set_text(sub, "TAB5 \xC2\xB7 802.15.4 EXPLORER");
    lv_obj_set_style_text_color(sub, lv_color_hex(HC_CYAN), 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 44, 11);

    lv_obj_t *frames_chip = status_chip(hdr, "FRAMES", "0", HC_CYAN);
    lv_obj_align(lv_obj_get_parent(frames_chip), LV_ALIGN_RIGHT_MID, 0, 0);
    s_frames_lbl = frames_chip;

    lv_obj_t *c6_chip = status_chip(hdr, "C6 RADIO", "waiting", HC_AMBER);
    lv_obj_align(lv_obj_get_parent(c6_chip), LV_ALIGN_RIGHT_MID, -100, 0);
    s_mode_lbl = c6_chip;

    /* Start centered so the comb extends in every direction from the first view. */
    lv_obj_update_layout(scr);
    int vw = lv_obj_get_width(cont),  vh = lv_obj_get_height(cont);
    int fw = lv_obj_get_width(field), fh = lv_obj_get_height(field);
    int sx = (fw - vw) / 2, sy = (fh - vh) / 2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    lv_obj_scroll_to(cont, sx, sy, LV_ANIM_OFF);

    bsp_display_unlock();
    s_active = true;
    ESP_LOGI(TAG, "waspnest UI up");
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

/*
 * Wasp Nest — P4 storage implementation (SDMMC + pcap)
 *
 * Tab5 microSD is on the P4's SDMMC slot (see docs/hardware.md for the pin set
 * reported by the board docs: G39-G44). We use the 1-bit or 4-bit SDMMC host via
 * the IDF sdmmc driver. If your board revision wires SD on SPI instead, switch
 * the host init block (clearly marked).
 */
#include "hc_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#define TAG "hc_store"
#define MOUNT_POINT "/sdcard"
#define CAP_DIR     MOUNT_POINT "/waspnest"

#define LINKTYPE_IEEE802_15_4_WITHFCS 195

static bool  s_mounted;
static FILE *s_pcap;

/* pcap classic global header */
typedef struct __attribute__((packed)) {
    uint32_t magic;        /* 0xa1b2c3d4 */
    uint16_t ver_major, ver_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;      /* link type */
} pcap_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t ts_sec, ts_usec;
    uint32_t incl_len, orig_len;
} pcap_rec_t;

esp_err_t hc_storage_mount(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    /* Tab5 SD: set the data width your wiring supports. Start with 1-bit for
     * reliability during bring-up, move to 4-bit for throughput later. */
    slot.width = 1;

    esp_err_t e = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mcfg, &card);
    if (e != ESP_OK) { ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(e)); return e; }

    mkdir(CAP_DIR, 0775);
    s_mounted = true;
    ESP_LOGI(TAG, "SD mounted");
    return ESP_OK;
}
bool hc_storage_is_mounted(void) { return s_mounted; }

esp_err_t hc_storage_pcap_open(char *path_out, size_t cap)
{
    if (!s_mounted) { esp_err_t e = hc_storage_mount(); if (e != ESP_OK) return e; }
    if (s_pcap) return ESP_ERR_INVALID_STATE;

    /* find a free capNNN.pcap */
    char path[96];
    for (int n = 0; n < 1000; ++n) {
        snprintf(path, sizeof(path), CAP_DIR "/cap%03d.pcap", n);
        struct stat st;
        if (stat(path, &st) != 0) break;
    }
    s_pcap = fopen(path, "wb");
    if (!s_pcap) return ESP_FAIL;

    pcap_hdr_t gh = {
        .magic = 0xa1b2c3d4, .ver_major = 2, .ver_minor = 4,
        .thiszone = 0, .sigfigs = 0, .snaplen = 256,
        .network = LINKTYPE_IEEE802_15_4_WITHFCS,
    };
    fwrite(&gh, sizeof(gh), 1, s_pcap);
    fflush(s_pcap);
    if (path_out) strncpy(path_out, path, cap);
    return ESP_OK;
}

esp_err_t hc_storage_pcap_write(const hc_frame_hdr_t *meta, const uint8_t *mpdu)
{
    if (!s_pcap) return ESP_ERR_INVALID_STATE;
    pcap_rec_t rh = {
        .ts_sec  = (uint32_t)(meta->ts_us / 1000000ull),
        .ts_usec = (uint32_t)(meta->ts_us % 1000000ull),
        .incl_len = meta->len, .orig_len = meta->len,
    };
    fwrite(&rh, sizeof(rh), 1, s_pcap);
    fwrite(mpdu, 1, meta->len, s_pcap);
    /* periodic flush so a yanked card still yields a usable file */
    static uint16_t n; if ((++n & 0x1F) == 0) fflush(s_pcap);
    return ESP_OK;
}

esp_err_t hc_storage_pcap_close(void)
{
    if (!s_pcap) return ESP_OK;
    fflush(s_pcap); fclose(s_pcap); s_pcap = NULL;
    return ESP_OK;
}

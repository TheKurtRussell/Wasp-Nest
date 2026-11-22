/*
 * Wasp Nest — ESP serial bootloader protocol (host side) implementation
 *
 * Frame format (esptool): SLIP-framed packets delimited by 0xC0, with 0xDB
 * escaping (0xDB 0xDC -> 0xC0, 0xDB 0xDD -> 0xDB). Each command packet is:
 *   dir(0x00) cmd(1) size(2,LE) checksum(4,LE) data[size]
 * Responses mirror: dir(0x01) cmd(1) size(2) value(4) data[size] status...
 */
#include "hc_esp_loader.h"
#include <string.h>
#include "esp_log.h"

#define TAG "hc_loader"
#define SLIP_END  0xC0
#define SLIP_ESC  0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

static hc_loader_transport_t T;

void hc_loader_bind(const hc_loader_transport_t *t) { T = *t; }

uint8_t hc_loader_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t c = 0xEF; /* esptool seed */
    for (uint16_t i = 0; i < len; ++i) c ^= data[i];
    return c;
}

/* ---- SLIP write ---------------------------------------------------------- */
static int slip_write_frame(const uint8_t *pkt, uint16_t len)
{
    uint8_t end = SLIP_END;
    T.write(&end, 1, T.io);
    for (uint16_t i = 0; i < len; ++i) {
        uint8_t b = pkt[i];
        if (b == SLIP_END)      { uint8_t e[2] = {SLIP_ESC, SLIP_ESC_END}; T.write(e, 2, T.io); }
        else if (b == SLIP_ESC) { uint8_t e[2] = {SLIP_ESC, SLIP_ESC_ESC}; T.write(e, 2, T.io); }
        else                    { T.write(&b, 1, T.io); }
    }
    T.write(&end, 1, T.io);
    return 0;
}

/* ---- SLIP read (one frame) ----------------------------------------------- */
static int slip_read_frame(uint8_t *out, uint16_t cap, uint32_t timeout_ms)
{
    uint16_t n = 0;
    bool in_frame = false, esc = false;
    uint8_t b;
    while (T.read(&b, 1, timeout_ms, T.io) == 1) {
        if (!in_frame) { if (b == SLIP_END) in_frame = true; continue; }
        if (b == SLIP_END) { if (n == 0) continue; return n; } /* end of frame */
        if (esc) { b = (b == SLIP_ESC_END) ? SLIP_END : SLIP_ESC; esc = false; }
        else if (b == SLIP_ESC) { esc = true; continue; }
        if (n < cap) out[n++] = b;
    }
    return -1; /* timeout */
}

esp_err_t hc_loader_command(hc_loader_cmd_t cmd, const uint8_t *data, uint16_t len,
                            uint32_t chk, uint32_t *val_out, uint32_t timeout_ms)
{
    uint8_t pkt[8 + 512];
    if (len > sizeof(pkt) - 8) return ESP_ERR_INVALID_SIZE;
    pkt[0] = 0x00; pkt[1] = (uint8_t)cmd;
    pkt[2] = len & 0xFF; pkt[3] = len >> 8;
    pkt[4] = chk; pkt[5] = chk >> 8; pkt[6] = chk >> 16; pkt[7] = chk >> 24;
    if (len) memcpy(&pkt[8], data, len);
    slip_write_frame(pkt, 8 + len);

    uint8_t resp[64];
    int r = slip_read_frame(resp, sizeof(resp), timeout_ms);
    if (r < 10) return ESP_ERR_TIMEOUT;
    if (resp[0] != 0x01 || resp[1] != (uint8_t)cmd) return ESP_ERR_INVALID_RESPONSE;
    if (val_out) memcpy(val_out, &resp[4], 4);
    /* status bytes are the last 2 (ROM) or 4 (stub); 0 == success */
    uint8_t status = resp[r - 2];
    return status == 0 ? ESP_OK : ESP_FAIL;
}

void hc_loader_enter_download_mode(bool usb_serial_jtag)
{
    if (!T.set_dtr_rts) return;
    if (usb_serial_jtag) {
        /* USB-Serial-JTAG reset into download: toggle both then release.
         * (esptool "usb_jtag_serial" reset sequence.) */
        T.set_dtr_rts(false, false, T.io);
        T.set_dtr_rts(true,  false, T.io);   /* IO0 low via DTR */
        T.set_dtr_rts(false, true,  T.io);   /* pulse reset */
        T.set_dtr_rts(false, false, T.io);
    } else {
        /* Classic: RTS->EN(reset), DTR->IO0(boot). */
        T.set_dtr_rts(false, true,  T.io);   /* EN low (in reset), IO0 high */
        T.set_dtr_rts(true,  true,  T.io);   /* IO0 low */
        T.set_dtr_rts(true,  false, T.io);   /* release EN -> boot with IO0 low */
        T.set_dtr_rts(false, false, T.io);
    }
}

esp_err_t hc_loader_sync(void)
{
    /* SYNC payload: 0x07 0x07 0x12 0x20 + 32x 0x55 */
    uint8_t s[36]; s[0]=0x07; s[1]=0x07; s[2]=0x12; s[3]=0x20;
    memset(&s[4], 0x55, 32);
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (hc_loader_command(LDR_SYNC, s, sizeof(s), 0, NULL, 300) == ESP_OK)
            return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t hc_loader_read_reg(uint32_t addr, uint32_t *out)
{
    uint8_t d[4] = { addr, addr>>8, addr>>16, addr>>24 };
    return hc_loader_command(LDR_READ_REG, d, 4, 0, out, 500);
}

esp_err_t hc_loader_flash_begin(uint32_t size, uint32_t offset, uint32_t block)
{
    uint32_t blocks = (size + block - 1) / block;
    uint8_t d[16];
    memcpy(&d[0], &size, 4); memcpy(&d[4], &blocks, 4);
    memcpy(&d[8], &block, 4); memcpy(&d[12], &offset, 4);
    return hc_loader_command(LDR_FLASH_BEGIN, d, sizeof(d), 0, NULL, 10000);
}

esp_err_t hc_loader_flash_block(const uint8_t *data, uint16_t len, uint32_t seq)
{
    uint8_t hdr[16] = {0};
    uint32_t l = len;
    memcpy(&hdr[0], &l, 4); memcpy(&hdr[4], &seq, 4);
    /* command data = 16-byte header + payload; checksum is over payload only */
    static uint8_t buf[16 + 1024];
    if (len > 1024) return ESP_ERR_INVALID_SIZE;
    memcpy(buf, hdr, 16); memcpy(&buf[16], data, len);
    uint32_t chk = hc_loader_checksum(data, len);
    return hc_loader_command(LDR_FLASH_DATA, buf, 16 + len, chk, NULL, 3000);
}

esp_err_t hc_loader_flash_end(bool reboot)
{
    uint32_t flag = reboot ? 0 : 1; /* 0 = reboot, 1 = stay in loader */
    uint8_t d[4]; memcpy(d, &flag, 4);
    return hc_loader_command(LDR_FLASH_END, d, 4, 0, NULL, 1000);
}

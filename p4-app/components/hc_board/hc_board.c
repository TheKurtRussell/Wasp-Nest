/*
 * Wasp Nest — Tab5 board support implementation
 *
 * The C6 reset control is real (plain GPIO). Display/touch bring-up is the one
 * heavy board piece: the Tab5 panel is a 1280x720 MIPI-DSI panel with a GT911
 * touch controller. Two good ways to bring it up:
 *   (A) Use M5Stack's / the community esp-bsp "m5stack_tab5" BSP component,
 *       which gives you bsp_display_start() returning an lv_display_t*.
 *   (B) Hand-roll esp_lcd_mipi_dsi + esp_lcd_touch_gt911.
 * We default to (A) — add the BSP to main/idf_component.yml and call it here.
 */
#include "hc_board.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"      /* M5Stack Tab5 BSP: bsp_display_start(), etc. */

#define TAG "hc_board"

static lv_display_t *s_lv_disp;

void hc_board_c6_reset(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << HC_PIN_C6_RESET,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(HC_PIN_C6_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(HC_PIN_C6_RESET, 1);
    ESP_LOGI(TAG, "C6 reset pulsed");
}

esp_err_t hc_board_init(void)
{
    /* The BSP brings up the MIPI-DSI panel, touch, the display LDO rail, and
     * starts the LVGL port, returning the LVGL display. */
    s_lv_disp = bsp_display_start();
    if (!s_lv_disp) {
        ESP_LOGE(TAG, "bsp_display_start() failed");
        return ESP_FAIL;
    }
    bsp_display_backlight_on();

    /* Boot into landscape. The panel is portrait-native (720x1280); rotate the
     * LVGL display 90 so it's upright held in the left hand over the battery.
     * (If it comes up the wrong way, this becomes LV_DISPLAY_ROTATION_270.) */
    if (bsp_display_lock(1000)) {
        lv_display_set_rotation(s_lv_disp, LV_DISPLAY_ROTATION_270);
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "display up (landscape)");
    return ESP_OK;
}

void *hc_board_lvgl_display(void) { return s_lv_disp; }

esp_err_t hc_board_power_read(int *mv, int *ma)
{
    /* TODO: read INA226 @ 0x41 over the Tab5 I2C bus. */
    if (mv) *mv = 0;
    if (ma) *ma = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

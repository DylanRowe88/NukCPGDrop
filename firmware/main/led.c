#include "led.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "led_strip.h"
#include <stdlib.h>

#define LED_GPIO 48
#define LED_STRIP_LEN 1

static const char *TAG = "led";
static led_strip_handle_t g_strip = NULL;
static led_color_t g_current = {0, 0, 0};

const led_color_t LED_OFF = {0, 0, 0};
const led_color_t LED_GREEN = {0, 255, 0};
const led_color_t LED_RED = {255, 0, 0};
const led_color_t LED_BLUE = {0, 0, 255};
const led_color_t LED_YELLOW = {255, 255, 0};
const led_color_t LED_WHITE = {255, 255, 255};

static bool is_qemu(void) {
  esp_chip_info_t info;
  esp_chip_info(&info);
  // QEMU reports chip revision 0; real ESP32-S3 starts at rev 0.1+
  return info.revision == 0;
}

esp_err_t led_init(void) {
  // The RMT peripheral is not emulated in QEMU — attempting to
  // initialise it would hang the CPU. Skip the LED entirely when
  // running under emulation.
  if (is_qemu()) {
    ESP_LOGW(TAG, "QEMU detected — skipping RGB LED init");
    return ESP_OK;
  }

  led_strip_config_t cfg = {
      .strip_gpio_num = LED_GPIO,
      .max_leds = LED_STRIP_LEN,
      .led_pixel_format = LED_PIXEL_FORMAT_GRB,
      .led_model = LED_MODEL_WS2812,
  };
  led_strip_rmt_config_t rmt_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000,
      .mem_block_symbols = 64,
  };
  esp_err_t ret = led_strip_new_rmt_device(&cfg, &rmt_cfg, &g_strip);
  if (ret == ESP_OK) {
    led_strip_clear(g_strip);
    ESP_LOGI(TAG, "RGB LED initialized on GPIO%d", LED_GPIO);
  }
  return ret;
}

esp_err_t led_set_color(led_color_t color) {
  if (!g_strip)
    return ESP_ERR_INVALID_STATE;
  g_current = color;
  esp_err_t ret = led_strip_set_pixel(g_strip, 0, color.r, color.g, color.b);
  if (ret != ESP_OK)
    return ret;
  return led_strip_refresh(g_strip);
}

void led_get_color(led_color_t *color) {
  if (color)
    *color = g_current;
}

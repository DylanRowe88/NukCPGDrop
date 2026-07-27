#include "led.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include <stdlib.h>

#define LED_GPIO 48
#define LED_RESOLUTION_HZ 80000000 // 80MHz
#define LED_T0H_NS 350
#define LED_T0L_NS 800
#define LED_T1H_NS 700
#define LED_T1L_NS 600
#define LED_RESET_US 300

static const char *TAG = "led";
static rmt_channel_handle_t g_led_chan = NULL;
static rmt_encoder_handle_t g_led_encoder = NULL;
static uint8_t g_brightness = 32;

const led_color_t LED_OFF = {0, 0, 0};
const led_color_t LED_GREEN = {0, 255, 0};
const led_color_t LED_RED = {255, 0, 0};
const led_color_t LED_BLUE = {0, 0, 255};
const led_color_t LED_YELLOW = {255, 255, 0};
const led_color_t LED_CYAN = {0, 255, 255};
const led_color_t LED_WHITE = {255, 255, 255};

static void scale_color(uint8_t r, uint8_t g, uint8_t b, uint8_t * or,
                        uint8_t *og, uint8_t *ob) {
  int br = g_brightness;
  * or = (uint8_t)(((int)r * br) >> 8);
  *og = (uint8_t)(((int)g * br) >> 8);
  *ob = (uint8_t)(((int)b * br) >> 8);
}

esp_err_t led_init(uint8_t gpio) {
  rmt_tx_channel_config_t chan_cfg = {
      .gpio_num = (gpio_num_t)gpio,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = LED_RESOLUTION_HZ,
      .mem_block_symbols = 64,
      .trans_queue_depth = 1,
  };
  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chan_cfg, &g_led_chan), TAG,
                      "new tx");

  // Simple copy encoder — we'll build the RMT symbols manually
  rmt_copy_encoder_config_t enc_cfg = {};
  ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &g_led_encoder), TAG,
                      "new encoder");

  ESP_RETURN_ON_ERROR(rmt_enable(g_led_chan), TAG, "enable");
  ESP_LOGI(TAG, "RGB LED initialized on GPIO%d", gpio);
  led_set_color(LED_OFF);
  return ESP_OK;
}

esp_err_t led_set_color(led_color_t color) {
  if (!g_led_chan || !g_led_encoder)
    return ESP_ERR_INVALID_STATE;

  uint8_t r, g, b;
  scale_color(color.r, color.g, color.b, &r, &g, &b);

  // WS2812 uses GRB order at 800kHz
  uint32_t grb = (g << 16) | (r << 8) | b;

  // Build RMT symbols: 24 bits, each as a pair of (high, low) pulses
  rmt_symbol_word_t symbols[24];
  uint32_t half_cycle = LED_RESOLUTION_HZ / 2 / 1000000000; // ns -> ticks

  for (int i = 0; i < 24; i++) {
    bool bit = (grb >> (23 - i)) & 1;
    uint16_t t0h_ns = bit ? LED_T1H_NS : LED_T0H_NS;
    uint16_t t0l_ns = bit ? LED_T1L_NS : LED_T0L_NS;
    symbols[i].duration0 = (uint16_t)(t0h_ns * half_cycle);
    symbols[i].level0 = 1;
    symbols[i].duration1 = (uint16_t)(t0l_ns * half_cycle);
    symbols[i].level1 = 0;
  }

  rmt_transmit_config_t tx_cfg = {
      .loop_count = 0,
      .flags.eot_level = 0,
  };

  return rmt_transmit(g_led_chan, g_led_encoder, symbols, sizeof(symbols),
                      &tx_cfg);
}

void led_set_brightness(uint8_t brightness) { g_brightness = brightness; }

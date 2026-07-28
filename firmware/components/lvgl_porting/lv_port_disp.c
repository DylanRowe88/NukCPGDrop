#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "lvgl_port";
static spi_device_handle_t spi_dev;

#define SPI_HOST SPI2_HOST
#define PIN_CS 10
#define PIN_DC 46
#define PIN_RST -1
#define PIN_BL 45
#define DISP_H_RES 240
#define DISP_V_RES 320

static void ili9341_send_cmd(uint8_t cmd) {
  spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
  gpio_set_level(PIN_DC, 0);
  spi_device_transmit(spi_dev, &t);
}

static void ili9341_send_data(uint8_t *data, size_t len) {
  spi_transaction_t t = {.length = len * 8, .tx_buffer = data};
  gpio_set_level(PIN_DC, 1);
  spi_device_transmit(spi_dev, &t);
}

static void ili9341_send_data_byte(uint8_t data) {
  ili9341_send_data(&data, 1);
}

static void ili9341_init(void) {
  ili9341_send_cmd(0x01);
  vTaskDelay(pdMS_TO_TICKS(120)); // SW reset
  ili9341_send_cmd(0x11);
  vTaskDelay(pdMS_TO_TICKS(120)); // Sleep out
  ili9341_send_cmd(0x36);
  ili9341_send_data_byte(0x40); // MADCTL: RGB, MY=1 (portrait flip)
  ili9341_send_cmd(0x3A);
  ili9341_send_data_byte(0x55); // COLMOD: 16-bit
  ili9341_send_cmd(0x29);       // Display ON
  ESP_LOGI(TAG, "ILI9341 initialized");
}

static void ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2,
                               uint16_t y2) {
  uint8_t data[4];
  ili9341_send_cmd(0x2A); // CASET
  data[0] = x1 >> 8;
  data[1] = x1 & 0xFF;
  data[2] = x2 >> 8;
  data[3] = x2 & 0xFF;
  ili9341_send_data(data, 4);
  ili9341_send_cmd(0x2B); // PASET
  data[0] = y1 >> 8;
  data[1] = y1 & 0xFF;
  data[2] = y2 >> 8;
  data[3] = y2 & 0xFF;
  ili9341_send_data(data, 4);
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_map) {
  ili9341_set_window(area->x1, area->y1, area->x2, area->y2);
  ili9341_send_cmd(0x2C); // RAMWR
  size_t size = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) *
                sizeof(lv_color_t);
  gpio_set_level(PIN_DC, 1);
  spi_transaction_t t = {.length = size * 8, .tx_buffer = color_map};
  spi_device_transmit(spi_dev, &t);
  lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void) {
  // SPI bus
  spi_bus_config_t bus = {
      .mosi_io_num = 11,
      .miso_io_num = 13,
      .sclk_io_num = 12,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = DISP_H_RES * 40 * sizeof(lv_color_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));

  // SPI device
  spi_device_interface_config_t dev = {
      .mode = 0,
      .clock_speed_hz = 40 * 1000 * 1000,
      .spics_io_num = PIN_CS,
      .queue_size = 1,
      .flags = SPI_DEVICE_HALFDUPLEX,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev, &spi_dev));

  // GPIOs
  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_DC, 0);

  // Backlight
  gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_BL, 1);

  ili9341_init();

  // LVGL display driver
  static lv_disp_drv_t drv;
  static lv_disp_draw_buf_t draw_buf;
  static lv_color_t *buf = NULL;
  buf = heap_caps_malloc(DISP_H_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, DISP_H_RES * 40);
  lv_disp_drv_init(&drv);
  drv.hor_res = DISP_H_RES;
  drv.ver_res = DISP_V_RES;
  drv.flush_cb = flush_cb;
  drv.draw_buf = &draw_buf;
  drv.direct_mode = false;
  drv.full_refresh = false;
  drv.rotated = 0;
  lv_disp_drv_register(&drv);
  ESP_LOGI(TAG, "Display initialized (%dx%d)", DISP_H_RES, DISP_V_RES);
}

void lv_port_indev_init(void) {
  // Touch is handled by the FT6336G on I2C
  // For MVP, we skip touch input and use buttons only
  ESP_LOGI(TAG, "Touch input skipped (MVP mode)");
}

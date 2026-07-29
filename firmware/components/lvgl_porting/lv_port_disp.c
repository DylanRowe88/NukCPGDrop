#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
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

#define TOUCH_RST 18
#define TOUCH_INT 17
#define TOUCH_I2C_SDA 16
#define TOUCH_I2C_SCL 15
#define TOUCH_ADDR 0x38

static esp_lcd_touch_handle_t tp_handle = NULL;

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
  vTaskDelay(pdMS_TO_TICKS(120));

  ili9341_send_cmd(0xCF);
  ili9341_send_data_byte(0x00);
  ili9341_send_data_byte(0xC1);
  ili9341_send_data_byte(0x30);

  ili9341_send_cmd(0xED);
  ili9341_send_data_byte(0x64);
  ili9341_send_data_byte(0x03);
  ili9341_send_data_byte(0x12);
  ili9341_send_data_byte(0x81);

  ili9341_send_cmd(0xE8);
  ili9341_send_data_byte(0x85);
  ili9341_send_data_byte(0x00);
  ili9341_send_data_byte(0x78);

  ili9341_send_cmd(0xCB);
  ili9341_send_data_byte(0x39);
  ili9341_send_data_byte(0x2C);
  ili9341_send_data_byte(0x00);
  ili9341_send_data_byte(0x34);
  ili9341_send_data_byte(0x02);

  ili9341_send_cmd(0xF7);
  ili9341_send_data_byte(0x20);

  ili9341_send_cmd(0xEA);
  ili9341_send_data_byte(0x00);
  ili9341_send_data_byte(0x00);

  ili9341_send_cmd(0xC0);
  ili9341_send_data_byte(0x13);

  ili9341_send_cmd(0xC1);
  ili9341_send_data_byte(0x13);

  ili9341_send_cmd(0xC5);
  ili9341_send_data_byte(0x22);
  ili9341_send_data_byte(0x35);

  ili9341_send_cmd(0xC7);
  ili9341_send_data_byte(0xBD);

  ili9341_send_cmd(0x21);

  ili9341_send_cmd(0x36);
  ili9341_send_data_byte(0x08);

  ili9341_send_cmd(0xB6);
  ili9341_send_data_byte(0x0A);
  ili9341_send_data_byte(0xA2);

  ili9341_send_cmd(0x3A);
  ili9341_send_data_byte(0x55);

  ili9341_send_cmd(0xF6);
  ili9341_send_data_byte(0x01);
  ili9341_send_data_byte(0x30);

  ili9341_send_cmd(0xB1);
  ili9341_send_data_byte(0x00);
  ili9341_send_data_byte(0x1B);

  ili9341_send_cmd(0xF2);
  ili9341_send_data_byte(0x00);

  ili9341_send_cmd(0x26);
  ili9341_send_data_byte(0x01);

  ili9341_send_cmd(0xE0);
  ili9341_send_data_byte(0x0F);
  ili9341_send_data_byte(0x35);
  ili9341_send_data_byte(0x31);
  ili9341_send_data_byte(0x0B);
  ili9341_send_data_byte(0x0E);
  ili9341_send_data_byte(0x06);
  ili9341_send_data_byte(0x49);
  ili9341_send_data_byte(0xA7);
  ili9341_send_data_byte(0x33);
  ili9341_send_data_byte(0x07);
  ili9341_send_data_byte(0x0F);
  ili9341_send_data_byte(0x03);
  ili9341_send_data_byte(0x0C);
  ili9341_send_data_byte(0x0A);
  ili9341_send_data_byte(0x00);

  ili9341_send_cmd(0xE1);
  ili9341_send_data_byte(0x00);
  ili9341_send_data_byte(0x0A);
  ili9341_send_data_byte(0x0F);
  ili9341_send_data_byte(0x04);
  ili9341_send_data_byte(0x11);
  ili9341_send_data_byte(0x08);
  ili9341_send_data_byte(0x36);
  ili9341_send_data_byte(0x58);
  ili9341_send_data_byte(0x4D);
  ili9341_send_data_byte(0x07);
  ili9341_send_data_byte(0x10);
  ili9341_send_data_byte(0x0C);
  ili9341_send_data_byte(0x32);
  ili9341_send_data_byte(0x34);
  ili9341_send_data_byte(0x0F);

  ili9341_send_cmd(0x11);
  vTaskDelay(pdMS_TO_TICKS(120));
  ili9341_send_cmd(0x29);
  ESP_LOGI(TAG, "ILI9341 initialized");
}

static void ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2,
                               uint16_t y2) {
  uint8_t data[4];
  ili9341_send_cmd(0x2A);
  data[0] = x1 >> 8;
  data[1] = x1 & 0xFF;
  data[2] = x2 >> 8;
  data[3] = x2 & 0xFF;
  ili9341_send_data(data, 4);
  ili9341_send_cmd(0x2B);
  data[0] = y1 >> 8;
  data[1] = y1 & 0xFF;
  data[2] = y2 >> 8;
  data[3] = y2 & 0xFF;
  ili9341_send_data(data, 4);
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_map) {
  ili9341_set_window(area->x1, area->y1, area->x2, area->y2);
  ili9341_send_cmd(0x2C);
  size_t size = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) *
                sizeof(lv_color_t);
  gpio_set_level(PIN_DC, 1);
  spi_transaction_t t = {.length = size * 8, .tx_buffer = color_map};
  spi_device_transmit(spi_dev, &t);
  lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void) {
  spi_bus_config_t bus = {
      .mosi_io_num = 11,
      .miso_io_num = 13,
      .sclk_io_num = 12,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = DISP_H_RES * 40 * sizeof(lv_color_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));

  spi_device_interface_config_t dev = {
      .mode = 0,
      .clock_speed_hz = 40 * 1000 * 1000,
      .spics_io_num = PIN_CS,
      .queue_size = 1,
      .flags = SPI_DEVICE_HALFDUPLEX,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev, &spi_dev));

  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_DC, 0);
  gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_BL, 1);

  ili9341_init();

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

static int touch_log_count = 0;
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);



void lv_port_indev_init(void) {
  i2c_master_bus_handle_t bus = i2c_bus_init(TOUCH_I2C_SDA, TOUCH_I2C_SCL, 100000);
  if (!bus) {
    ESP_LOGW(TAG, "I2C init failed — touch disabled");
    return;
  }

  gpio_set_direction(TOUCH_RST, GPIO_MODE_OUTPUT);
  gpio_set_level(TOUCH_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(TOUCH_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  const esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
  esp_lcd_panel_io_handle_t tp_io = NULL;
  if (esp_lcd_new_panel_io_i2c(bus, &io_config, &tp_io) != ESP_OK) {
    ESP_LOGW(TAG, "Touch IO init failed");
    return;
  }

  esp_lcd_touch_config_t tp_cfg = {
      .x_max = DISP_H_RES,
      .y_max = DISP_V_RES,
      .rst_gpio_num = GPIO_NUM_NC,
      .int_gpio_num = GPIO_NUM_NC,
      .levels = {.reset = 0, .interrupt = 0},
      .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
  };

  if (esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &tp_handle) != ESP_OK) {
    ESP_LOGW(TAG, "FT6336G init failed — touch disabled");
    return;
  }
  ESP_LOGI(TAG, "FT6336G touch initialized");

  // Probe I2C touch directly — read multiple registers
  ESP_LOGI(TAG, "Probing FT6336G on I2C 0x%02X...", TOUCH_ADDR);
  i2c_master_bus_handle_t b = i2c_bus_get();
  if (b) {
    ESP_LOGI(TAG, "I2C bus handle OK");
    i2c_device_config_t probe_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t probe_dev = NULL;
    esp_err_t add_err = i2c_master_bus_add_device(b, &probe_cfg, &probe_dev);
    if (add_err == ESP_OK && probe_dev) {
      ESP_LOGI(TAG, "FT6336G device added OK");
      uint8_t regs[] = {0x00,0x01,0x02,0x03,0x06,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9};
      for (int ri = 0; ri < 15; ri++) {
        uint8_t r = regs[ri], v = 0;
        esp_err_t r_err = i2c_master_transmit_receive(probe_dev, &r, 1, &v, 1, pdMS_TO_TICKS(50));
        if (r_err == ESP_OK)
          ESP_LOGI(TAG, "FT6336G[0x%02X] = 0x%02X", r, v);
        else
          ESP_LOGE(TAG, "FT6336G[0x%02X] err=%s", r, esp_err_to_name(r_err));
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      i2c_master_bus_rm_device(probe_dev);
    } else {
      ESP_LOGE(TAG, "FT6336G add_device err=%s", esp_err_to_name(add_err));
    }
  } else {
    ESP_LOGE(TAG, "FT6336G i2c_bus_get NULL");
  }

  vTaskDelay(pdMS_TO_TICKS(10));

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
  if (indev)
    ESP_LOGI(TAG, "LVGL indev registered");
  else
    ESP_LOGE(TAG, "LVGL indev registration FAILED");
}

void touch_read_data(void) {}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  if (!tp_handle) return;
  esp_err_t err = esp_lcd_touch_read_data(tp_handle);
  uint8_t point_cnt = 0;
  esp_lcd_touch_point_data_t pts[1];
  esp_lcd_touch_get_data(tp_handle, pts, &point_cnt, 1);
  if (point_cnt > 0) {
    data->point.x = pts[0].x;
    data->point.y = pts[0].y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  (void)err;
}

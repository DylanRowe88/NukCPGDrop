#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "driver/spi_master.h"

static const char *TAG = "lvgl_port";

static lv_disp_t *display_handle;

void lv_port_disp_init(void) {
  // SPI bus config
  spi_bus_config_t bus = {
      .mosi_io_num = 11,
      .miso_io_num = 13,
      .sclk_io_num = 12,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 320 * 40 * sizeof(lv_color_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

  // LCD panel IO config (SPI)
  esp_lcd_panel_io_spi_config_t io_cfg = {
      .cs_gpio_num = 10,
      .dc_gpio_num = 46,
      .spi_mode = 0,
      .pclk_hz = 40 * 1000 * 1000,
      .trans_queue_depth = 10,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  esp_lcd_panel_io_handle_t io_handle;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io_handle));

  // ILI9341 panel
  esp_lcd_panel_handle_t panel_handle;
  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = -1,
      .rgb_endian = LCD_RGB_ENDIAN_BGR,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  // Add display to LVGL
  lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io_handle,
      .panel_handle = panel_handle,
      .buffer_size = 320 * 40,
      .double_buffer = true,
      .hres = 240,
      .vres = 320,
      .monochrome = false,
      .flags = {.buff_dma = true, .buff_spiram = false},
  };
  display_handle = lvgl_port_add_disp(&disp_cfg);
  ESP_LOGI(TAG, "Display initialized (240x320)");
}

void lv_port_indev_init(void) {
  // I2C for touch
  i2c_config_t i2c_cfg = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = 16,
      .scl_io_num = 15,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = 400000,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_cfg));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

  // FT5x06 / FT6336G touch
  esp_lcd_touch_handle_t touch_handle;
  esp_lcd_touch_config_t touch_cfg = {
      .x_max = 240,
      .y_max = 320,
      .rst_gpio_num = 18,
      .int_gpio_num = -1,
      .levels = {.reset = 0, .interrupt = 0},
      .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
  };
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(i2c_cfg.master.clk_speed,
                                                 &touch_cfg, &touch_handle));

  // Add touch to LVGL
  lvgl_port_touch_cfg_t touch_glue = {
      .disp = display_handle,
      .handle = touch_handle,
  };
  lvgl_port_add_touch(&touch_glue);
  ESP_LOGI(TAG, "Touch initialized (FT6336G)");
}

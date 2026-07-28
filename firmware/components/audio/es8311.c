#include "audio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define ES8311_ADDR 0x18
#define TAG "audio"

static i2c_master_dev_handle_t es8311_dev = NULL;

static esp_err_t es8311_ensure_dev(void) {
  if (es8311_dev)
    return ESP_OK;
  i2c_master_bus_handle_t bus = i2c_bus_get();
  if (!bus)
    return ESP_ERR_INVALID_STATE;
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = ES8311_ADDR,
      .scl_speed_hz = 100000,
  };
  return i2c_master_bus_add_device(bus, &dev_cfg, &es8311_dev);
}

static void es8311_write_reg(uint8_t reg, uint8_t val) {
  if (es8311_ensure_dev() != ESP_OK)
    return;
  uint8_t buf[2] = {reg, val};
  i2c_master_transmit(es8311_dev, buf, 2, pdMS_TO_TICKS(10));
}

esp_err_t audio_init(void) {
  // Get I2C bus from shared init
  i2c_bus_init(16, 15, 100000);
  es8311_ensure_dev();

  es8311_write_reg(0x01, 0x01); // Reset
  vTaskDelay(pdMS_TO_TICKS(10));
  es8311_write_reg(0x00, 0x3F); // Set slave, I2S 16-bit
  es8311_write_reg(0x10, 0x1E); // DAC power up
  es8311_write_reg(0x11, 0x7F); // ADC power up
  es8311_write_reg(0x12, 0x0C); // MCLK=384*FS
  es8311_write_reg(0x13, 0x00); // 16kHz
  ESP_LOGI(TAG, "ES8311 audio codec initialized");
  return ESP_OK;
}

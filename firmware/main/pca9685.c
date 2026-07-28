#include "pca9685.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#define PCA9685_MODE1 0x00
#define PCA9685_PRESCALE 0xFE
#define PCA9685_LED0_ON_L 0x06
#define PCA9685_SLEEP_BIT 4
#define PCA9685_AI_BIT 5

#define PCA9685_OSC_CLOCK 25000000UL

struct pca9685_t {
  i2c_master_dev_handle_t dev_handle;
  i2c_master_bus_handle_t bus_handle;
  uint8_t addr;
  uint16_t channel_cache[16];
  bool initialized;
};

static const char *TAG = "pca9685";

static esp_err_t pca9685_write_reg(pca9685_t *dev, uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  return i2c_master_transmit(dev->dev_handle, buf, 2, pdMS_TO_TICKS(10));
}

static esp_err_t pca9685_read_reg(pca9685_t *dev, uint8_t reg, uint8_t *value) {
  return i2c_master_transmit_receive(dev->dev_handle, &reg, 1, value, 1,
                                     pdMS_TO_TICKS(10));
}

esp_err_t pca9685_init(pca9685_t **out, const pca9685_config_t *cfg) {
  esp_err_t ret;
  pca9685_t *dev = calloc(1, sizeof(pca9685_t));
  if (!dev)
    return ESP_ERR_NO_MEM;

  i2c_master_bus_config_t bus_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = cfg->sda_gpio,
      .scl_io_num = cfg->scl_gpio,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };

  ret = i2c_new_master_bus(&bus_cfg, &dev->bus_handle);
  if (ret != ESP_OK) {
    free(dev);
    return ret;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = cfg->addr,
      .scl_speed_hz = cfg->clk_speed,
  };

  ret = i2c_master_bus_add_device(dev->bus_handle, &dev_cfg, &dev->dev_handle);
  if (ret != ESP_OK) {
    free(dev);
    return ret;
  }

  dev->addr = cfg->addr;
  memset(dev->channel_cache, 0, sizeof(dev->channel_cache));

  // Probe the I2C bus before any communication. If no device ACKs (e.g. in
  // QEMU which lacks I2C slave emulation) we bail out quickly instead of
  // hanging inside i2c_master_transmit when the bus never NACKs.
  ret = i2c_master_probe(dev->bus_handle, cfg->addr, 50);
  if (ret != ESP_OK) {
    free(dev);
    return ret;
  }

  ret = pca9685_write_reg(dev, PCA9685_MODE1, 0x00);
  if (ret != ESP_OK) {
    free(dev);
    return ret;
  }

  vTaskDelay(pdMS_TO_TICKS(5));
  dev->initialized = true;
  *out = dev;
  return ESP_OK;
}

esp_err_t pca9685_deinit(pca9685_t *dev) {
  if (!dev)
    return ESP_ERR_INVALID_ARG;
  free(dev);
  return ESP_OK;
}

esp_err_t pca9685_set_pwm_freq(pca9685_t *dev, uint16_t freq_hz) {
  uint8_t prescale = (uint8_t)((PCA9685_OSC_CLOCK / (4096UL * freq_hz)) - 1);
  uint8_t mode1;
  ESP_RETURN_ON_ERROR(pca9685_read_reg(dev, PCA9685_MODE1, &mode1), TAG,
                      "read mode1");

  uint8_t sleep = mode1 | (1 << PCA9685_SLEEP_BIT);
  ESP_RETURN_ON_ERROR(pca9685_write_reg(dev, PCA9685_MODE1, sleep), TAG,
                      "enter sleep");
  ESP_RETURN_ON_ERROR(pca9685_write_reg(dev, PCA9685_PRESCALE, prescale), TAG,
                      "set prescale");
  ESP_RETURN_ON_ERROR(pca9685_write_reg(dev, PCA9685_MODE1, mode1), TAG,
                      "restore mode1");

  vTaskDelay(pdMS_TO_TICKS(5));
  uint8_t restart = mode1 | (1 << PCA9685_AI_BIT);
  ESP_RETURN_ON_ERROR(pca9685_write_reg(dev, PCA9685_MODE1, restart), TAG,
                      "restart");
  return ESP_OK;
}

esp_err_t pca9685_set_channel(pca9685_t *dev, uint8_t channel, uint16_t on,
                              uint16_t off) {
  if (channel > 15)
    return ESP_ERR_INVALID_ARG;
  uint8_t reg = PCA9685_LED0_ON_L + (channel * 4);
  uint8_t buf[5] = {reg, on & 0xFF, (on >> 8) & 0xFF, off & 0xFF,
                    (off >> 8) & 0xFF};
  esp_err_t ret =
      i2c_master_transmit(dev->dev_handle, buf, 5, pdMS_TO_TICKS(10));
  if (ret == ESP_OK)
    dev->channel_cache[channel] = off;
  return ret;
}

esp_err_t pca9685_set_channel_raw(pca9685_t *dev, uint8_t channel,
                                  uint16_t value) {
  return pca9685_set_channel(dev, channel, 0, value);
}

esp_err_t pca9685_write_batch(pca9685_t *dev, const uint8_t *channels,
                              const uint16_t *values, uint8_t count) {
  if (count == 0 || count > 16)
    return ESP_ERR_INVALID_ARG;

  uint8_t first_ch = channels[0];
  uint8_t reg = PCA9685_LED0_ON_L + (first_ch * 4);
  uint8_t buf[2 + count * 2];
  buf[0] = reg;

  for (uint8_t i = 0; i < count; i++) {
    uint8_t idx = channels[i];
    buf[2 + i * 2] = values[i] & 0xFF;
    buf[2 + i * 2 + 1] = (values[i] >> 8) & 0xFF;
    dev->channel_cache[idx] = values[i];
  }

  return i2c_master_transmit(dev->dev_handle, buf, 2 + count * 2,
                             pdMS_TO_TICKS(10));
}

esp_err_t pca9685_set_outputs(pca9685_t *dev, uint8_t outputs_enabled) {
  uint8_t mode1;
  ESP_RETURN_ON_ERROR(pca9685_read_reg(dev, PCA9685_MODE1, &mode1), TAG,
                      "read mode1");
  if (outputs_enabled) {
    mode1 &= ~(1 << PCA9685_SLEEP_BIT);
  } else {
    mode1 |= (1 << PCA9685_SLEEP_BIT);
  }
  return pca9685_write_reg(dev, PCA9685_MODE1, mode1);
}

bool pca9685_is_present(pca9685_t *dev) {
  if (!dev || !dev->bus_handle)
    return false;
  esp_err_t ret = i2c_master_probe(dev->bus_handle, dev->addr, 50);
  return (ret == ESP_OK);
}

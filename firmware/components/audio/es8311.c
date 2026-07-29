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
  if (es8311_dev) return ESP_OK;
  i2c_master_bus_handle_t bus = i2c_bus_get();
  if (!bus) return ESP_ERR_INVALID_STATE;
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = ES8311_ADDR,
      .scl_speed_hz = 100000,
  };
  return i2c_master_bus_add_device(bus, &dev_cfg, &es8311_dev);
}

static void es8311_write_reg(uint8_t reg, uint8_t val) {
  if (es8311_ensure_dev() != ESP_OK) return;
  uint8_t buf[2] = {reg, val};
  i2c_master_transmit(es8311_dev, buf, 2, pdMS_TO_TICKS(10));
}

esp_err_t audio_init(void) {
  i2c_bus_init(16, 15, 100000);
  es8311_ensure_dev();

  // Reset: use hardware reset sequence from official driver
  es8311_write_reg(0x00, 0x1F);
  vTaskDelay(pdMS_TO_TICKS(20));
  es8311_write_reg(0x00, 0x00);
  vTaskDelay(pdMS_TO_TICKS(20));
  es8311_write_reg(0x00, 0x80); // Power-on command
  vTaskDelay(pdMS_TO_TICKS(20));

  // REG01 = 0x3F: enable all clocks, MCLK from MCLK pin, normal polarity
  es8311_write_reg(0x01, 0x3F);

  // Clock dividers for 16kHz @ MCLK=6.144MHz (384*16000)
  // Using coefficient: {6144000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10}
  es8311_write_reg(0x02, (0x03-1)<<5 | 0x01<<3); // pre_div=3, pre_multi=1
  es8311_write_reg(0x03, 0x00<<6 | 0x10);         // fs_mode=0, adc_osr=0x10
  es8311_write_reg(0x04, 0x10);                    // dac_osr=0x10
  es8311_write_reg(0x05, (0x01-1)<<4 | (0x01-1)); // adc_div=1, dac_div=1
  es8311_write_reg(0x06, 0x00<<5 | (0x04-1));     // sclk normal, bclk_div=4
  es8311_write_reg(0x07, 0x00);                    // lrck_h=0
  es8311_write_reg(0x08, 0xFF);                    // lrck_l=0xFF

  // Slave mode, I2S format, 16-bit
  es8311_write_reg(0x09, 3<<2); // SDP In: 16-bit
  es8311_write_reg(0x0A, 3<<2); // SDP Out: 16-bit

  // Power up analog circuitry
  es8311_write_reg(0x0D, 0x01);

  // Enable analog PGA, enable ADC modulator
  es8311_write_reg(0x0E, 0x02);

  // Enable mic bias (single-ended), set PGA gain
  es8311_write_reg(0x0F, 0x02); // bit1=mic bias enable

  // Power up DAC & ADC
  es8311_write_reg(0x10, 0x1E); // DAC & ADC power up
  es8311_write_reg(0x11, 0x7F); // ADC mixer & PGA bias

  // Power up DAC
  es8311_write_reg(0x12, 0x00);

  // Enable output to HP drive
  es8311_write_reg(0x13, 0x10);

  // MIC config: analog MIC, PGA gain max
  es8311_write_reg(0x14, 0x1A);

  // ADC gain
  es8311_write_reg(0x17, 0xC8);

  // MIC gain (24dB)
  es8311_write_reg(0x16, 6);

  // ADC Equalizer bypass, DC offset cancel
  es8311_write_reg(0x1C, 0x6A);

  // DAC volume: right and left channels
  // volume=85: 85*256/100 - 1 = 217 = 0xD9
  es8311_write_reg(0x20, 0xD9); // DAC RVOL
  es8311_write_reg(0x21, 0xD9); // DAC LVOL

  // Bypass DAC equalizer
  es8311_write_reg(0x37, 0x08);

  ESP_LOGI(TAG, "ES8311 audio codec initialized (official driver sequence)");
  return ESP_OK;
}

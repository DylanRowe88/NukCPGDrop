#include "i2c_bus.h"

static i2c_master_bus_handle_t g_bus = NULL;

i2c_master_bus_handle_t i2c_bus_init(int sda, int scl, int speed) {
  if (g_bus)
    return g_bus;
  i2c_master_bus_config_t bus_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = sda,
      .scl_io_num = scl,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  esp_err_t ret = i2c_new_master_bus(&bus_cfg, &g_bus);
  if (ret != ESP_OK)
    return NULL;
  return g_bus;
}

i2c_master_bus_handle_t i2c_bus_get(void) { return g_bus; }

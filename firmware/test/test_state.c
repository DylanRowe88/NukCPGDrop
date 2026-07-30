#include "state.h"
#include "unity.h"
#include <string.h>

TEST_CASE("state init loads defaults", "[state]") {
  esp_err_t ret = state_init();
  TEST_ASSERT_EQUAL(ESP_OK, ret);
  TEST_ASSERT_EQUAL(DIFFICULTY_SHORT, g_state.difficulty);
  TEST_ASSERT_EQUAL(16, g_state.active_servos);
}

TEST_CASE("state difficulty does not affect active_servos", "[state]") {
  state_init();
  g_state.active_servos = 7;
  state_set_difficulty(DIFFICULTY_LONG);
  TEST_ASSERT_EQUAL(DIFFICULTY_LONG, g_state.difficulty);
  TEST_ASSERT_EQUAL(7, g_state.active_servos);
}

TEST_CASE("state active_servos persists independently", "[state]") {
  state_init();
  g_state.active_servos = 7;
  g_state.range_min = 500;
  g_state.range_max = 3000;
  state_save();

  nukcpgdrop_state_t loaded;
  memset(&loaded, 0, sizeof(loaded));
  state_load(&loaded);
  TEST_ASSERT_EQUAL(7, loaded.active_servos);
  TEST_ASSERT_EQUAL(500, loaded.range_min);
  TEST_ASSERT_EQUAL(3000, loaded.range_max);
}

TEST_CASE("state sequence respects active_servos", "[state]") {
  state_init();
  g_state.active_servos = 4;
  uint8_t seq[16] = {3, 1, 2, 0};
  state_save_sequence(seq, 2);

  nukcpgdrop_state_t loaded;
  state_load(&loaded);
  TEST_ASSERT_EQUAL(2, loaded.last_completed);
  TEST_ASSERT_EQUAL(4, g_state.active_servos);
}

TEST_CASE("state defaults after reset", "[state]") {
  state_init();
  g_state.active_servos = 7;
  g_state.sv_start_pos = 45;
  g_state.sv_stop_pos = 135;

  nukcpgdrop_state_t loaded;
  state_load(&loaded);
  TEST_ASSERT_EQUAL(7, loaded.active_servos);
  TEST_ASSERT_EQUAL(45, loaded.sv_start_pos);
  TEST_ASSERT_EQUAL(135, loaded.sv_stop_pos);
}

TEST_CASE("state drop counter increments", "[state]") {
  state_init();
  uint32_t before = g_state.drop_count;
  state_increment_drop_count();
  TEST_ASSERT_EQUAL(before + 1, g_state.drop_count);
}

TEST_CASE("state drop interval", "[state]") {
  TEST_ASSERT_EQUAL(2000, state_get_drop_interval_ms(DIFFICULTY_LONG));
  TEST_ASSERT_EQUAL(500, state_get_drop_interval_ms(DIFFICULTY_SHORT));
  uint32_t rand_val = state_get_drop_interval_ms(DIFFICULTY_RANDOM);
  TEST_ASSERT(rand_val >= 300 && rand_val <= 2000);
}

TEST_CASE("state sequence save/load", "[state]") {
  state_init();
  uint8_t seq[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  state_save_sequence(seq, 2);
  nukcpgdrop_state_t loaded;
  state_load(&loaded);
  TEST_ASSERT_EQUAL(2, loaded.last_completed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(seq, loaded.last_sequence, 16);
}

TEST_CASE("state start stop defaults", "[state]") {
  state_init();
  TEST_ASSERT_EQUAL(0, g_state.sv_start_pos);
  TEST_ASSERT_EQUAL(180, g_state.sv_stop_pos);
}

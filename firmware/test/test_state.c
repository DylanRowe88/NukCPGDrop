#include "unity.h"
#include "state.h"

TEST_CASE("state init loads defaults", "[state]")
{
    esp_err_t ret = state_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(DIFFICULTY_SHORT, g_state.difficulty);
    TEST_ASSERT_FALSE(g_state.double_drop);
}

TEST_CASE("state difficulty persists", "[state]")
{
    state_init();
    state_set_difficulty(DIFFICULTY_LONG);
    TEST_ASSERT_EQUAL(DIFFICULTY_LONG, g_state.difficulty);

    nukcpgdrop_state_t loaded;
    state_load(&loaded);
    TEST_ASSERT_EQUAL(DIFFICULTY_LONG, loaded.difficulty);
}

TEST_CASE("state double drop toggle", "[state]")
{
    state_init();
    state_set_double_drop(true);
    TEST_ASSERT_TRUE(g_state.double_drop);
    state_set_double_drop(false);
    TEST_ASSERT_FALSE(g_state.double_drop);
}

TEST_CASE("state drop counter increments", "[state]")
{
    state_init();
    uint32_t before = g_state.drop_count;
    state_increment_drop_count();
    TEST_ASSERT_EQUAL(before + 1, g_state.drop_count);
}

TEST_CASE("state drop interval", "[state]")
{
    TEST_ASSERT_EQUAL(2000, state_get_drop_interval_ms(DIFFICULTY_LONG));
    TEST_ASSERT_EQUAL(500, state_get_drop_interval_ms(DIFFICULTY_SHORT));
    uint32_t rand_val = state_get_drop_interval_ms(DIFFICULTY_RANDOM);
    TEST_ASSERT(rand_val >= 300 && rand_val <= 2000);
}

TEST_CASE("state sequence save/load", "[state]")
{
    state_init();
    uint8_t seq[6] = {5, 3, 1, 0, 2, 4};
    state_save_sequence(seq, 2);

    nukcpgdrop_state_t loaded;
    state_load(&loaded);
    TEST_ASSERT_EQUAL(2, loaded.last_completed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(seq, loaded.last_sequence, 6);
}

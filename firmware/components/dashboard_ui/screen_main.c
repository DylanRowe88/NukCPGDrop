#include "screen_main.h"
#include "servos.h"
#include "state.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "screen_main";

static lv_obj_t *main_screen = NULL;

static lv_obj_t *title_label = NULL;
static lv_obj_t *rssi_label = NULL;

static lv_obj_t *can_indicators[SERVO_COUNT] = {NULL};
static lv_obj_t *can_labels[SERVO_COUNT] = {NULL};

static lv_obj_t *drop_all_btn = NULL;
static lv_obj_t *reset_btn = NULL;

static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;

static lv_obj_t *interval_slider = NULL;
static lv_obj_t *interval_label = NULL;

static lv_obj_t *dd_switch = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *battery_bar = NULL;
static lv_obj_t *battery_label = NULL;

static void drop_all_btn_cb(lv_event_t *e) { servos_release_all(); }

static void reset_btn_cb(lv_event_t *e) { servos_hold_all(); }

static void interval_slider_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  uint32_t val = (uint32_t)lv_slider_get_value(slider);
  g_state.custom_interval = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "%lu ms", val);
  lv_label_set_text(interval_label, buf);
}

static void dd_switch_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  g_state.double_drop = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

static void can_tap_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  int *idx_ptr = (int *)lv_obj_get_user_data(btn);
  if (idx_ptr) {
    uint8_t idx = (uint8_t)(*idx_ptr);
    if (servos_is_held(idx)) {
      servos_drop(idx);
    } else {
      servos_set(idx, SERVO_POSITION_HOLD);
    }
  }
}

static void set_can_color(uint8_t idx, bool held) {
  lv_color_t c = held ? lv_color_hex(0x28a745) : lv_color_hex(0xdc3545);
  lv_obj_set_style_bg_color(can_indicators[idx], c, LV_STATE_DEFAULT);
  lv_label_set_text(can_labels[idx], held ? "HELD" : "DROP");
}

static void create_top_bar(lv_obj_t *parent) {
  lv_obj_t *top = lv_obj_create(parent);
  lv_obj_set_size(top, 240, 36);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_style_bg_color(top, lv_color_hex(0x1a1a1a), LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(top, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(top, 0, LV_STATE_DEFAULT);

  title_label = lv_label_create(top);
  lv_label_set_text(title_label, "NukCPGDrop v1.0");
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xf0f0f0),
                              LV_STATE_DEFAULT);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 6, 0);

  rssi_label = lv_label_create(top);
  lv_label_set_text(rssi_label, "RSSI:--");
  lv_obj_set_style_text_color(rssi_label, lv_color_hex(0x888888),
                              LV_STATE_DEFAULT);
  lv_obj_align(rssi_label, LV_ALIGN_RIGHT_MID, -6, 0);
}

static void create_can_grid(lv_obj_t *parent) {
  static int indices[SERVO_COUNT] = {0, 1, 2, 3, 4, 5};

  int cols = 3;
  int cell_w = 74;
  int cell_h = 52;
  int start_y = 44;
  int gap_x = 6;
  int gap_y = 6;
  int start_x = (240 - (cell_w * cols + gap_x * (cols - 1))) / 2;

  for (int i = 0; i < SERVO_COUNT; i++) {
    int row = i / cols;
    int col = i % cols;
    int x = start_x + col * (cell_w + gap_x);
    int y = start_y + row * (cell_h + gap_y);

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, cell_w, cell_h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x28a745), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_user_data(btn, &indices[i]);
    lv_obj_add_event_cb(btn, can_tap_cb, LV_EVENT_CLICKED, NULL);

    can_indicators[i] = btn;

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "HELD");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
    lv_obj_center(lbl);
    can_labels[i] = lbl;
  }
}

static void create_action_buttons(lv_obj_t *parent) {
  drop_all_btn = lv_btn_create(parent);
  lv_obj_set_size(drop_all_btn, 110, 36);
  lv_obj_set_pos(drop_all_btn, 8, 160);
  lv_obj_set_style_bg_color(drop_all_btn, lv_color_hex(0xe63946),
                            LV_STATE_DEFAULT);
  lv_obj_set_style_radius(drop_all_btn, 6, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(drop_all_btn, drop_all_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *da_lbl = lv_label_create(drop_all_btn);
  lv_label_set_text(da_lbl, "DROP ALL");
  lv_obj_center(da_lbl);

  reset_btn = lv_btn_create(parent);
  lv_obj_set_size(reset_btn, 110, 36);
  lv_obj_set_pos(reset_btn, 122, 160);
  lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x28a745),
                            LV_STATE_DEFAULT);
  lv_obj_set_style_radius(reset_btn, 6, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(reset_btn, reset_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *re_lbl = lv_label_create(reset_btn);
  lv_label_set_text(re_lbl, "RESET");
  lv_obj_center(re_lbl);
}

static void create_progress(lv_obj_t *parent) {
  progress_bar = lv_bar_create(parent);
  lv_obj_set_size(progress_bar, 224, 12);
  lv_obj_set_pos(progress_bar, 8, 204);
  lv_bar_set_range(progress_bar, 0, SERVO_COUNT);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x333333),
                            LV_STATE_DEFAULT);
  lv_obj_set_style_anim_time(progress_bar, 200, LV_STATE_DEFAULT);

  progress_label = lv_label_create(parent);
  lv_label_set_text(progress_label, "0/6");
  lv_obj_set_style_text_color(progress_label, lv_color_hex(0xf0f0f0),
                              LV_STATE_DEFAULT);
  lv_obj_align_to(progress_label, progress_bar, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
}

static void create_interval_slider(lv_obj_t *parent) {
  lv_obj_t *int_label = lv_label_create(parent);
  lv_label_set_text(int_label, "Interval:");
  lv_obj_set_style_text_color(int_label, lv_color_hex(0xf0f0f0),
                              LV_STATE_DEFAULT);
  lv_obj_set_pos(int_label, 8, 224);

  interval_slider = lv_slider_create(parent);
  lv_obj_set_size(interval_slider, 140, 12);
  lv_obj_set_pos(interval_slider, 66, 226);
  lv_slider_set_range(interval_slider, 200, 5000);
  lv_slider_set_value(interval_slider, g_state.custom_interval, LV_ANIM_OFF);
  lv_obj_add_event_cb(interval_slider, interval_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  interval_label = lv_label_create(parent);
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "%lu ms", g_state.custom_interval);
  lv_label_set_text(interval_label, buf);
  lv_obj_set_style_text_color(interval_label, lv_color_hex(0xf0f0f0),
                              LV_STATE_DEFAULT);
  lv_obj_align_to(interval_label, interval_slider, LV_ALIGN_OUT_RIGHT_MID, 6,
                  0);
}

static void create_dd_toggle(lv_obj_t *parent) {
  lv_obj_t *dd_label = lv_label_create(parent);
  lv_label_set_text(dd_label, "Double Drop:");
  lv_obj_set_style_text_color(dd_label, lv_color_hex(0xf0f0f0),
                              LV_STATE_DEFAULT);
  lv_obj_set_pos(dd_label, 8, 250);

  dd_switch = lv_switch_create(parent);
  lv_obj_set_pos(dd_switch, 86, 248);
  if (g_state.double_drop) {
    lv_obj_add_state(dd_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(dd_switch, dd_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_status_line(lv_obj_t *parent) {
  status_label = lv_label_create(parent);
  lv_label_set_text(status_label, "Drops: 0  Clients: 0");
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888),
                              LV_STATE_DEFAULT);
  lv_obj_set_pos(status_label, 8, 280);
}

static void create_battery(lv_obj_t *parent) {
  battery_label = lv_label_create(parent);
  lv_label_set_text(battery_label, "Battery: --%");
  lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888888),
                              LV_STATE_DEFAULT);
  lv_obj_set_pos(battery_label, 8, 296);

  battery_bar = lv_bar_create(parent);
  lv_obj_set_size(battery_bar, 100, 10);
  lv_obj_set_pos(battery_bar, 86, 298);
  lv_bar_set_range(battery_bar, 0, 100);
  lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x333333),
                            LV_STATE_DEFAULT);
}

esp_err_t screen_main_create(void) {
  main_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x0d0d0d),
                            LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(main_screen, 0, LV_STATE_DEFAULT);

  create_top_bar(main_screen);
  create_can_grid(main_screen);
  create_action_buttons(main_screen);
  create_progress(main_screen);
  create_interval_slider(main_screen);
  create_dd_toggle(main_screen);
  create_status_line(main_screen);
  create_battery(main_screen);

  lv_scr_load(main_screen);

  ESP_LOGI(TAG, "main screen created (240x320)");
  return ESP_OK;
}

lv_obj_t *screen_main_get(void) { return main_screen; }

void screen_main_update_indicators(void) {
  for (int i = 0; i < SERVO_COUNT; i++) {
    bool held = servos_is_held((uint8_t)i);
    set_can_color((uint8_t)i, held);
  }
}

void screen_main_update_progress(void) {
  uint32_t completed = g_state.last_completed;
  if (completed > SERVO_COUNT)
    completed = SERVO_COUNT;
  lv_bar_set_value(progress_bar, completed, LV_ANIM_ON);
  char buf[8];
  lv_snprintf(buf, sizeof(buf), "%lu/%d", completed, SERVO_COUNT);
  lv_label_set_text(progress_label, buf);
}

void screen_main_update_rssi(int rssi) {
  char buf[32];
  lv_snprintf(buf, sizeof(buf), "RSSI: %d dBm", rssi);
  lv_label_set_text(rssi_label, buf);
}

void screen_main_update_interval(uint32_t interval_ms) {
  lv_slider_set_value(interval_slider, interval_ms, LV_ANIM_OFF);
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "%lu ms", interval_ms);
  lv_label_set_text(interval_label, buf);
}

void screen_main_update_double_drop(bool enabled) {
  if (enabled) {
    lv_obj_add_state(dd_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(dd_switch, LV_STATE_CHECKED);
  }
}

void screen_main_update_status(void) {
  char buf[48];
  lv_snprintf(buf, sizeof(buf), "Drops: %lu  Clients: %d", g_state.drop_count,
              0);
  lv_label_set_text(status_label, buf);
}

void screen_main_update_battery(void) {
  int pct = g_state.battery_percent;
  if (pct < 0 || pct > 100) {
    lv_label_set_text(battery_label, "Battery: --%");
    return;
  }
  char buf[24];
  lv_snprintf(buf, sizeof(buf), "Battery: %d%%", pct);
  lv_label_set_text(battery_label, buf);
  lv_bar_set_value(battery_bar, pct, LV_ANIM_OFF);
  lv_color_t bat_color =
      pct > 20 ? lv_color_hex(0x28a745) : lv_color_hex(0xe63946);
  lv_obj_set_style_bg_color(battery_bar, bat_color, LV_PART_INDICATOR);
}

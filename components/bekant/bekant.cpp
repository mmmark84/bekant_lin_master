#include "bekant.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "driver/gpio.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "bekant_cover.h"
#include "bekant_number.h"

// Bus schedule, init sequence and controller state machine follow the original IKEA controller as
// reverse engineered by Megadesk (https://github.com/gcormier/megadesk, GPL-3.0). See
// docs/protocol.md.

namespace esphome::bekant {

static const char *const TAG = "bekant";

static constexpr uint8_t ID_LEG_A = 0x08;        ///< leg A: encoder (LE) + status
static constexpr uint8_t ID_LEG_B = 0x09;        ///< leg B: encoder (LE) + status
static constexpr uint8_t ID_CYCLE_START = 0x11;  ///< master, 3 zero bytes
static constexpr uint8_t ID_FILLER = 0x10;       ///< master, no data, 6x per burst - purpose unknown
static constexpr uint8_t ID_CYCLE_END = 0x01;    ///< master, no data - purpose unknown
static constexpr uint8_t ID_COMMAND = 0x12;      ///< master: target encoder (LE) + command
static constexpr uint8_t ID_MASTER_REQUEST = 0x3C;
static constexpr uint8_t ID_SLAVE_RESPONSE = 0x3D;

static constexpr uint8_t CMD_IDLE = 0xFC;
static constexpr uint8_t CMD_PREMOVE = 0xC4;
static constexpr uint8_t CMD_RAISE = 0x86;
static constexpr uint8_t CMD_LOWER = 0x85;
static constexpr uint8_t CMD_FINE = 0x87;
static constexpr uint8_t CMD_FINISH = 0x84;
static constexpr uint8_t CMD_RECALIBRATE = 0xBD;
static constexpr uint8_t CMD_RECALIBRATE_END = 0xBC;

static constexpr uint32_t SLOT_MS = 5;        ///< frame spacing within a burst
static constexpr uint32_t INIT_SLOT_MS = 10;  ///< frame spacing during the init sequence
static constexpr uint32_t STARTUP_DELAY_MS = 250;
static constexpr uint32_t RETRY_DELAY_MS = 2000;
static constexpr uint8_t MAX_FAILED_BURSTS = 10;
static constexpr int32_t HYSTERESIS = 137;   ///< ignore moves shorter than this (encoder counts)
static constexpr int32_t MOVE_OFFSET = 159;  ///< look-ahead while a button is held
static constexpr uint32_t PUBLISH_INTERVAL_MS = 500;
static constexpr uint32_t TASK_STACK = 4096;
static constexpr UBaseType_t TASK_PRIORITY = 10;  // above the ESPHome loop task, below Wi-Fi/lwIP

static bool leg_idle(uint8_t status) { return status == 0 || status == 37 || status == 96; }

void BekantDesk::set_calibration(uint16_t raw_low, float cm_low, uint16_t raw_high, float cm_high) {
  this->cal_raw_low_ = raw_low;
  this->cal_cm_low_ = cm_low;
  this->cm_per_raw_ = (cm_high - cm_low) / static_cast<float>(raw_high - raw_low);
}

void BekantDesk::setup() {
  if (!this->lin_.begin(static_cast<uart_port_t>(this->uart_num_), this->tx_pin_, this->rx_pin_)) {
    ESP_LOGE(TAG, "UART %u setup failed", this->uart_num_);
    this->mark_failed();
    return;
  }
  // Enable the transceiver only now that TX idles high, so it can't put garbage on the bus.
  if (this->cs_pin_ >= 0) {
    const auto cs = static_cast<gpio_num_t>(this->cs_pin_);
    gpio_reset_pin(cs);
    gpio_set_direction(cs, GPIO_MODE_OUTPUT);
    gpio_set_level(cs, 1);
  }
  if (xTaskCreatePinnedToCore(task_entry_, "bekant_lin", TASK_STACK, this, TASK_PRIORITY, &this->task_,
                              tskNO_AFFINITY) != pdPASS) {
    ESP_LOGE(TAG, "Could not start LIN task");
    this->mark_failed();
  }
}

void BekantDesk::dump_config() {
  ESP_LOGCONFIG(TAG, "Bekant desk (LIN master):");
  ESP_LOGCONFIG(TAG, "  UART: %u, TX: GPIO%d, RX: GPIO%d, CS: GPIO%d", this->uart_num_, this->tx_pin_,
                this->rx_pin_, this->cs_pin_);
  ESP_LOGCONFIG(TAG, "  Height limits: %u - %u raw (%.1f - %.1f cm)", this->min_raw_, this->max_raw_,
                this->raw_to_cm(this->min_raw_), this->raw_to_cm(this->max_raw_));
  ESP_LOGCONFIG(TAG, "  Cycle gap: %u ms, max leg drift: %u", static_cast<unsigned>(this->cycle_gap_ms_),
                this->max_drift_);
  LOG_SENSOR("  ", "Height", this->height_sensor_);
  LOG_SENSOR("  ", "Raw height", this->raw_height_sensor_);
  LOG_SENSOR("  ", "Drift", this->drift_sensor_);
  LOG_TEXT_SENSOR("  ", "Status", this->status_text_sensor_);
}

// ---------------------------------------------------------------------------------------------
// Main loop side
// ---------------------------------------------------------------------------------------------

void BekantDesk::move_to_raw(uint16_t raw) {
  this->req_target_.store(std::clamp<int32_t>(raw, this->min_raw_, this->max_raw_));
}

void BekantDesk::move_to_position(float position) {
  position = std::clamp(position, 0.0f, 1.0f);
  this->move_to_raw(this->min_raw_ + static_cast<uint16_t>(lroundf(position * (this->max_raw_ - this->min_raw_))));
}

float BekantDesk::raw_to_cm(uint16_t raw) const {
  return this->cal_cm_low_ + (static_cast<float>(raw) - this->cal_raw_low_) * this->cm_per_raw_;
}

uint16_t BekantDesk::cm_to_raw(float cm) const {
  const long raw = lroundf(this->cal_raw_low_ + (cm - this->cal_cm_low_) / this->cm_per_raw_);
  return static_cast<uint16_t>(std::clamp<long>(raw, 0, UINT16_MAX));
}

float BekantDesk::raw_to_position(uint16_t raw) const {
  const float position = static_cast<float>(raw - this->min_raw_) / (this->max_raw_ - this->min_raw_);
  return std::clamp(position, 0.0f, 1.0f);
}

void BekantDesk::loop() {
  const Link link = this->link_.load();
  const Motion motion = link == Link::ONLINE ? this->motion_.load() : Motion::IDLE;
  const uint16_t raw = this->enc_a_.load();
  const uint32_t now = millis();

  // Throttle height updates while moving, but always publish where the desk ended up.
  if (link == Link::ONLINE && raw != 0 && raw != this->published_raw_ &&
      (motion == Motion::IDLE || now - this->last_height_publish_ >= PUBLISH_INTERVAL_MS)) {
    this->publish_height_(raw);
    this->last_height_publish_ = now;
  }
  if (motion != this->published_motion_)
    this->publish_motion_(motion);
  this->publish_status_(link, motion);
}

void BekantDesk::publish_height_(uint16_t raw) {
  this->published_raw_ = raw;
  if (this->height_sensor_ != nullptr)
    this->height_sensor_->publish_state(this->raw_to_cm(raw));
  if (this->raw_height_sensor_ != nullptr)
    this->raw_height_sensor_->publish_state(raw);
  if (this->drift_sensor_ != nullptr)
    this->drift_sensor_->publish_state(std::abs(static_cast<int>(raw) - static_cast<int>(this->enc_b_.load())));
  if (this->cover_ != nullptr) {
    this->cover_->position = this->raw_to_position(raw);
    this->cover_->publish_state(false);
  }
  if (this->target_height_number_ != nullptr && !this->number_published_) {
    this->target_height_number_->publish_state(this->raw_to_cm(raw));
    this->number_published_ = true;
  }
}

void BekantDesk::publish_motion_(Motion motion) {
  this->published_motion_ = motion;
  if (this->cover_ != nullptr) {
    switch (motion) {
      case Motion::UP:
        this->cover_->current_operation = cover::COVER_OPERATION_OPENING;
        break;
      case Motion::DOWN:
        this->cover_->current_operation = cover::COVER_OPERATION_CLOSING;
        break;
      default:
        this->cover_->current_operation = cover::COVER_OPERATION_IDLE;
        break;
    }
    this->cover_->publish_state(false);
  }
  // Let the slider follow the desk once it has settled (also after button/cover moves).
  if (motion == Motion::IDLE && this->target_height_number_ != nullptr && this->enc_a_.load() != 0)
    this->target_height_number_->publish_state(this->get_height_cm());
}

void BekantDesk::publish_status_(Link link, Motion motion) {
  if (this->status_text_sensor_ == nullptr)
    return;

  char status[48];
  bool fault = false;
  switch (link) {
    case Link::STARTING:
      snprintf(status, sizeof(status), "starting");
      break;
    case Link::INITIALISING:
      snprintf(status, sizeof(status), "initialising legs");
      break;
    case Link::OFFLINE:
      snprintf(status, sizeof(status), "leg 0x%02X not responding", this->silent_leg_.load());
      fault = true;
      break;
    case Link::ONLINE:
      if (this->drift_fault_.load()) {
        snprintf(status, sizeof(status), "legs out of sync - recalibrate");
        fault = true;
      } else if (motion == Motion::UP) {
        snprintf(status, sizeof(status), "moving up");
      } else if (motion == Motion::DOWN) {
        snprintf(status, sizeof(status), "moving down");
      } else if (motion == Motion::RECALIBRATING) {
        snprintf(status, sizeof(status), "recalibrating");
      } else {
        snprintf(status, sizeof(status), "idle");
      }
      break;
  }
  if (this->published_status_ == status)
    return;
  this->published_status_ = status;
  this->status_text_sensor_->publish_state(status);
  if (fault) {
    this->status_set_warning();
  } else {
    this->status_clear_warning();
  }
}

// ---------------------------------------------------------------------------------------------
// LIN task side
// ---------------------------------------------------------------------------------------------

void BekantDesk::task_entry_(void *arg) { static_cast<BekantDesk *>(arg)->task_loop_(); }

void BekantDesk::wait_(TickType_t &wake, uint32_t ms) { xTaskDelayUntil(&wake, pdMS_TO_TICKS(ms)); }

void BekantDesk::task_loop_() {
  vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));  // let the legs power up

  bool need_init = true;
  uint8_t failed_bursts = 0;
  for (;;) {
    if (need_init) {
      this->link_.store(Link::INITIALISING);
      this->reset_motion_();
      const uint8_t missing = this->lin_init_();
      if (missing > 0)
        ESP_LOGW(TAG, "Init sequence: %u leg(s) did not answer", missing);
      need_init = false;
      failed_bursts = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(this->cycle_gap_ms_));
    if (this->burst_()) {
      if (this->link_.load() != Link::ONLINE) {
        ESP_LOGI(TAG, "Legs online, height %u", this->enc_a_.load());
        this->link_.store(Link::ONLINE);
      }
      failed_bursts = 0;
      this->plan_();
      continue;
    }

    if (++failed_bursts >= MAX_FAILED_BURSTS) {
      ESP_LOGW(TAG, "Leg 0x%02X not responding, re-initialising", this->silent_leg_.load());
      this->link_.store(Link::OFFLINE);
      this->reset_motion_();
      vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
      need_init = true;
    }
  }
}

void BekantDesk::reset_motion_() {
  this->state_ = State::OFF;
  this->user_cmd_ = Command::NONE;
  this->moving_to_target_ = false;
  this->target_ = this->enc_a_.load();
  this->motion_.store(Motion::IDLE);
  this->drift_fault_.store(false);
  // Don't act on requests that piled up while the bus was down.
  this->req_target_.store(-1);
  this->req_stop_.store(false);
  this->req_recalibrate_.store(false);
}

// The original controller's start-up handshake. The two legs are identical, so the master hands
// out their node addresses with LIN diagnostic frames before the normal schedule starts. Returns
// the number of legs that never answered.
uint8_t BekantDesk::lin_init_() {
  struct Step {
    uint8_t a, b, c, d;  ///< a == 0: use the node address found so far
  };
  static const Step SEQUENCE[] = {
      {255, 7, 255, 255}, {255, 7, 255, 255}, {255, 1, 7, 255}, {208, 2, 7, 255},
      {0, 2, 7, 255},  // probe for leg A
      {0, 6, 9, 0},       {0, 6, 12, 0},      {0, 6, 13, 0},     {0, 6, 10, 0},     {0, 6, 11, 0},
      {0, 4, 0, 0},
      {0, 2, 0, 0},  // probe for leg B
      {0, 6, 9, 0},       {0, 6, 12, 0},      {0, 6, 13, 0},     {0, 6, 10, 0},     {0, 6, 11, 0},
      {0, 4, 1, 0},
      {0, 2, 1, 0},  // sweep the remaining addresses
      {208, 1, 7, 0},     {208, 2, 7, 0},
  };
  static constexpr size_t PROBE_A = 4, PROBE_B = 11, SWEEP = 18;
  static constexpr int8_t LAST_NODE = 8;
  static const uint8_t INIT_DONE[3] = {0xF6, 0xFF, 0xBF};

  uint8_t missing = 0;
  int8_t node = -1;
  TickType_t wake = xTaskGetTickCount();

  for (size_t i = 0; i < sizeof(SEQUENCE) / sizeof(SEQUENCE[0]); i++) {
    const Step &step = SEQUENCE[i];
    if (i == PROBE_A || i == PROBE_B) {
      bool found = false;
      while (!found && node < LAST_NODE) {
        node++;
        this->init_send_(wake, node, step.b, step.c, step.d);
        found = this->init_recv_(wake);
      }
      if (!found)
        missing++;
    } else if (i == SWEEP) {
      while (node < LAST_NODE) {
        node++;
        this->init_send_(wake, node, step.b, step.c, step.d);
        this->init_recv_(wake);
      }
    } else {
      this->init_send_(wake, step.a != 0 ? step.a : node, step.b, step.c, step.d);
      this->init_recv_(wake);
    }
  }

  wait_(wake, 15);
  this->lin_.send(ID_COMMAND, INIT_DONE, sizeof(INIT_DONE));
  vTaskDelay(pdMS_TO_TICKS(5));
  return missing;
}

void BekantDesk::init_send_(TickType_t &wake, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  const uint8_t frame[8] = {a, b, c, d, 0xFF, 0xFF, 0xFF, 0xFF};
  wait_(wake, INIT_SLOT_MS);
  this->lin_.send(ID_MASTER_REQUEST, frame, sizeof(frame));
}

bool BekantDesk::init_recv_(TickType_t &wake) {
  uint8_t frame[8];
  wait_(wake, INIT_SLOT_MS);
  const LinResult result = this->lin_.request(ID_SLAVE_RESPONSE, frame, sizeof(frame));
  return result != LinResult::NO_RESPONSE && result != LinResult::NO_ECHO;
}

// One cycle of the bus schedule: poll both legs, then send the command for this cycle.
bool BekantDesk::burst_() {
  static const uint8_t CYCLE_START[3] = {0, 0, 0};
  uint8_t leg_a[3];
  uint8_t leg_b[3];
  TickType_t wake = xTaskGetTickCount();

  this->lin_.send(ID_CYCLE_START, CYCLE_START, sizeof(CYCLE_START));
  wait_(wake, SLOT_MS);
  if (this->lin_.request(ID_LEG_B, leg_b, sizeof(leg_b)) != LinResult::OK) {
    this->silent_leg_.store(ID_LEG_B);
    return false;
  }
  wait_(wake, SLOT_MS);
  if (this->lin_.request(ID_LEG_A, leg_a, sizeof(leg_a)) != LinResult::OK) {
    this->silent_leg_.store(ID_LEG_A);
    return false;
  }
  for (int i = 0; i < 6; i++) {
    wait_(wake, SLOT_MS);
    this->lin_.send(ID_FILLER, nullptr, 0);
  }
  wait_(wake, SLOT_MS);
  this->lin_.send(ID_CYCLE_END, nullptr, 0);

  const uint16_t enc_a = leg_a[0] | (leg_a[1] << 8);
  const uint16_t enc_b = leg_b[0] | (leg_b[1] << 8);
  const uint8_t status_a = leg_a[2];
  const uint8_t status_b = leg_b[2];
  const uint16_t enc_min = std::min(enc_a, enc_b);
  const uint16_t enc_max = std::max(enc_a, enc_b);
  this->enc_a_.store(enc_a);
  this->enc_b_.store(enc_b);

  // Moving up, both legs aim for the lower one (and vice versa) so they stay level.
  uint16_t target = enc_a;
  uint8_t command = CMD_IDLE;
  switch (this->state_) {
    case State::OFF:
      if (this->user_cmd_ != Command::NONE && leg_idle(status_a) && leg_idle(status_b))
        this->state_ = State::STARTING;
      break;
    case State::STARTING:
      command = CMD_PREMOVE;
      if (this->user_cmd_ == Command::UP) {
        this->state_ = State::UP;
      } else if (this->user_cmd_ == Command::DOWN) {
        this->state_ = State::DOWN;
      } else {
        this->state_ = State::OFF;
      }
      break;
    case State::UP:
      target = enc_min;
      command = CMD_RAISE;
      this->last_move_up_ = true;
      if (this->user_cmd_ != Command::UP || enc_max >= this->max_raw_)
        this->state_ = State::STOPPING1;
      break;
    case State::DOWN:
      target = enc_max;
      command = CMD_LOWER;
      this->last_move_up_ = false;
      if (this->user_cmd_ != Command::DOWN || enc_min <= this->min_raw_)
        this->state_ = State::STOPPING1;
      break;
    case State::STOPPING1:
    case State::STOPPING2:
    case State::STOPPING3:
      target = static_cast<uint16_t>(std::clamp<int32_t>(this->target_, 0, UINT16_MAX));
      command = CMD_FINE;
      this->state_ = static_cast<State>(static_cast<uint8_t>(this->state_) + 1);
      break;
    case State::STOPPING4:
      target = this->last_move_up_ ? enc_min : enc_max;
      command = CMD_FINISH;
      if (leg_idle(status_a))
        this->state_ = State::OFF;
      break;
    case State::STARTING_RECAL:
      command = CMD_PREMOVE;
      this->state_ = State::RECAL;
      break;
    case State::RECAL:
      target = 0;
      command = CMD_RECALIBRATE;
      if (enc_max <= 99 && status_a == 1 && status_b == 1)
        this->state_ = State::END_RECAL;
      break;
    case State::END_RECAL:
      target = 99;
      command = CMD_RECALIBRATE_END;
      this->state_ = State::OFF;
      this->target_ = enc_max;  // don't drive back to the old target afterwards
      ESP_LOGI(TAG, "Recalibration finished");
      break;
  }

  Motion motion = Motion::IDLE;
  switch (this->state_) {
    case State::OFF:
      break;
    case State::STARTING:
      if (this->user_cmd_ != Command::NONE)
        motion = this->user_cmd_ == Command::UP ? Motion::UP : Motion::DOWN;
      break;
    case State::UP:
      motion = Motion::UP;
      break;
    case State::DOWN:
      motion = Motion::DOWN;
      break;
    case State::STOPPING1:
    case State::STOPPING2:
    case State::STOPPING3:
    case State::STOPPING4:
      motion = this->last_move_up_ ? Motion::UP : Motion::DOWN;
      break;
    case State::STARTING_RECAL:
    case State::RECAL:
    case State::END_RECAL:
      motion = Motion::RECALIBRATING;
      break;
  }
  this->motion_.store(motion);

  const uint8_t frame[3] = {static_cast<uint8_t>(target & 0xFF), static_cast<uint8_t>(target >> 8), command};
  wait_(wake, SLOT_MS);
  this->lin_.send(ID_COMMAND, frame, sizeof(frame));
  return true;
}

void BekantDesk::halt_(int32_t current) {
  // Same "smooth stop" as the original: going up, coast a little further; going down, stop now.
  this->target_ = this->target_ > current ? current + MOVE_OFFSET : current - HYSTERESIS;
  this->moving_to_target_ = false;
}

// Turns the requests from the main loop into the up/down command for the next burst.
void BekantDesk::plan_() {
  if (this->state_ >= State::STARTING_RECAL)
    return;  // recalibration runs to completion on its own

  if (this->req_recalibrate_.exchange(false)) {
    if (this->state_ == State::OFF) {
      ESP_LOGI(TAG, "Recalibrating: desk drives to the bottom end stop");
      this->state_ = State::STARTING_RECAL;
      this->user_cmd_ = Command::NONE;
      this->moving_to_target_ = false;
      return;
    }
    ESP_LOGW(TAG, "Recalibration ignored while the desk is moving");
  }

  const int32_t current = this->enc_a_.load();
  if (current <= 5) {
    this->user_cmd_ = Command::NONE;  // implausible reading, don't act on it
    return;
  }

  const int8_t button = this->button_.load();
  if (button != this->prev_button_) {
    if (button != 0 && this->moving_to_target_) {
      this->halt_(current);
      this->button_suppressed_ = true;  // this press only stops; release before jogging again
    } else if (button == 0) {
      this->button_suppressed_ = false;
    }
    this->prev_button_ = button;
  }

  if (this->req_stop_.exchange(false))
    this->halt_(current);

  const int32_t requested = this->req_target_.exchange(-1);
  if (requested >= 0) {
    this->target_ = requested;
    this->moving_to_target_ = true;
  }

  if (button != 0 && !this->button_suppressed_) {
    this->moving_to_target_ = false;
    this->target_ = button > 0 ? std::max<int32_t>(current + MOVE_OFFSET, DANGER_MIN_RAW)
                               : std::min<int32_t>(current - MOVE_OFFSET, DANGER_MAX_RAW);
  }

  const int32_t drift = std::abs(current - static_cast<int32_t>(this->enc_b_.load()));
  const bool drifting = this->max_drift_ > 0 && drift > this->max_drift_;
  if (drifting != this->drift_fault_.load()) {
    if (drifting)
      ESP_LOGW(TAG, "Legs %d counts apart, refusing to move until recalibrated", static_cast<int>(drift));
    this->drift_fault_.store(drifting);
  }
  if (drifting) {
    this->target_ = current;
    this->moving_to_target_ = false;
  }

  if (this->target_ < DANGER_MIN_RAW || this->target_ > DANGER_MAX_RAW)
    this->target_ = current;  // never aim at an end stop

  if (this->target_ > current + HYSTERESIS && current < this->max_raw_) {
    this->user_cmd_ = Command::UP;
  } else if (this->target_ < current - HYSTERESIS && current > this->min_raw_) {
    this->user_cmd_ = Command::DOWN;
  } else {
    this->user_cmd_ = Command::NONE;
    this->moving_to_target_ = false;
  }
}

}  // namespace esphome::bekant

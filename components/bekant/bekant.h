#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lin_bus.h"

namespace esphome::bekant {

class BekantCover;
class BekantTargetHeightNumber;

/// Encoder positions the original controller never drives past (Megadesk: 162 / 6777 with the
/// stop hysteresis taken off). Beyond these the legs sit at their mechanical end stops.
static constexpr uint16_t DANGER_MIN_RAW = 299;
static constexpr uint16_t DANGER_MAX_RAW = 6640;

/// What the desk is doing, as published to the main loop.
enum class Motion : uint8_t { IDLE, UP, DOWN, RECALIBRATING };
/// State of the LIN link with the legs.
enum class Link : uint8_t { STARTING, INITIALISING, ONLINE, OFFLINE };

/// LIN master replacing the IKEA Bekant up/down controller.
///
/// The bus schedule (a burst of 10 frames 5 ms apart, every cycle) runs in its own FreeRTOS task
/// so Wi-Fi and API work in the ESPHome main loop can't stretch it. The task and the main loop
/// only share the atomics below; everything else belongs to one side.
class BekantDesk : public Component {
 public:
  void set_uart_num(uint8_t uart_num) { this->uart_num_ = uart_num; }
  void set_tx_pin(int pin) { this->tx_pin_ = pin; }
  void set_rx_pin(int pin) { this->rx_pin_ = pin; }
  void set_cs_pin(int pin) { this->cs_pin_ = pin; }
  void set_height_limits_raw(uint16_t min_raw, uint16_t max_raw) {
    this->min_raw_ = min_raw;
    this->max_raw_ = max_raw;
  }
  void set_calibration(uint16_t raw_low, float cm_low, uint16_t raw_high, float cm_high);
  void set_cycle_gap_ms(uint32_t ms) { this->cycle_gap_ms_ = ms; }
  void set_max_drift(uint16_t max_drift) { this->max_drift_ = max_drift; }

  void set_cover(BekantCover *cover) { this->cover_ = cover; }
  void set_height_sensor(sensor::Sensor *sensor) { this->height_sensor_ = sensor; }
  void set_raw_height_sensor(sensor::Sensor *sensor) { this->raw_height_sensor_ = sensor; }
  void set_drift_sensor(sensor::Sensor *sensor) { this->drift_sensor_ = sensor; }
  void set_status_text_sensor(text_sensor::TextSensor *sensor) { this->status_text_sensor_ = sensor; }
  void set_target_height_number(BekantTargetHeightNumber *number) { this->target_height_number_ = number; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Commands - safe to call from the main loop and YAML lambdas.
  void move_to_raw(uint16_t raw);
  void move_to_cm(float cm) { this->move_to_raw(this->cm_to_raw(cm)); }
  /// 0.0 = lowest allowed height, 1.0 = highest.
  void move_to_position(float position);
  void stop() { this->req_stop_.store(true); }
  /// Drives both legs slowly to the bottom end stop and re-zeroes them. Only accepted when idle.
  void recalibrate() { this->req_recalibrate_.store(true); }
  /// Mirrors the original rocker: +1 while "up" is held, -1 while "down" is held, 0 on release.
  /// A press while the desk drives to a target stops it instead, like the original controller.
  void set_button(int8_t direction) { this->button_.store(direction); }

  bool is_online() const { return this->link_.load() == Link::ONLINE; }
  bool is_moving() const { return this->motion_.load() != Motion::IDLE; }
  uint16_t get_height_raw() const { return this->enc_a_.load(); }
  float get_height_cm() const { return this->raw_to_cm(this->get_height_raw()); }

  float raw_to_cm(uint16_t raw) const;
  uint16_t cm_to_raw(float cm) const;
  float raw_to_position(uint16_t raw) const;

 protected:
  /// Controller states, as in the original controller / Megadesk.
  enum class State : uint8_t {
    OFF,
    STARTING,
    UP,
    DOWN,
    STOPPING1,
    STOPPING2,
    STOPPING3,
    STOPPING4,
    STARTING_RECAL,
    RECAL,
    END_RECAL,
  };
  enum class Command : uint8_t { NONE, UP, DOWN };

  static void task_entry_(void *arg);
  void task_loop_();
  uint8_t lin_init_();
  void init_send_(TickType_t &wake, uint8_t a, uint8_t b, uint8_t c, uint8_t d);
  bool init_recv_(TickType_t &wake);
  bool burst_();
  void plan_();
  void halt_(int32_t current);
  void reset_motion_();
  static void wait_(TickType_t &wake, uint32_t ms);

  void publish_height_(uint16_t raw);
  void publish_motion_(Motion motion);
  void publish_status_(Link link, Motion motion);

  LinBus lin_;
  TaskHandle_t task_{nullptr};

  uint8_t uart_num_{1};
  int tx_pin_{-1};
  int rx_pin_{-1};
  int cs_pin_{-1};
  uint16_t min_raw_{DANGER_MIN_RAW};
  uint16_t max_raw_{DANGER_MAX_RAW};
  uint16_t cal_raw_low_{299};
  float cal_cm_low_{58.42f};
  float cm_per_raw_{(119.38f - 58.42f) / (6640 - 299)};
  uint32_t cycle_gap_ms_{50};
  uint16_t max_drift_{200};

  BekantCover *cover_{nullptr};
  sensor::Sensor *height_sensor_{nullptr};
  sensor::Sensor *raw_height_sensor_{nullptr};
  sensor::Sensor *drift_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  BekantTargetHeightNumber *target_height_number_{nullptr};

  // main loop -> LIN task
  std::atomic<int32_t> req_target_{-1};
  std::atomic<bool> req_stop_{false};
  std::atomic<bool> req_recalibrate_{false};
  std::atomic<int8_t> button_{0};

  // LIN task -> main loop
  std::atomic<uint16_t> enc_a_{0};  ///< 0 until the legs answered once
  std::atomic<uint16_t> enc_b_{0};
  std::atomic<Motion> motion_{Motion::IDLE};
  std::atomic<Link> link_{Link::STARTING};
  std::atomic<bool> drift_fault_{false};
  std::atomic<uint8_t> silent_leg_{0};  ///< LIN id of the leg that stopped answering

  // LIN task only
  State state_{State::OFF};
  Command user_cmd_{Command::NONE};
  bool last_move_up_{false};
  int32_t target_{0};
  bool moving_to_target_{false};
  int8_t prev_button_{0};
  bool button_suppressed_{false};

  // main loop only
  uint16_t published_raw_{0};
  uint32_t last_height_publish_{0};
  Motion published_motion_{Motion::IDLE};
  bool number_published_{false};
  std::string published_status_;
};

}  // namespace esphome::bekant

#include "bekant_number.h"

namespace esphome::bekant {

void BekantTargetHeightNumber::control(float value) {
  this->parent_->move_to_cm(value);
  this->publish_state(value);
}

}  // namespace esphome::bekant

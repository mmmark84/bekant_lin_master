#include "bekant_cover.h"

namespace esphome::bekant {

cover::CoverTraits BekantCover::get_traits() {
  cover::CoverTraits traits;
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  return traits;
}

void BekantCover::control(const cover::CoverCall &call) {
  if (call.get_stop()) {
    this->parent_->stop();
    return;
  }
  const auto &position = call.get_position();
  if (position.has_value())
    this->parent_->move_to_position(*position);
}

}  // namespace esphome::bekant

#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"

#include "bekant.h"

namespace esphome::bekant {

class BekantRecalibrateButton : public button::Button, public Parented<BekantDesk> {
 protected:
  void press_action() override { this->parent_->recalibrate(); }
};

}  // namespace esphome::bekant

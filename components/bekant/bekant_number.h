#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/helpers.h"

#include "bekant.h"

namespace esphome::bekant {

/// Height slider in cm; follows the actual height once the desk has stopped.
class BekantTargetHeightNumber : public number::Number, public Parented<BekantDesk> {
 protected:
  void control(float value) override;
};

}  // namespace esphome::bekant

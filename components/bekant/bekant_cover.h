#pragma once

#include "esphome/components/cover/cover.h"
#include "esphome/core/helpers.h"

#include "bekant.h"

namespace esphome::bekant {

/// The desk as a cover: open = up, closed = down, position = height between the limits.
class BekantCover : public cover::Cover, public Parented<BekantDesk> {
 public:
  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
};

}  // namespace esphome::bekant

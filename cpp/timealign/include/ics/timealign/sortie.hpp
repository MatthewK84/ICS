#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "ics/common/units.hpp"

namespace ics::timealign {

// Numbers one vehicle's sorties, its boots, from the boot times it sends, in
// the order it sends them: a sortie ends when a boot time goes back by more
// than reboot_gap from the latest, as when the vehicle restarts.
class SortieCounter {
 public:
  explicit SortieCounter(const Duration reboot_gap = std::chrono::seconds(1)) noexcept
      : reboot_gap_us_(std::chrono::duration_cast<std::chrono::microseconds>(reboot_gap).count()) {}

  // The sortie of the next boot time, counting from 0.
  [[nodiscard]] std::size_t sortie(std::int64_t boot_us) noexcept;

 private:
  std::int64_t reboot_gap_us_ = 0;
  std::optional<std::int64_t> latest_us_;
  std::size_t sortie_ = 0;
};

}  // namespace ics::timealign

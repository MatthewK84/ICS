#pragma once

#include <string_view>

#include "ics/capture/capture.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/common/error.hpp"
#include "ics/logging/logger.hpp"

namespace ics::capd {

// What ics-capd logs as each capture file closes (ICS-020).

// The counts from before to now.
[[nodiscard]] capture::Counters since(const capture::Counters& now, const capture::Counters& before) noexcept;

// Logs "file_closed" for a file of interface, with counted, the interface's
// counters while it was open, when libpcap has them, and "counters_unavailable"
// when it does not. Any packet dropped also logs "drops" as an error: the
// capture missed packets, which a run record must not hide.
void log_closed(const logging::Logger& logger, std::string_view interface, const capture::ClosedFile& file,
                const Result<capture::Counters>& counted);

}  // namespace ics::capd

// Fuzzes the ArduPilot half of the onboard-log importer (ICS-025) end to
// end, from the DataFlash reader to the records: whatever the log, the import
// must succeed, as the reader skips what it cannot read, and must keep the
// promises in sound.hpp. The corpus holds short cuts of the sample logs
// (test/logs).
#include <cstddef>
#include <cstdint>
#include <span>

#include "ics/common/error.hpp"
#include "ics/flightlog/import.hpp"
#include "sound.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  namespace fuzz = ics::flightlog::fuzz;
  const ics::Result<ics::flightlog::LogContents> contents = ics::flightlog::import_dataflash(
      std::as_bytes(std::span(data, size)), fuzz::settings(), fuzz::geoid(), fuzz::range());
  fuzz::require(contents.has_value());
  fuzz::require_sound(*contents, ics::v1::PLI_SOURCE_DATAFLASH);
  return 0;
}

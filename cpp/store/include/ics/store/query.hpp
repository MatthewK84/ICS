#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>

#include "ics/v1/pli_query.pb.h"

namespace ics::store {

// Bounds on one query (ICS-030).
struct QueryLimits {
  // The most records or events one query returns, whatever its limit_count.
  std::uint32_t max_items = 100'000;
  // The serialized size a batch is kept under; a batch of one larger item
  // passes it.
  std::size_t max_response_bytes = std::size_t{60} * 1024;
};

// The times one segment holds, so a query can skip a segment outside its
// range. A time range is valid only when its count is not zero.
struct SegmentSummary {
  // The sound bytes summarized.
  std::uint64_t bytes = 0;
  std::uint64_t records = 0;
  std::int64_t first_record_ns = 0;
  std::int64_t last_record_ns = 0;
  std::uint64_t events = 0;
  std::int64_t first_event_ns = 0;
  std::int64_t last_event_ns = 0;
};

// One segment and its summary.
struct CatalogEntry {
  std::filesystem::path path;
  SegmentSummary summary;
};

// The summaries of the segments in one folder (ICS-030), kept from one query
// to the next. Only a segment that has changed size is read again: the one
// being written.
class Catalog {
 public:
  explicit Catalog(std::filesystem::path folder) : folder_(std::move(folder)) {}

  // The segments in the folder, in name order, which is the order they
  // opened. A segment that cannot be read is left out.
  [[nodiscard]] std::vector<CatalogEntry> segments();

 private:
  std::filesystem::path folder_;
  std::map<std::filesystem::path, SegmentSummary> summaries_;
};

// Sends one response; false when it could not, which ends the query.
using SendResponse = std::function<bool(const v1::QueryPliResponse&)>;

// Answers request from the catalog's segments: the records (by valid_utc_ns)
// or events (by time_utc_ns) in its range, of its entity when it names one,
// earliest first and, at the same time, in the order stored. At most
// limit_count, or max_items when that is 0 or more. They go to send in
// batches under max_response_bytes; the last has done set, and truncated
// when more matched. A request with no kind, or whose end is not after its
// start, gets one response with done and error set. Returns how many records
// or events were sent.
std::uint64_t answer(const v1::QueryPliRequest& request, Catalog& catalog, const QueryLimits& limits,
                     const SendResponse& send);

}  // namespace ics::store

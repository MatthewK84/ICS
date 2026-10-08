#include "ics/store/query.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/store/segment_reader.hpp"
#include "ics/store/segment_writer.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::store {
namespace {

using v1::PliEvent;
using v1::PliRecord;
using v1::QueryPliRequest;
using v1::QueryPliResponse;

// A repeated field's tag and length, at most, beside each item's own bytes.
constexpr std::size_t kItemOverhead = 6;

[[nodiscard]] std::int64_t time_of(const PliRecord& record) noexcept { return record.valid_utc_ns(); }
[[nodiscard]] std::int64_t time_of(const PliEvent& event) noexcept { return event.time_utc_ns(); }
[[nodiscard]] constexpr EntryKind kind_of(const PliRecord* /*unused*/) noexcept { return EntryKind::kRecord; }
[[nodiscard]] constexpr EntryKind kind_of(const PliEvent* /*unused*/) noexcept { return EntryKind::kEvent; }
void add_to(QueryPliResponse& batch, PliRecord&& record) { *batch.add_records() = std::move(record); }
void add_to(QueryPliResponse& batch, PliEvent&& event) { *batch.add_events() = std::move(event); }

// The times of one kind of entry in a segment.
struct TimeRange {
  std::uint64_t count = 0;
  std::int64_t first = 0;
  std::int64_t last = 0;
};

void widen(TimeRange& range, const std::int64_t time) noexcept {
  range.first = range.count == 0 ? time : std::min(range.first, time);
  range.last = range.count == 0 ? time : std::max(range.last, time);
  ++range.count;
}

[[nodiscard]] Result<SegmentSummary> summarize(const std::filesystem::path& path) {
  TimeRange records;
  TimeRange events;
  const Result<std::uint64_t> bytes = read_segment(path, [&records, &events](const Entry& entry) {
    const int size = static_cast<int>(entry.payload.size());
    PliRecord record;
    PliEvent event;
    if (entry.kind == EntryKind::kRecord && record.ParseFromArray(entry.payload.data(), size)) {
      widen(records, record.valid_utc_ns());
    }
    if (entry.kind == EntryKind::kEvent && event.ParseFromArray(entry.payload.data(), size)) {
      widen(events, event.time_utc_ns());
    }
  });
  return bytes.map([&](const std::uint64_t sound) {
    return SegmentSummary{.bytes = sound,
                          .records = records.count,
                          .first_record_ns = records.first,
                          .last_record_ns = records.last,
                          .events = events.count,
                          .first_event_ns = events.first,
                          .last_event_ns = events.last};
  });
}

[[nodiscard]] TimeRange range_of(const SegmentSummary& summary, const EntryKind kind) noexcept {
  return kind == EntryKind::kRecord ? TimeRange{summary.records, summary.first_record_ns, summary.last_record_ns}
                                    : TimeRange{summary.events, summary.first_event_ns, summary.last_event_ns};
}

[[nodiscard]] bool overlaps(const TimeRange& range, const QueryPliRequest& request) noexcept {
  return range.count > 0 && range.first < request.end_utc_ns() && range.last >= request.start_utc_ns();
}

// What makes a request unanswerable; empty for none.
[[nodiscard]] std::string request_error(const QueryPliRequest& request) {
  if (request.kind() != QueryPliRequest::KIND_RECORDS && request.kind() != QueryPliRequest::KIND_EVENTS) {
    return "kind must be KIND_RECORDS or KIND_EVENTS";
  }
  if (request.end_utc_ns() <= request.start_utc_ns()) {
    return "end_utc_ns must be after start_utc_ns";
  }
  return {};
}

template <typename Message>
struct Match {
  std::int64_t time = 0;
  // Its place in the store, which orders matches at the same time.
  std::uint64_t order = 0;
  Message message;
};

template <typename Message>
[[nodiscard]] bool earlier(const Match<Message>& a, const Match<Message>& b) noexcept {
  return a.time != b.time ? a.time < b.time : a.order < b.order;
}

// The earliest matches offered, at most limit of them, in a heap whose top is
// the latest kept.
template <typename Message>
class Earliest {
 public:
  explicit Earliest(const std::size_t limit) : limit_(std::max<std::size_t>(limit, 1)) {}

  void offer(Message message, const std::int64_t time) {
    Match<Message> match{.time = time, .order = order_++, .message = std::move(message)};
    if (heap_.size() < limit_) {
      heap_.push_back(std::move(match));
      std::ranges::push_heap(heap_, earlier<Message>);
      return;
    }
    truncated_ = true;
    static_cast<void>(ics::check(!heap_.empty()));
    if (earlier(match, heap_.front())) {
      std::ranges::pop_heap(heap_, earlier<Message>);
      heap_.back() = std::move(match);
      std::ranges::push_heap(heap_, earlier<Message>);
    }
  }

  [[nodiscard]] bool truncated() const noexcept { return truncated_; }

  // The matches kept, earliest first.
  [[nodiscard]] std::vector<Match<Message>> take_sorted() {
    std::ranges::sort_heap(heap_, earlier<Message>);
    return std::move(heap_);
  }

 private:
  std::size_t limit_;
  std::vector<Match<Message>> heap_;
  std::uint64_t order_ = 0;
  bool truncated_ = false;
};

template <typename Message>
void collect(const QueryPliRequest& request, const std::vector<CatalogEntry>& segments, Earliest<Message>& earliest) {
  constexpr EntryKind kKind = kind_of(static_cast<const Message*>(nullptr));
  const auto visit = [&request, &earliest](const Entry& entry) {
    Message message;
    if (entry.kind != kKind || !message.ParseFromArray(entry.payload.data(), static_cast<int>(entry.payload.size()))) {
      return;
    }
    const std::int64_t time = time_of(message);
    const bool in_range = time >= request.start_utc_ns() && time < request.end_utc_ns();
    const bool of_entity = request.entity_id().empty() || message.entity_id() == request.entity_id();
    if (in_range && of_entity) {
      earliest.offer(std::move(message), time);
    }
  };
  for (const CatalogEntry& segment : segments) {
    if (overlaps(range_of(segment.summary, kKind), request)) {
      // A segment that has become unreadable since it was summarized gives
      // what it could.
      static_cast<void>(read_segment(segment.path, visit));
    }
  }
}

[[nodiscard]] std::uint64_t items_in(const QueryPliResponse& batch) noexcept {
  return static_cast<std::uint64_t>(batch.records_size()) + static_cast<std::uint64_t>(batch.events_size());
}

template <typename Message>
[[nodiscard]] std::uint64_t send_batches(std::vector<Match<Message>> matches, const bool truncated,
                                         const QueryLimits& limits, const SendResponse& send) {
  QueryPliResponse batch;
  std::size_t bytes = 0;
  std::uint64_t sent = 0;
  for (Match<Message>& match : matches) {
    const std::size_t size = match.message.ByteSizeLong() + kItemOverhead;
    if (bytes > 0 && bytes + size > limits.max_response_bytes) {
      if (!send(batch)) {
        return sent;
      }
      sent += items_in(batch);
      batch.Clear();
      bytes = 0;
    }
    add_to(batch, std::move(match.message));
    bytes += size;
  }
  batch.set_done(true);
  batch.set_truncated(truncated);
  return sent + (send(batch) ? items_in(batch) : 0);
}

template <typename Message>
[[nodiscard]] std::uint64_t run(const QueryPliRequest& request, const std::vector<CatalogEntry>& segments,
                                const std::size_t limit, const QueryLimits& limits, const SendResponse& send) {
  Earliest<Message> earliest(limit);
  collect(request, segments, earliest);
  const bool truncated = earliest.truncated();
  return send_batches(earliest.take_sorted(), truncated, limits, send);
}

}  // namespace

std::vector<CatalogEntry> Catalog::segments() {
  std::vector<std::filesystem::path> paths;
  std::error_code error;
  for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(folder_, error)) {
    if (is_segment_name(item.path().filename().string())) {
      paths.push_back(item.path());
    }
  }
  std::ranges::sort(paths);
  std::vector<CatalogEntry> out;
  for (const std::filesystem::path& path : paths) {
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    const auto known = summaries_.find(path);
    if (known == summaries_.end() || known->second.bytes != size) {
      const Result<SegmentSummary> made = summarize(path);
      summaries_.erase(path);
      if (made) {
        summaries_.emplace(path, *made);
      }
    }
    const auto summary = summaries_.find(path);
    if (summary != summaries_.end()) {
      out.push_back({.path = path, .summary = summary->second});
    }
  }
  return out;
}

std::uint64_t answer(const QueryPliRequest& request, Catalog& catalog, const QueryLimits& limits,
                     const SendResponse& send) {
  const std::string error = request_error(request);
  if (!error.empty()) {
    QueryPliResponse response;
    response.set_done(true);
    response.set_error(error);
    static_cast<void>(send(response));
    return 0;
  }
  const std::size_t limit =
      request.limit_count() == 0 ? limits.max_items : std::min(request.limit_count(), limits.max_items);
  const std::vector<CatalogEntry> segments = catalog.segments();
  if (request.kind() == QueryPliRequest::KIND_RECORDS) {
    return run<PliRecord>(request, segments, limit, limits, send);
  }
  return run<PliEvent>(request, segments, limit, limits, send);
}

}  // namespace ics::store

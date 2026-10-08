#include "ics/plid/ingest.hpp"

#include <filesystem>
#include <optional>
#include <utility>

#include "ics/common/check.hpp"

namespace ics::plid {

Ingest::Ingest(store::SegmentWriter writer, const IngestTiming timing, const UtcTime now)
    : writer_(std::move(writer)), timing_(timing), synced_(now) {}

Result<Ingest> Ingest::open(const std::filesystem::path& folder, const IngestTiming timing, const UtcTime now) {
  return store::SegmentWriter::open(folder, now).map([&](store::SegmentWriter writer) {
    return Ingest(std::move(writer), timing, now);
  });
}

// Counts an append; one too large for an entry is dropped rather than failing
// the service.
Status Ingest::counted(const Status appended) {
  if (appended) {
    ++stored_;
    return {};
  }
  if (appended.error() == Error::kInvalidArgument) {
    ++dropped_;
    return {};
  }
  return appended;
}

void Ingest::track_arming(const v1::PliEvent& event) {
  if (event.kind() == v1::PliEvent::KIND_ARMED) {
    armed_.insert(event.entity_id());
  }
  if (event.kind() == v1::PliEvent::KIND_DISARMED && armed_.erase(event.entity_id()) == 1) {
    sortie_ended_ = sortie_ended_ || armed_.empty();
  }
}

Status Ingest::store(Pli& pli) {
  Status appended;
  for (const v1::PliRecord& record : pli.records) {
    appended = appended.and_then([&] { return counted(writer_.append(record)); });
  }
  for (const v1::PliEvent& event : pli.events) {
    track_arming(event);
    appended = appended.and_then([&] { return counted(writer_.append(event)); });
  }
  pli = {};
  static_cast<void>(ics::check(pli.records.empty()));
  return appended;
}

Result<std::optional<std::filesystem::path>> Ingest::tick(const UtcTime now) {
  if (sortie_ended_ || writer_.due(now, timing_.rotate_interval)) {
    sortie_ended_ = false;
    synced_ = now;
    return writer_.rotate(now).map([](std::filesystem::path closed) { return std::optional(std::move(closed)); });
  }
  const bool due = now - synced_ >= timing_.sync_interval;
  synced_ = due ? now : synced_;
  return (due ? writer_.sync() : writer_.flush()).map([] { return std::optional<std::filesystem::path>{}; });
}

Result<std::filesystem::path> Ingest::close() { return writer_.close(); }

}  // namespace ics::plid

#include "ics/plid/service.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/store/archive.hpp"
#include "ics/store/query.hpp"
#include "ics/store/segment_reader.hpp"
#include "ics/store/segment_writer.hpp"

namespace ics::plid {
namespace {

// The segments in folder that no run has archived, oldest first.
[[nodiscard]] std::vector<std::filesystem::path> unarchived(const std::filesystem::path& folder) {
  std::vector<std::filesystem::path> out;
  std::error_code error;
  for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(folder, error)) {
    if (store::is_segment_name(item.path().filename().string()) && !store::is_archived(item.path())) {
      out.push_back(item.path());
    }
  }
  std::ranges::sort(out);
  return out;
}

// Cuts each unarchived segment's torn tail and queues it for archiving.
void recover(const std::filesystem::path& folder, Archiver& archiver, const logging::Logger& logger) {
  for (const std::filesystem::path& segment : unarchived(folder)) {
    const Result<std::uint64_t> kept = store::cut_torn_tail(segment);
    if (!kept) {
      logger.warn("segment_unusable", {{"segment", segment.native()}, {"error", to_string(kept.error())}});
    } else {
      logger.info("segment_recovered", {{"segment", segment.native()}, {"bytes", static_cast<std::int64_t>(*kept)}});
      archiver.add(segment);
    }
  }
}

[[nodiscard]] Result<frames::EnuFrame> range_frame(const RangeOrigin& range) {
  return frames::Geodetic::make(range.latitude, range.longitude, range.height).map([](const frames::Geodetic& origin) {
    return frames::EnuFrame(origin);
  });
}

}  // namespace

Service::Service(Feeds feeds, Ingest ingest, std::unique_ptr<Archiver> archiver, std::unique_ptr<QueryServer> query)
    : feeds_(std::move(feeds)), ingest_(std::move(ingest)), archiver_(std::move(archiver)), query_(std::move(query)) {}

Result<Service> Service::open(const Config& config, const frames::Egm96& geoid, const UtcTime now,
                              const logging::Logger& logger, std::string& reason) {
  Result<Feeds> feeds = validate(config, reason).and_then([&] { return range_frame(config.range); })
                            .and_then([&](const frames::EnuFrame& range) {
                              return open_feeds(config, geoid, range, logger, reason);
                            });
  if (!feeds) {
    return fail(feeds.error());
  }
  auto archiver = std::make_unique<Archiver>(logger, store::RowGroupLimits{});
  recover(config.store_folder, *archiver, logger);
  Result<Ingest> ingest = Ingest::open(config.store_folder, {config.rotate_interval, config.sync_interval}, now);
  if (!ingest) {
    reason = "cannot open a segment in " + config.store_folder.native();
    return fail(ingest.error());
  }
  Result<std::unique_ptr<QueryServer>> query =
      QueryServer::open(config.query_socket, config.store_folder, logger, store::QueryLimits{});
  if (!query) {
    reason = "cannot listen at " + config.query_socket.native();
    return fail(query.error());
  }
  return Service(std::move(*feeds), std::move(*ingest), std::move(archiver), std::move(*query));
}

std::vector<pollfd> Service::descriptors() const {
  std::vector<pollfd> out;
  for (const Source& source : feeds_.sources) {
    if (!source.done) {
      out.push_back(pollfd{source.capture.fd(), POLLIN, 0});
    }
  }
  if (feeds_.sapient) {
    out.push_back(feeds_.sapient->descriptor());
  }
  return out;
}

// Reads what the capture has waiting, a batch at a time; a file read to its
// end is done.
Status Service::drain(Source& source) {
  Result<std::size_t> count = std::size_t{kBatch};
  for (int batch = 0; batch < kMaxBatches && !source.done && count.value_or(0) == kBatch; ++batch) {
    count = source.capture.dispatch(feeds_.router, kBatch);
    source.done = source.file && count.value_or(1) == 0;
  }
  return count.map([](std::size_t /*unused*/) {});
}

void Service::archive(const std::filesystem::path& segment, const logging::Logger& logger) {
  logger.info("segment_closed", {{"segment", segment.native()}, {"stored", static_cast<std::int64_t>(ingest_.stored())}});
  archiver_->add(segment);
}

Status Service::step(const UtcTime now, const logging::Logger& logger) {
  Status drained;
  for (Source& source : feeds_.sources) {
    drained = drained.and_then([&] { return drain(source); });
  }
  if (feeds_.live) {
    feeds_.router.tick(now);
  }
  feeds_.router.take(pending_);
  if (feeds_.sapient) {
    feeds_.sapient->step(now, pending_.records, logger);
  }
  if (feeds_.lattice) {
    feeds_.lattice->take(pending_.records);
  }
  const Status stepped = drained.and_then([&] { return ingest_.store(pending_); })
                             .and_then([&] { return ingest_.tick(now); })
                             .map([&](const std::optional<std::filesystem::path>& closed) {
                               if (closed) {
                                 archive(*closed, logger);
                               }
                             });
  if (!stepped) {
    logger.error("store_failed", {{"error", to_string(stepped.error())}});
  }
  return stepped;
}

Status Service::close(const logging::Logger& logger) {
  const Status closed = ingest_.close().map([&](const std::filesystem::path& segment) { archive(segment, logger); });
  archiver_->wait_idle();
  return closed;
}

bool Service::replayed() const noexcept {
  return std::ranges::all_of(feeds_.sources, [](const Source& source) { return source.done; });
}

}  // namespace ics::plid

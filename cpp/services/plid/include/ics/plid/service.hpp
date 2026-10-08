#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <poll.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/logging/logger.hpp"
#include "ics/plid/archiver.hpp"
#include "ics/plid/config.hpp"
#include "ics/plid/feeds.hpp"
#include "ics/plid/ingest.hpp"
#include "ics/plid/query_server.hpp"

namespace ics::plid {

// Packets read from one capture per dispatch, and dispatches per step, so one
// busy capture cannot hold up the others for long.
inline constexpr int kBatch = 256;
inline constexpr int kMaxBatches = 16;

// ics-plid's work (ICS-030): each step reads what every feed has waiting,
// appends it to the segment log, syncs and rotates the log as due, and hands
// each closed segment to the archiver. Queries are answered meanwhile from
// the query server's thread.
class Service {
 public:
  // Opens the store, the feeds and the query socket. First it archives each
  // segment an earlier run left unarchived, cutting a tail a crash tore, and
  // warns "segment_unusable" for one it cannot read. Fails with an
  // explanation in reason: as validate and open_feeds fail; kUnwritable when
  // no segment can be opened in the store folder; and as QueryServer::open
  // fails. The geoid and logger must outlive the service.
  [[nodiscard]] static Result<Service> open(const Config& config, const frames::Egm96& geoid, UtcTime now,
                                            const logging::Logger& logger, std::string& reason);

  // What to poll: each capture not yet read to its end, and the SAPIENT
  // socket.
  [[nodiscard]] std::vector<pollfd> descriptors() const;

  // Stores what the feeds have waiting, then syncs or rotates the log as due
  // at now. Logs "replayed" once every capture, all of them files, has been
  // read to its end. On a failure to store, logs "store_failed" and returns
  // it.
  [[nodiscard]] Status step(UtcTime now, const logging::Logger& logger);

  // Closes the current segment and waits until every closed segment is
  // archived. Fails as the close does.
  [[nodiscard]] Status close(const logging::Logger& logger);

  // Whether every capture is a file read to its end.
  [[nodiscard]] bool replayed() const noexcept;

  [[nodiscard]] const Ingest& ingest() const noexcept { return ingest_; }
  [[nodiscard]] const Feeds& feeds() const noexcept { return feeds_; }

 private:
  Service(Feeds feeds, Ingest ingest, std::unique_ptr<Archiver> archiver, std::unique_ptr<QueryServer> query);
  [[nodiscard]] Status drain(Source& source);
  void archive(const std::filesystem::path& segment, const logging::Logger& logger);

  Feeds feeds_;
  Ingest ingest_;
  // Destroyed before the feeds: the archiver finishes, then the query server
  // stops.
  std::unique_ptr<Archiver> archiver_;
  std::unique_ptr<QueryServer> query_;
  Pli pending_;
  bool replay_logged_ = false;
};

}  // namespace ics::plid

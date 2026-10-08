// Fuzzes the PLI segment reader (ICS-030). The input is a segment file's
// bytes, as a crash or a bad disk may leave them. They are read twice: by
// parse_magic and parse_entry over the bytes in memory, and by SegmentReader
// from an in-memory file. Both must agree, and keep their promises:
// - every entry lies within the bytes, after the one before;
// - the sound bytes end where the last entry ends;
// - an entry whose payload parses as its message is written back by
//   append_entry to an entry that parses the same way.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "ics/store/segment_format.hpp"
#include "ics/store/segment_reader.hpp"
#include "ics/timing/unix_socket.hpp"

namespace {

using ics::store::Entry;
using ics::store::EntryKind;
using ics::store::Found;

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

// Writes entry's message back, when it parses, and requires the result to
// parse to the same bytes.
template <typename Message>
void require_round_trip(const Entry& entry) {
  Message message;
  if (!message.ParseFromArray(entry.payload.data(), static_cast<int>(entry.payload.size()))) {
    return;
  }
  std::vector<std::byte> written;
  require(ics::store::append_entry(message, written).has_value());
  const ics::store::Parsed parsed = ics::store::parse_entry(written);
  require(parsed.found == Found::kEntry && parsed.size == written.size() && parsed.entry.kind == entry.kind);
  Message again;
  require(again.ParseFromArray(parsed.entry.payload.data(), static_cast<int>(parsed.entry.payload.size())));
  require(again.SerializeAsString() == message.SerializeAsString());
}

// The entries in bytes and the sound bytes, read in memory.
std::vector<Entry> parse_all(const std::span<const std::byte> bytes, std::size_t& sound) {
  std::vector<Entry> entries;
  sound = 0;
  if (ics::store::parse_magic(bytes) != Found::kEntry) {
    return entries;
  }
  sound = ics::store::kSegmentMagic.size();
  for (ics::store::Parsed parsed = ics::store::parse_entry(bytes.subspan(sound)); parsed.found == Found::kEntry;
       parsed = ics::store::parse_entry(bytes.subspan(sound))) {
    require(parsed.size == ics::store::kEntryHeaderBytes + parsed.entry.payload.size());
    require(parsed.size <= bytes.size() - sound);
    require(parsed.entry.payload.data() == bytes.subspan(sound + ics::store::kEntryHeaderBytes).data());
    entries.push_back(parsed.entry);
    sound += parsed.size;
  }
  return entries;
}

// What SegmentReader reads from a file holding bytes.
void require_reader_agrees(const std::span<const std::byte> bytes, const std::vector<Entry>& entries,
                           const std::size_t sound) {
  const ics::timing::Fd file(::memfd_create("segment", MFD_CLOEXEC));
  require(file.valid());
  require(::write(file.get(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  ics::Result<ics::store::SegmentReader> reader =
      ics::store::SegmentReader::open("/proc/self/fd/" + std::to_string(file.get()));
  require(reader.has_value());
  std::size_t index = 0;
  ics::Result<std::optional<Entry>> entry = reader->next();
  for (; entry && entry->has_value(); entry = reader->next()) {
    require(index < entries.size());
    require((*entry)->kind == entries[index].kind);
    require(std::ranges::equal((*entry)->payload, entries[index].payload));
    ++index;
  }
  const bool malformed = ics::store::parse_magic(bytes) == Found::kInvalid;
  require(entry.has_value() != malformed);
  require(index == entries.size());
  require(reader->sound_bytes() == sound);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> input = std::as_bytes(std::span<const std::uint8_t>(data, size));
  std::size_t sound = 0;
  const std::vector<Entry> entries = parse_all(input, sound);
  for (const Entry& entry : entries) {
    if (entry.kind == EntryKind::kRecord) {
      require_round_trip<ics::v1::PliRecord>(entry);
    } else {
      require_round_trip<ics::v1::PliEvent>(entry);
    }
  }
  require_reader_agrees(input, entries, sound);
  return 0;
}

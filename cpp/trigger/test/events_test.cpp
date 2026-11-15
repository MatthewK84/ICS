// events_test.cpp: the trigger event file, written and read back (ICS-040).
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/store/crc32c.hpp>
#include <ics/trigger/events.h>
#include <ics/v1/trigger_event.pb.h>

namespace
{

using ics::int32;
using ics::uint32;
using ics::trigger::Event_file;
using ics::trigger::Events;
using ics::trigger::read_events;
using ics::v1::TriggerEvent;

constexpr ics::int64 fired_ns = 1760000008400000000;
constexpr ics::float64 miss_m = 0.25;
constexpr int32 word_bytes = 4;
constexpr int32 byte_bits = 8;
constexpr uint32 byte_mask = 0xFFU;
constexpr uint32 too_long = 65537U;
constexpr std::size_t huge = 70000U;
constexpr ics::int64 tail = 3;

TriggerEvent event_of(const TriggerEvent::Kind kind, const std::string& detail)
{
  TriggerEvent event;
  event.set_sequence_id("sequence-1");
  event.set_time_utc_ns(fired_ns);
  event.set_kind(kind);
  event.set_target_entity_id("uas-1");
  event.set_interceptor_entity_id("int-1");
  event.set_predicted_closest_approach_utc_ns(fired_ns);
  event.set_predicted_miss_distance_m(miss_m);
  event.add_channels("A");
  event.set_detail(detail);
  return event;
}

// A file's bytes with two events in it.
std::string two_events()
{
  std::string bytes(ics::trigger::event_magic);
  EXPECT_TRUE(ics::trigger::append_event(event_of(TriggerEvent::KIND_ARMED, ""), bytes).has_value());
  EXPECT_TRUE(ics::trigger::append_event(event_of(TriggerEvent::KIND_FIRED, "on time"), bytes).has_value());
  return bytes;
}

// A number as four bytes, the lowest first.
std::string word(const uint32 value)
{
  std::string bytes;
  for (int32 byte = 0; byte < word_bytes; ++byte)
  {
    bytes.push_back(static_cast<char>((value >> static_cast<uint32>(byte_bits * byte)) & byte_mask));
  }
  return bytes;
}

// The magic and one entry with a payload, its length as given and its checksum right.
std::string entry_of(const std::string& payload, const uint32 length)
{
  return std::string(ics::trigger::event_magic) + word(length) +
         word(ics::store::crc32c(std::as_bytes(std::span(payload)))) + payload;
}

std::string path_in_temp(const std::string& name)
{
  return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Events, AFileGivesBackTheEventsPutInIt)
{
  const std::string path = path_in_temp("ics-trigger-events-test.icstrg");
  {
    ics::Result<std::unique_ptr<Event_file>> made = Event_file::create(path);
    ASSERT_TRUE(made.has_value());
    const std::unique_ptr<Event_file> file = std::move(*made);
    EXPECT_TRUE(file->put(event_of(TriggerEvent::KIND_ARMED, "")).has_value());
    EXPECT_TRUE(file->put(event_of(TriggerEvent::KIND_FIRED, "on time")).has_value());
  }
  const ics::Result<Events> loaded = ics::trigger::load_events(path);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_TRUE(loaded->whole);
  ASSERT_EQ(loaded->events.size(), 2U);
  EXPECT_EQ(loaded->events[0].kind(), TriggerEvent::KIND_ARMED);
  EXPECT_EQ(loaded->events[1].kind(), TriggerEvent::KIND_FIRED);
  EXPECT_EQ(loaded->events[1].detail(), "on time");
  EXPECT_EQ(loaded->events[1].time_utc_ns(), fired_ns);
  EXPECT_EQ(loaded->events[1].channels(0), "A");
  std::ifstream written(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
  EXPECT_EQ(bytes, two_events());
  static_cast<void>(std::filesystem::remove(path));
}

TEST(Events, AFileWithNoEventsIsEmptyAndWhole)
{
  const Events none = read_events("");
  EXPECT_TRUE(none.whole);
  EXPECT_TRUE(none.events.empty());
  const Events started = read_events(ics::trigger::event_magic);
  EXPECT_TRUE(started.whole);
  EXPECT_TRUE(started.events.empty());
}

TEST(Events, APartWrittenLastEntryIsLeftOut)
{
  const std::string bytes = two_events();
  const std::size_t kept = static_cast<std::size_t>(static_cast<ics::int64>(bytes.size()) - tail);
  const Events cut = read_events(std::string_view(bytes).substr(0U, kept));
  EXPECT_FALSE(cut.whole);
  EXPECT_EQ(cut.events.size(), 1U);
  const Events trailing = read_events(bytes + "abc");
  EXPECT_FALSE(trailing.whole);
  EXPECT_EQ(trailing.events.size(), 2U);
}

TEST(Events, AFileWithoutTheMagicHoldsNothing)
{
  const Events other = read_events("ICSPLI1\nabcdefgh");
  EXPECT_FALSE(other.whole);
  EXPECT_TRUE(other.events.empty());
}

TEST(Events, AnEntryThatFailsItsChecksIsLeftOut)
{
  std::string corrupt = two_events();
  corrupt.back() = '!';
  const Events checked = read_events(corrupt);
  EXPECT_FALSE(checked.whole);
  EXPECT_EQ(checked.events.size(), 1U);
  const Events oversized = read_events(entry_of("payload", too_long));
  EXPECT_FALSE(oversized.whole);
  EXPECT_TRUE(oversized.events.empty());
  // A payload whose checksum is right, but which is not a message: field number 0.
  const std::string not_a_message("\x07", 1U);
  const Events unparsed = read_events(entry_of(not_a_message, static_cast<uint32>(not_a_message.size())));
  EXPECT_FALSE(unparsed.whole);
  EXPECT_TRUE(unparsed.events.empty());
}

TEST(Events, AnEventTooLargeForAnEntryIsRefused)
{
  std::string bytes;
  const TriggerEvent large = event_of(TriggerEvent::KIND_FAULT, std::string(huge, 'x'));
  EXPECT_EQ(ics::trigger::append_event(large, bytes).error(), ics::Error::kInvalidArgument);
  EXPECT_TRUE(bytes.empty());
}

TEST(Events, AFileThatCannotBeOpenedOrWrittenSaysSo)
{
  EXPECT_EQ(Event_file::create("/no-such-folder/events.icstrg").error(), ics::Error::kUnwritable);
  EXPECT_EQ(ics::trigger::load_events("/no-such-folder/events.icstrg").error(), ics::Error::kUnreadable);
  ics::Result<std::unique_ptr<Event_file>> full = Event_file::create("/dev/full");
  ASSERT_TRUE(full.has_value());
  const std::unique_ptr<Event_file> file = std::move(*full);
  EXPECT_EQ(file->put(event_of(TriggerEvent::KIND_ARMED, "")).error(), ics::Error::kUnwritable);
  const TriggerEvent large = event_of(TriggerEvent::KIND_FAULT, std::string(huge, 'x'));
  EXPECT_EQ(file->put(large).error(), ics::Error::kInvalidArgument);
}

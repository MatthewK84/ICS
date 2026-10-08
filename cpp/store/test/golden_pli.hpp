#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "ics/v1/common.pb.h"
#include "ics/v1/pli.pb.h"
#include "store_support.hpp"

namespace ics::store::testing {

// The PLI in golden/pli (ICS-030): records and events that reach every kind
// of column value, null included. python/tests/test_pli_parquet.py reads the
// Parquet files the store writes from them with pyarrow.
inline std::vector<v1::PliRecord> golden_records() {
  std::vector<v1::PliRecord> out;
  v1::PliRecord full = record(0);
  full.set_received_utc_ns(kStartNs + 2'500'000);
  full.mutable_velocity_enu_mps()->set_east(-3.25);
  full.mutable_velocity_enu_mps()->set_north(4.5);
  full.mutable_velocity_enu_mps()->set_up(0.125);
  full.set_vertical_sigma_m(2.75);
  full.mutable_attitude()->set_w(0.5);
  full.mutable_attitude()->set_x(-0.5);
  full.mutable_attitude()->set_y(0.5);
  full.mutable_attitude()->set_z(-0.5);
  out.push_back(full);
  out.push_back(record(1));
  v1::PliRecord unnamed = record(2);
  unnamed.set_role(static_cast<v1::EntityRole>(99));
  unnamed.set_entity_id("drone-\xC3\xA9\xE2\x9C\x88");
  out.push_back(unnamed);
  v1::PliRecord receipt = record(3);
  receipt.set_source(v1::PLI_SOURCE_COT);
  receipt.set_time_basis(v1::PLI_TIME_BASIS_RECEIPT);
  receipt.set_valid_utc_ns(std::numeric_limits<std::int64_t>::min());
  receipt.mutable_position()->set_height_ellipsoid_m(-12.5);
  out.push_back(receipt);
  out.push_back(v1::PliRecord{});
  for (int index = 5; index < 7; ++index) {
    out.push_back(record(index));
  }
  return out;
}

inline std::vector<v1::PliEvent> golden_events() {
  std::vector<v1::PliEvent> out;
  out.push_back(event(0));
  v1::PliEvent command = event(1);
  command.set_kind(v1::PliEvent::KIND_COMMAND);
  command.set_command(std::numeric_limits<std::uint32_t>::max());
  command.set_command_result(4);
  out.push_back(command);
  out.push_back(v1::PliEvent{});
  v1::PliEvent text = event(3);
  text.set_detail("Mode \xE2\x86\x92 AUTO");
  out.push_back(text);
  return out;
}

}  // namespace ics::store::testing

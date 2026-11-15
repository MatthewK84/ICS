// controller.h: the trigger controller for one station (ICS-040, CPP-21). It
// fires the station's cameras at an engagement's closest approach.
//
// A sequence runs from the engage command to its pulse:
// - idle: nothing is armed. Reports are not taken.
// - armed: the engage command named a target and an interceptor. Their
//   reports are filtered as the cue's are (ICS-037), and each step predicts
//   their closest approach. The pulse is asked for a lead before it.
// - committed: the pulse generator holds the shot, the PPS edge the pulse is
//   timed from and the delay from it, and is armed for that edge. Until a
//   command margin before the edge, a prediction that moves the pulse by more
//   than the recommit threshold programs the delay again, and one that moves
//   it to a later edge withdraws the shot. After that the shot stands. When
//   its time has passed, the generator is disarmed and the sequence is over.
//
// A sequence also ends, disarmed, on the operator's command, when the
// clock is not locked within its bound, or when no pulse has gone out by the
// arming timeout; and, faulted, when the generator refuses a command. Every
// step is an ics.v1.TriggerEvent, given to the event sink.
//
// The controller is used from one thread. An event allocates, as its message
// does: the pulse's time is the generator's, not the host's.
#ifndef ICS_TRIGGER_CONTROLLER_H
#define ICS_TRIGGER_CONTROLLER_H

// JSF deviation D-6: ics::Result, ics::Duration, ics::UtcTime and ics::frames come from .hpp headers.
#include <array>
#include <memory>
#include <optional>
#include <string>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/frames/enu.hpp>
#include <ics/trigger/approach.h>
#include <ics/trigger/events.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/shot.h>
#include <ics/v1/pli.pb.h>
#include <ics/v1/trigger_event.pb.h>

namespace ics::trigger
{

struct Trigger_settings
{
  // The range, and the filter and the sigmas each vehicle's reports get
  // (ICS-037). The station, the target role and the command latency are not
  // used.
  cue::Cue_settings cue;
  Approach_settings approach;
  Shot_settings shot;
  // How long before the closest approach the pulse goes out.
  Duration lead;
  // A delay this far from the one programmed is programmed again.
  Duration recommit;
  // The least time between two events that report a prediction.
  Duration predict_period;
  // How long a sequence waits for its pulse.
  Duration arm_timeout;
  // The largest clock error bound a pulse is timed by.
  Duration max_clock_bound;
  // The generator's channels, as the events name them, and how many there are.
  std::array<std::string, channels_length> channels;
  int32 channel_count;
};

// Whether a controller can be made with the settings: approach and shot
// settings that are valid(), times that are not negative, and 1 to 8 named
// channels. kInvalidArgument otherwise.
[[nodiscard]] Status valid(const Trigger_settings& settings);

// The station's clock, as ics-timingd reports it (ICS-019).
struct Clock_state
{
  bool locked;
  // The bound on its error.
  Duration bound;
};

enum class Stage : uint8
{
  idle,
  armed,
  committed
};

// What became of a report.
enum class Take : uint8
{
  // Given to the filter of the vehicle it reports.
  given,
  // Not of the sequence's target or interceptor, or there is no sequence.
  unlisted,
  // Of one of them, but not a position the filter can take.
  unusable
};

class Controller
{
public:
  // A controller, for settings that are valid() and a range origin on the
  // Earth; kInvalidArgument or kOutOfRange otherwise. The generator and the
  // sink must outlive it.
  [[nodiscard]] static Result<std::unique_ptr<Controller>> make(const Trigger_settings& settings,
                                                                Pulse_generator& generator,
                                                                Event_sink& sink);

  // The engage command: arms for a target and an interceptor, as
  // PliRecord.entity_id names them. kInvalidArgument for names that are empty
  // or the same, and kFull while a sequence is running.
  [[nodiscard]] Status engage(const std::string& target,
                              const std::string& interceptor,
                              const UtcTime now);
  // The operator's disarm command; nothing when idle.
  void disarm(const std::string& why, const UtcTime now);
  // Takes a report of any vehicle.
  [[nodiscard]] Take take(const v1::PliRecord& record);
  // Moves the sequence on to now.
  void step(const UtcTime now, const Clock_state& clock);

  [[nodiscard]] Stage stage() const;
  // The shot the generator holds, while committed.
  [[nodiscard]] std::optional<Shot> shot() const;
  // The events the sink could not keep.
  [[nodiscard]] int64 lost_events() const;

private:
  Controller(const Trigger_settings& settings,
             const frames::EnuFrame& range,
             Pulse_generator& generator,
             Event_sink& sink);

  // Gives the sink an event of the sequence.
  void emit(const v1::TriggerEvent::Kind kind,
            const UtcTime time,
            const std::string& detail);
  // Ends the sequence with an event, disarming the generator if it is armed.
  void end(const v1::TriggerEvent::Kind kind,
           const UtcTime now,
           const std::string& detail);
  // The closest approach predicted from the filters at now.
  [[nodiscard]] std::optional<Approach> predicted(const UtcTime now) const;
  // Follows a prediction: reports it, and aims the shot until the shot stands.
  void follow(const UtcTime now, const Approach& approach);
  // Withdraws a shot for another edge, and commits the shot wanted when its window is open.
  void aim(const UtcTime now, const Shot& wanted);
  // Gives the generator a shot; a refusal ends the sequence, faulted.
  void commit(const UtcTime now, const Shot& wanted);

  Trigger_settings settings_;
  frames::EnuFrame range_;
  Pulse_generator& generator_;
  Event_sink& sink_;
  cue::Cue_filter target_filter_;
  cue::Cue_filter interceptor_filter_;
  Stage stage_ = Stage::idle;
  std::string target_;
  std::string interceptor_;
  std::string sequence_id_;
  int64 sequences_ = 0;
  UtcTime armed_at_{};
  std::optional<Approach> approach_;
  // The prediction the shot was last timed by.
  std::optional<Approach> aimed_;
  std::optional<UtcTime> reported_at_;
  Shot shot_{};
  int64 lost_events_ = 0;
};

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_CONTROLLER_H

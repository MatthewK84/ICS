// main.cpp: ics-tracker-monitor (ICS-038), the operational test of the
// tracking camera. It streams the camera named in the [tracker] table of
// /etc/ics/ics-tracker.toml and writes what it finds in each frame as a JSON
// line. The config path is not an argument: CodeQL's path-injection rule
// rejects a path taken from the command line. See ics/tracker/monitor.h and
// cpp/README.md.
//
// Usage: ics-tracker-monitor FRAMES
#include <cstddef>
#include <iostream>
#include <span>

#include <ics/tracker/monitor.h>

// JSF deviation D-7: the C++ standard fixes main's signature (AV 97 and 209).
int main(const int argc, char* argv[])
{
  return ics::tracker::run_monitor(std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
                                   "/etc/ics/ics-tracker.toml",
                                   std::cout,
                                   std::cerr);
}

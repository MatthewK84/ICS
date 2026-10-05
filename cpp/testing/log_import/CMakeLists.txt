# The onboard-log import tool (ICS-025, deploy/sitl): runs a PX4 ULog or
# ArduPilot DataFlash log through ics::flightlog, so a flight's logs can be
# checked against what ICS recorded live. Test equipment; it ships nowhere.
add_executable(ics-log-import main.cpp)
target_link_libraries(ics-log-import PRIVATE ics::flightlog)

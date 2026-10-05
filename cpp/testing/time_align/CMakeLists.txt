# The time alignment tool (ICS-026, deploy/sitl): fits each vehicle's boot
# clock to UTC per sortie from a TAP capture, and checks the fit against an
# injected drift. Test equipment; it ships nowhere.
add_executable(ics-time-align main.cpp output.cpp sorties.cpp)
target_link_libraries(ics-time-align PRIVATE ics::capture ics::flightlog ics::mavlink ics::timealign)

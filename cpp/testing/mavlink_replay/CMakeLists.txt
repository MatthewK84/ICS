# The MAVLink replay (ICS-021, deploy/sitl): runs a pcap file of a TAP port
# through the MAVLink adapter, so the SITL rig's captures can be checked
# against its truth log. Test equipment; it ships nowhere.
add_executable(ics-mavlink-replay main.cpp)
target_link_libraries(ics-mavlink-replay PRIVATE ics::capture ics::mavlink)

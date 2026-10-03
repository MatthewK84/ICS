# The capture bench's own tools (ICS-020, deploy/capture-bench): a UDP load
# generator and a checker of the files ics-capd writes. Test equipment; they
# ship nowhere.
add_executable(ics-capture-send send.cpp)
target_link_libraries(ics-capture-send PRIVATE ics::common)

add_executable(ics-capture-verify verify.cpp)
target_link_libraries(ics-capture-verify PRIVATE ics::capture)

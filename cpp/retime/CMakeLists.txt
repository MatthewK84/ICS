# ics::retime (ICS-026, ICS-030): times a sortie's PLI by its vehicles' clock
# fits, from the sortie's TAP captures and onboard logs. ics-time-align
# reports on it, and ics-pli-import stores what it gives.
add_library(ics_retime STATIC src/retime.cpp src/sorties.cpp)
add_library(ics::retime ALIAS ics_retime)
target_include_directories(ics_retime PUBLIC include)
target_link_libraries(ics_retime PUBLIC ics::capture ics::common ics::flightlog ics::frames ics::mavlink ics::proto
                                        ics::timealign)

ics_add_gtest(retime_test SOURCES test/retime_test.cpp test/sorties_test.cpp LIBRARIES ics::retime)
target_include_directories(retime_test PRIVATE test ${PROJECT_SOURCE_DIR}/timing/test ${PROJECT_SOURCE_DIR}/capture/test)
target_compile_definitions(retime_test PRIVATE
  ICS_EGM96_PATH="${ICS_GEOID_DIR}/egm96-5.pgm"
  ICS_MAVLINK_FIXTURES="${PROJECT_SOURCE_DIR}/mavlink/test/fixtures")

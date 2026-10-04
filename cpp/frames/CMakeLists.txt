# ics::frames (ICS-017): WGS84 geodetic, ECEF and range ENU conversions, EGM96
# heights, and UTM positions (ICS-024); see docs/frames-and-time.md. Parts are
# ported from GeographicLib 2.3 under its MIT License
# (GEOGRAPHICLIB-LICENSE.txt), and the tests match GeographicLib's golden
# vectors in golden/frames.
set(ICS_GEOID_DIR "/usr/share/GeographicLib/geoids" CACHE PATH "Folder holding the EGM96 egm96-5 grid, where the ICS images install it")

add_library(ics_frames STATIC src/degrees.cpp src/egm96.cpp src/enu.cpp src/geodetic.cpp src/utm.cpp)
add_library(ics::frames ALIAS ics_frames)
target_include_directories(ics_frames PUBLIC include)
target_link_libraries(ics_frames PUBLIC ics::common)

ics_add_gtest(frames_test
  SOURCES
    test/degrees_test.cpp
    test/egm96_test.cpp
    test/enu_test.cpp
    test/geodetic_test.cpp
    test/golden_test.cpp
    test/utm_test.cpp
  LIBRARIES ics::frames)
target_include_directories(frames_test PRIVATE src)
target_compile_definitions(frames_test PRIVATE
  ICS_GOLDEN_FRAMES_DIR="${PROJECT_SOURCE_DIR}/../golden/frames"
  ICS_EGM96_PATH="${ICS_GEOID_DIR}/egm96-5.pgm")

# The geoid grid reader parses a file, so it is fuzzed (ICS-008).
ics_add_fuzzer(egm96_fuzz SOURCES fuzz/egm96_fuzz.cpp LIBRARIES ics::frames CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)

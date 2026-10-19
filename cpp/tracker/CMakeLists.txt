# ics::tracker (ICS-038, CPP-19): the closed-loop tracker for one station. It
# streams the narrow-field tracking camera through Aravis, centroids the
# target on every frame, tracks its sky direction and gives the rate loop
# (ICS-033) its demand. Held to the JSF AV rules (docs/cpp-jsf-av.md);
# python/ics_models/tracker is its reference. See cpp/README.md.
find_package(aravis CONFIG REQUIRED)
find_package(glib CONFIG REQUIRED)

add_library(ics_tracker STATIC
  src/aravis_camera.cpp
  src/camera.cpp
  src/centroid.cpp
  src/config.cpp
  src/demand.cpp
  src/estimator.cpp
  src/geometry.cpp
  src/monitor.cpp
  src/tracker.cpp)
add_library(ics::tracker ALIAS ics_tracker)
target_include_directories(ics_tracker PUBLIC include)
target_link_libraries(ics_tracker
  PUBLIC ics::common ics::config ics::encoder ics::mount
  PRIVATE aravis::aravis glib::glib-2.0 glib::gobject-2.0)
# Aravis needs GLib's gio itself, and a linker with --as-needed (GCC's on
# Ubuntu) leaves gio out of the executable. The executable's DT_RUNPATH is not
# searched for a library's own dependencies, so the host's gio would load
# against Conan's glib. DT_RPATH is searched for them, so every executable
# that links the tracker gets Conan's GLib throughout.
target_link_options(ics_tracker INTERFACE LINKER:--disable-new-dtags)
ics_jsf_target(ics_tracker)

# The operational test of the tracking camera and the centroider.
add_executable(ics-tracker-monitor tool/main.cpp)
target_link_libraries(ics-tracker-monitor PRIVATE ics::tracker)
ics_jsf_target(ics-tracker-monitor)

ics_add_gtest(tracker_test
  SOURCES
    test/aravis_camera_test.cpp
    test/camera_test.cpp
    test/centroid_test.cpp
    test/config_test.cpp
    test/estimator_test.cpp
    test/geometry_test.cpp
    test/golden_test.cpp
    test/monitor_test.cpp
    test/tracker_test.cpp
  LIBRARIES ics::tracker)
ics_jsf_target(tracker_test)
target_include_directories(tracker_test PRIVATE test)
target_compile_definitions(tracker_test PRIVATE
  ICS_GOLDEN_TRACKER_DIR="${PROJECT_SOURCE_DIR}/../golden/tracker"
  ICS_TRACKER_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-tracker.toml")

add_test(NAME ics-tracker-monitor_usage COMMAND ics-tracker-monitor --help)
set_tests_properties(ics-tracker-monitor_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: ics-tracker-monitor")

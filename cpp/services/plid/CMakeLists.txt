# ics-plid (ICS-030): stores the vehicles' PLI from the MAVLink, CoT, SAPIENT
# and Lattice feeds in the ics::store segment log, archives it as Parquet and
# answers queries for it; see cpp/README.md.
add_library(ics_plid STATIC
  src/archiver.cpp
  src/config.cpp
  src/feeds.cpp
  src/import.cpp
  src/ingest.cpp
  src/lattice_feed.cpp
  src/query_server.cpp
  src/roles.cpp
  src/router.cpp
  src/run.cpp
  src/sapient_link.cpp
  src/service.cpp)
add_library(ics::plid ALIAS ics_plid)
target_include_directories(ics_plid PUBLIC include)
target_link_libraries(ics_plid PUBLIC ics::capture ics::config ics::cot ics::flightlog ics::frames ics::lattice
                                      ics::logging ics::mavlink ics::retime ics::sapient ics::store ics::timing)

add_executable(ics-plid src/main.cpp)
target_link_libraries(ics-plid PRIVATE ics::plid)
add_executable(ics-pli-import src/import_main.cpp)
target_link_libraries(ics-pli-import PRIVATE ics::plid)

# The executable itself, given an argument it does not take: it prints its
# usage. This also covers main.cpp.
add_test(NAME ics-plid_usage COMMAND ics-plid --help)
set_tests_properties(ics-plid_usage PROPERTIES PASS_REGULAR_EXPRESSION
                     "^usage: ics-plid\nThe config file is /etc/ics/ics-plid.toml.\n$")
add_test(NAME ics-pli-import_usage COMMAND ics-pli-import --help)
set_tests_properties(ics-pli-import_usage PROPERTIES PASS_REGULAR_EXPRESSION
                     "^usage: ics-pli-import\nRun it in the folder of one sortie's files. The config file is /etc/ics/ics-pli-import.toml.\n$")

ics_add_gtest(plid_test
  SOURCES
    test/archiver_test.cpp
    test/config_test.cpp
    test/import_test.cpp
    test/ingest_test.cpp
    test/lattice_feed_test.cpp
    test/query_server_test.cpp
    test/roles_test.cpp
    test/router_test.cpp
    test/run_test.cpp
    test/sapient_link_test.cpp
    test/service_test.cpp
  LIBRARIES ics::plid)
target_include_directories(plid_test PRIVATE test ${PROJECT_SOURCE_DIR}/timing/test ${PROJECT_SOURCE_DIR}/capture/test
                           ${PROJECT_SOURCE_DIR}/mavlink/test ${PROJECT_SOURCE_DIR}/sapient/test
                           ${PROJECT_SOURCE_DIR}/lattice/test)
target_compile_definitions(plid_test PRIVATE
  ICS_PLID_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-plid.toml"
  ICS_IMPORT_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-pli-import.toml"
  ICS_FLIGHTLOG_LOGS="${PROJECT_SOURCE_DIR}/flightlog/test/logs"
  ICS_EGM96_PATH="${ICS_GEOID_DIR}/egm96-5.pgm"
  ICS_MAVLINK_FIXTURES="${PROJECT_SOURCE_DIR}/mavlink/test/fixtures")

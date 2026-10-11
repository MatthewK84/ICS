# ics-camd (ICS-031): drives the station's cameras, and serves CameraService
# over gRPC, whose one command arms and offloads every camera; see
# cpp/README.md.
add_library(ics_camd STATIC
  src/command_client.cpp
  src/command_server.cpp
  src/config.cpp
  src/drivers.cpp
  src/offloader.cpp
  src/quality_watch.cpp
  src/run.cpp)
add_library(ics::camd ALIAS ics_camd)
target_include_directories(ics_camd PUBLIC include)
target_link_libraries(ics_camd PUBLIC ics::camera ics::config ics::logging ics::proto ics::proto_grpc ics::timing)

add_executable(ics-camd src/main.cpp)
target_link_libraries(ics-camd PRIVATE ics::camd)

# ics-cam-offload: the one command that arms and offloads every camera.
add_executable(ics-cam-offload src/offload_main.cpp)
target_link_libraries(ics-cam-offload PRIVATE ics::camd)

# The tests serve time quality reports with ics-timingd's own server.
ics_add_gtest(camd_test
  SOURCES
    test/command_client_test.cpp
    test/command_server_test.cpp
    test/config_test.cpp
    test/drivers_test.cpp
    test/offloader_test.cpp
    test/quality_watch_test.cpp
    test/run_test.cpp
  LIBRARIES ics::camd ics::timingd)
target_include_directories(camd_test PRIVATE ${PROJECT_SOURCE_DIR}/timing/test test)
target_compile_definitions(camd_test PRIVATE ICS_CAMD_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-camd.toml")

# The executables themselves, given arguments they do not take: each prints
# its usage. This also covers their main.cpp.
add_test(NAME ics-camd_usage COMMAND ics-camd --help)
set_tests_properties(ics-camd_usage PROPERTIES PASS_REGULAR_EXPRESSION
                     "^usage: ics-camd\nThe config file is /etc/ics/ics-camd.toml.\n$")
add_test(NAME ics-cam-offload_usage COMMAND ics-cam-offload)
set_tests_properties(ics-cam-offload_usage PROPERTIES PASS_REGULAR_EXPRESSION
                     "^usage: ics-cam-offload RUN_ID \\[SOCKET\\]\n$")

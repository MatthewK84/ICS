# The Phantom bench (ICS-027): records and offloads segments on the emulated
# camera, as the SDK-backed camera will on the bench. Test equipment; it
# ships nowhere.
add_executable(ics-phantom-bench main.cpp)
target_link_libraries(ics-phantom-bench PRIVATE ics::camera)

# Every preset's test run runs it, so CI records and verifies ten segments on
# the emulated camera in each sanitizer build.
add_test(NAME ics-phantom-bench COMMAND ics-phantom-bench WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

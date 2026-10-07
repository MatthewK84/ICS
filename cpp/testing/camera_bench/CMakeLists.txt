# The camera bench (ICS-027, ICS-028): records and offloads segments on an
# emulated Phantom or FLIR X6980-HS, as the SDK-backed cameras will on the
# bench. Test equipment; it ships nowhere.
add_executable(ics-camera-bench main.cpp)
target_link_libraries(ics-camera-bench PRIVATE ics::camera)

# Every preset's test run runs it for each camera, so CI records and verifies
# ten segments on each emulated camera in each sanitizer build. Each writes
# into a folder of its own.
foreach(camera phantom x6980)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${camera})
  add_test(NAME ics-camera-bench-${camera} COMMAND ics-camera-bench ${camera}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${camera})
endforeach()

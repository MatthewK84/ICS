# The strobe bench (ICS-029): records a strobe calibration on an emulated
# camera with a known offset, measures the offset, and publishes it through
# the camera offsets file ics-timingd reads. Test equipment; it ships
# nowhere.
add_executable(ics-strobe-bench main.cpp)
target_link_libraries(ics-strobe-bench PRIVATE ics::strobe)
target_include_directories(ics-strobe-bench PRIVATE ${PROJECT_SOURCE_DIR}/strobe/test)

# ICS-029's "Done when", offsets within 5 µs on the bench, for each emulated
# camera in each preset's test run. Each writes into a folder of its own.
foreach(camera phantom x6980)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${camera})
  add_test(NAME ics-strobe-bench-${camera} COMMAND ics-strobe-bench ${camera}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${camera})
endforeach()

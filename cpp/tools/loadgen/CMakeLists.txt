# ics-loadgen (ICS-020): test equipment that sends a fixed number of UDP
# datagrams at a fixed rate, for deploy/capture-bench. It ships nowhere.
add_executable(ics-loadgen main.cpp)
target_link_libraries(ics-loadgen PRIVATE ics::timing)

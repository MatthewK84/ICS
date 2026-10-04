# The Lattice probe (ICS-023): streams entities from a Lattice environment,
# such as a Lattice Sandbox, through the Lattice adapter and prints the
# records, so the adapter can be run against the vendor's sandbox. Test
# equipment; it ships nowhere.
add_executable(ics-lattice-probe main.cpp)
target_link_libraries(ics-lattice-probe PRIVATE ics::lattice)

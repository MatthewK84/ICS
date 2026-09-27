# The generated messages link and round-trip with the protobuf runtime (ICS-011).
ics_add_gtest(ics_proto_test SOURCES proto_roundtrip_test.cpp LIBRARIES ics::proto)

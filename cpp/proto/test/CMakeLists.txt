# Every ICS message reads and writes the golden files from the Python reference
# byte for byte (ICS-012); see python/ics_golden/proto.py.
ics_add_gtest(ics_proto_test SOURCES proto_golden_test.cpp LIBRARIES ics::proto)
target_compile_definitions(ics_proto_test PRIVATE ICS_PROTO_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/../golden/proto")

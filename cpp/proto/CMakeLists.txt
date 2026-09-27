# The C++ protobuf messages generated from proto/ (ICS-011), as the ics::proto
# library. gen/ is written by buf (proto/buf.gen.yaml) and committed; never edit
# it by hand. Like a Conan dependency, generated code is not ICS-authored:
# cpp/CMakeLists.txt adds this folder before the ICS warning flags, its headers
# are system headers to the code that includes them, and the policy scans skip
# gen/. The ICS code that uses the messages is checked as usual (proto/test).
find_package(protobuf CONFIG REQUIRED)

file(GLOB_RECURSE ICS_PROTO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen/*.pb.cc")
add_library(ics_proto STATIC ${ICS_PROTO_SOURCES})
add_library(ics::proto ALIAS ics_proto)
target_include_directories(ics_proto SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gen")
target_link_libraries(ics_proto PUBLIC protobuf::libprotobuf)

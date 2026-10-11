# The C++ protobuf messages generated from proto/ (ICS-011), as the ics::proto
# library, with the SAPIENT messages copied into proto/third_party (ICS-024).
# gen/ is written by buf (proto/buf.gen.yaml and proto/buf.gen.third_party.yaml)
# and committed; never edit it by hand. Like a Conan dependency, generated code is not ICS-authored:
# cpp/CMakeLists.txt adds this folder before the ICS warning flags, its headers
# are system headers to the code that includes them, and the policy scans skip
# gen/. The ICS code that uses the messages is checked as usual (proto/test).
find_package(protobuf CONFIG REQUIRED)

file(GLOB_RECURSE ICS_PROTO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen/*.pb.cc")
add_library(ics_proto STATIC ${ICS_PROTO_SOURCES})
add_library(ics::proto ALIAS ics_proto)
target_include_directories(ics_proto SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gen")
target_link_libraries(ics_proto PUBLIC protobuf::libprotobuf)

# The gRPC service code (#146, #150), as the ics::proto_grpc library. It is
# generated at build time by the protoc and grpc_cpp_plugin of the Conan
# packages the libraries come from (cpp/conanfile.py's tool requirements),
# so the code and the gRPC library cannot drift apart. It lands in this
# build folder's gen/, which, like the committed gen/, the policy scans skip.
find_package(gRPC CONFIG REQUIRED)
find_program(ICS_PROTOC protoc REQUIRED)
find_program(ICS_GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
execute_process(COMMAND "${ICS_PROTOC}" --version OUTPUT_VARIABLE ICS_PROTOC_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT ICS_PROTOC_VERSION STREQUAL "libprotoc 35.0")
  message(FATAL_ERROR "${ICS_PROTOC} is ${ICS_PROTOC_VERSION}; the gRPC code needs libprotoc 35.0, "
                      "the protoc proto/tools.txt pins")
endif()

set(ICS_PROTO_ROOT "${PROJECT_SOURCE_DIR}/../proto")
set(ICS_GRPC_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
# The files that define a service: ics-plid's queries (#146) and ics-timingd's
# reports (#150). Each is generated on its own.
set(ICS_GRPC_PROTOS ics/v1/pli_query ics/v1/time_quality_service)
set(ICS_GRPC_SOURCES "")
foreach(proto IN LISTS ICS_GRPC_PROTOS)
  add_custom_command(
    OUTPUT "${ICS_GRPC_GEN}/${proto}.grpc.pb.cc" "${ICS_GRPC_GEN}/${proto}.grpc.pb.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ICS_GRPC_GEN}"
    COMMAND "${ICS_PROTOC}" -I "${ICS_PROTO_ROOT}" "--grpc_out=${ICS_GRPC_GEN}"
            "--plugin=protoc-gen-grpc=${ICS_GRPC_CPP_PLUGIN}" "${proto}.proto"
    DEPENDS "${ICS_PROTO_ROOT}/${proto}.proto" "${ICS_PROTOC}" "${ICS_GRPC_CPP_PLUGIN}"
    COMMENT "Generating the gRPC service code for ${proto}.proto"
    VERBATIM)
  list(APPEND ICS_GRPC_SOURCES "${ICS_GRPC_GEN}/${proto}.grpc.pb.cc")
endforeach()
add_library(ics_proto_grpc STATIC ${ICS_GRPC_SOURCES})
add_library(ics::proto_grpc ALIAS ics_proto_grpc)
target_include_directories(ics_proto_grpc SYSTEM PUBLIC "${ICS_GRPC_GEN}")
target_link_libraries(ics_proto_grpc PUBLIC ics::proto gRPC::grpc++)

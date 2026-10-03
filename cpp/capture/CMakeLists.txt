# ics::capture (ICS-020): libpcap capture from an interface, written to
# rotating pcap files that are SHA-256 hashed as they are written. See
# services/capd for the service.
find_package(libpcap CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)

add_library(ics_capture STATIC
  src/capture.cpp
  src/output_file.cpp
  src/pcap_format.cpp
  src/rotating_writer.cpp
  src/sha256.cpp)
add_library(ics::capture ALIAS ics_capture)
target_include_directories(ics_capture PUBLIC include)
target_link_libraries(ics_capture PUBLIC ics::common ics::timing PRIVATE libpcap::libpcap OpenSSL::Crypto)

ics_add_gtest(capture_test
  SOURCES
    test/capture_test.cpp
    test/output_file_test.cpp
    test/pcap_format_test.cpp
    test/rotating_writer_test.cpp
    test/sha256_test.cpp
  LIBRARIES ics::capture libpcap::libpcap)
target_include_directories(capture_test PRIVATE ${PROJECT_SOURCE_DIR}/timing/test test)

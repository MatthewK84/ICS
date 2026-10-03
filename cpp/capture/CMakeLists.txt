# Packet capture on TAP ports (ICS-020): libpcap captures, and OpenSSL hashes
# each file; see cpp/README.md.
find_package(libpcap CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(ics_capture STATIC src/live_capture.cpp src/pcap_file.cpp src/rotating_writer.cpp src/sha256.cpp)
add_library(ics::capture ALIAS ics_capture)
target_include_directories(ics_capture PUBLIC include)
target_link_libraries(ics_capture PUBLIC ics::common ics::timing PRIVATE libpcap::libpcap OpenSSL::Crypto)

ics_add_gtest(capture_test
  SOURCES
    test/live_capture_test.cpp
    test/pcap_file_test.cpp
    test/rotating_writer_test.cpp
    test/sha256_test.cpp
  LIBRARIES ics::capture libpcap::libpcap OpenSSL::Crypto)
target_include_directories(capture_test PRIVATE ${PROJECT_SOURCE_DIR}/timing/test test)

# OpenSSL cannot allocate in this test, which must therefore run in its own
# process: CRYPTO_set_mem_functions works only before OpenSSL's first
# allocation.
ics_add_gtest(capture_openssl_fault_test SOURCES test/openssl_fault_test.cpp LIBRARIES ics::capture OpenSSL::Crypto)
target_include_directories(capture_openssl_fault_test PRIVATE ${PROJECT_SOURCE_DIR}/timing/test test)

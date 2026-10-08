# ics::store (ICS-030): the PLI store. ics-plid appends each ics.v1.PliRecord
# and PliEvent to a crash-safe segment log, and archives each closed segment
# as Parquet. See services/plid for the service.
add_library(ics_store STATIC
  src/archive.cpp
  src/crc32c.cpp
  src/files.cpp
  src/parquet_schema.cpp
  src/parquet_writer.cpp
  src/query.cpp
  src/segment_format.cpp
  src/segment_reader.cpp
  src/segment_writer.cpp
  src/thrift.cpp)
add_library(ics::store ALIAS ics_store)
target_include_directories(ics_store PUBLIC include)
target_link_libraries(ics_store PUBLIC ics::capture ics::common ics::proto ics::timing)

ics_add_gtest(store_test
  SOURCES
    test/archive_test.cpp
    test/crc32c_test.cpp
    test/parquet_schema_test.cpp
    test/parquet_writer_test.cpp
    test/query_test.cpp
    test/segment_format_test.cpp
    test/segment_reader_test.cpp
    test/segment_writer_test.cpp
    test/thrift_test.cpp
  LIBRARIES ics::store)
target_include_directories(store_test PRIVATE test ${PROJECT_SOURCE_DIR}/capture/test ${PROJECT_SOURCE_DIR}/timing/test)
# golden/pli: the segment and Parquet files python/tests/test_pli_parquet.py
# reads with pyarrow.
target_compile_definitions(store_test PRIVATE ICS_PLI_GOLDEN="${PROJECT_SOURCE_DIR}/../golden/pli")

# A segment's bytes come from a disk that a crash may have left torn, so the
# reader is fuzzed (ICS-008).
ics_add_fuzzer(store_fuzz SOURCES fuzz/store_fuzz.cpp LIBRARIES ics::store
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)

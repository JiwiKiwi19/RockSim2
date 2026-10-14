# GoogleTest: use an installed copy if present, otherwise fetch a pinned release.
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(rocksim_tests
  test_contacts.cpp
  test_integrators.cpp
  test_nbody.cpp
  test_simulation.cpp
)
target_link_libraries(rocksim_tests PRIVATE RockSim::engine rocksim_warnings GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(rocksim_tests DISCOVERY_TIMEOUT 60)

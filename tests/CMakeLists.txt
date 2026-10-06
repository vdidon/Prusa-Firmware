# do not build the firmware by default (tests are the focus if not crosscompiling)
project(cmake_test)

# include catch_discover_tests function from Catch2
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

# Make test executable
set(TEST_SOURCES
	Example_test.cpp
	PrusaStatistics_test.cpp
	Strtod_test.cpp
	Crc32_test.cpp
	Fastdiv_test.cpp
	Preheat_test.cpp
	host_stubs.c
	${CMAKE_SOURCE_DIR}/Firmware/strtod.c
    #Tests/Timer_test.cpp
    #Firmware/Timer.cpp
	)

add_executable(tests ${TEST_SOURCES})
# tests/ first: its avr/ headers stand in for avr-libc on the host
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/Firmware)
target_link_libraries(tests Catch2::Catch2WithMain)
catch_discover_tests(tests)

set(ctest_test_args --output-on-failure)

include(ProcessorCount)
ProcessorCount(N)
if(N EQUAL 0)
  message(
    WARNING "CTest: There was an issue reading the core count, tests won't be run in parallel"
    )
else()
  message(STATUS "CTest: Detected ${N} CPU threads")
  set(ctest_test_args -j${N} ${ctest_test_args})
endif()

# This step needs to always return OK but log whether it was successful or not. The thought here
# is that if the tests all pass, .ctest-finished is created and we can check for its existance
# after generating the report to determine if the overall build result is a pass or fail.
add_custom_target(
  test_run_all
  COMMAND ${CMAKE_CTEST_COMMAND} ${ctest_test_args}
  COMMAND ${CMAKE_COMMAND} -E touch .ctest-finished || exit 0
  BYPRODUCTS ${PROJECT_BINARY_DIR}/.ctest-finished
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  DEPENDS tests
  )

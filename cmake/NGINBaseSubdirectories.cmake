#-------------------------------------------------------------------------------
# Tests, examples, and benchmarks
#-------------------------------------------------------------------------------
if(NGIN_BASE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NGIN_BASE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NGIN_BASE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

cmake_minimum_required(VERSION 3.16)
project(NGINBenchmarks LANGUAGES CXX)

# Determine the top-level source/binary directories even when this subproject is
# included from an aggregate build.
set(_base_root_dir "${NGIN_BASE_ROOT_DIR}")
if(NOT _base_root_dir)
  get_filename_component(_base_root_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()
set(_base_binary_dir "${NGIN_BASE_BINARY_DIR}")
if(NOT _base_binary_dir)
  get_filename_component(_base_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/.." ABSOLUTE)
endif()

include("${_base_root_dir}/cmake/CPM.cmake")

# Cache third-party sources to speed up repeated configuration runs (CI, IDE refresh).
set(CPM_SOURCE_CACHE "${PROJECT_BINARY_DIR}/cpm-cache" CACHE PATH "CPM cache")

include_directories("${_base_root_dir}/include")

option(NGIN_BENCH_USE_SIMDJSON "Enable simdjson comparisons" ON)
option(NGIN_BENCH_USE_RAPIDJSON "Enable RapidJSON comparisons" ON)
option(NGIN_BENCH_USE_PUGIXML "Enable pugixml comparisons" ON)
option(NGIN_BENCH_USE_TINYXML2 "Enable tinyxml2 comparisons" ON)

function(ngin_add_benchmark target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE NGIN::Base)
  if(TARGET NGIN.Base.BuildOptions)
    target_link_libraries(${target} PRIVATE NGIN.Base.BuildOptions)
  endif()
  set_target_properties(${target} PROPERTIES FOLDER "Benchmarks")
endfunction()

ngin_add_benchmark(ExampleBenchmark ExampleBenchmark.cpp)
ngin_add_benchmark(SchedulerBenchmarks SchedulerBenchmarks.cpp)
ngin_add_benchmark(StringBenchmarks StringBenchmarks.cpp)
ngin_add_benchmark(VectorBenchmarks VectorBenchmarks.cpp)
ngin_add_benchmark(CallableBenchmarks CallableBenchmarks.cpp)
ngin_add_benchmark(FiberBenchmarks FiberBenchmarks.cpp)
ngin_add_benchmark(SIMDFastMathBench SIMDFastMathBench.cpp)
ngin_add_benchmark(JsonBenchmarks JsonBenchmarks.cpp)
ngin_add_benchmark(XmlBenchmarks XmlBenchmarks.cpp)

if(MSVC)
  target_compile_options(SIMDFastMathBench PRIVATE /arch:AVX2)
else()
  target_compile_options(SIMDFastMathBench PRIVATE -mavx2)
endif()

# ---------------------------------------------------------------------------
# Optional JSON dependencies
# ---------------------------------------------------------------------------
if(NGIN_BENCH_USE_SIMDJSON)
  set(_have_simdjson OFF)
  find_package(simdjson QUIET)
  if(simdjson_FOUND)
    set(_have_simdjson ON)
  else()
    CPMAddPackage(
      NAME simdjson
      GITHUB_REPOSITORY simdjson/simdjson
      VERSION 4.2.4
      OPTIONS
        "SIMDJSON_BUILD_STATIC_LIB ON"
        "SIMDJSON_BUILD_SHARED_LIB OFF"
        "SIMDJSON_DEVELOPMENT_CHECKS OFF"
    )
    if(simdjson_ADDED)
      set(_have_simdjson ON)
    endif()
  endif()

  if(_have_simdjson)
    if(TARGET simdjson::simdjson)
      target_link_libraries(JsonBenchmarks PRIVATE simdjson::simdjson)
    elseif(TARGET simdjson)
      target_link_libraries(JsonBenchmarks PRIVATE simdjson)
    endif()
    target_compile_definitions(JsonBenchmarks PRIVATE NGIN_HAVE_SIMDJSON=1)
  endif()
endif()

if(NGIN_BENCH_USE_RAPIDJSON)
  set(_policy_minimum_backup "${CMAKE_POLICY_VERSION_MINIMUM}")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  CPMAddPackage(
    NAME rapidjson
    GITHUB_REPOSITORY Tencent/rapidjson
    VERSION 1.1.0
    OPTIONS
      "RAPIDJSON_BUILD_DOC OFF"
      "RAPIDJSON_BUILD_EXAMPLES OFF"
      "RAPIDJSON_BUILD_TESTS OFF"
  )
  if(_policy_minimum_backup STREQUAL "")
    unset(CMAKE_POLICY_VERSION_MINIMUM)
  else()
    set(CMAKE_POLICY_VERSION_MINIMUM "${_policy_minimum_backup}")
  endif()
  if(rapidjson_ADDED)
    target_include_directories(JsonBenchmarks PRIVATE "${rapidjson_SOURCE_DIR}/include")
    target_compile_definitions(JsonBenchmarks PRIVATE NGIN_HAVE_RAPIDJSON=1)
  endif()
endif()

# ---------------------------------------------------------------------------
# Optional XML dependencies
# ---------------------------------------------------------------------------
if(NGIN_BENCH_USE_PUGIXML)
  set(_have_pugixml OFF)
  find_package(pugixml QUIET)
  if(pugixml_FOUND)
    set(_have_pugixml ON)
  else()
    CPMAddPackage(
      NAME pugixml
      GITHUB_REPOSITORY zeux/pugixml
      VERSION 1.14
      OPTIONS
        "PUGIXML_BUILD_SHARED_LIBS OFF"
        "PUGIXML_BUILD_TESTS OFF"
    )
    if(pugixml_ADDED)
      set(_have_pugixml ON)
    endif()
  endif()

  if(_have_pugixml)
    if(TARGET pugixml::pugixml)
      target_link_libraries(XmlBenchmarks PRIVATE pugixml::pugixml)
    elseif(TARGET pugixml)
      target_link_libraries(XmlBenchmarks PRIVATE pugixml)
    endif()
    target_compile_definitions(XmlBenchmarks PRIVATE NGIN_HAVE_PUGIXML=1)
  endif()
endif()

if(NGIN_BENCH_USE_TINYXML2)
  set(_have_tinyxml2 OFF)
  find_package(tinyxml2 QUIET)
  if(tinyxml2_FOUND)
    set(_have_tinyxml2 ON)
  else()
    CPMAddPackage(
      NAME tinyxml2
      GITHUB_REPOSITORY leethomason/tinyxml2
      VERSION 10.0.0
      OPTIONS
        "tinyxml2_BUILD_TESTING OFF"
        "tinyxml2_SHARED_LIBS OFF"
    )
    if(tinyxml2_ADDED)
      set(_have_tinyxml2 ON)
    endif()
  endif()

  if(_have_tinyxml2)
    if(TARGET tinyxml2::tinyxml2)
      target_link_libraries(XmlBenchmarks PRIVATE tinyxml2::tinyxml2)
    elseif(TARGET tinyxml2)
      target_link_libraries(XmlBenchmarks PRIVATE tinyxml2)
    endif()
    target_compile_definitions(XmlBenchmarks PRIVATE NGIN_HAVE_TINYXML2=1)
  endif()
endif()

# ---------------------------------------------------------------------------
# Optional concurrent map comparison benchmarks (TBB via system or CPM)
# ---------------------------------------------------------------------------
option(NGIN_TBB_STATIC "Prefer static TBB build via CPM" ON)

set(_have_tbb OFF)
find_package(TBB QUIET)
if(TBB_FOUND)
  set(_have_tbb ON)
else()
  if(NGIN_TBB_STATIC)
    set(_tbb_opts
      "TBB_TEST OFF"
      "TBB_STRICT OFF"
      "BUILD_SHARED_LIBS OFF"
      "TBB_INSTALL OFF"
    )
  else()
    set(_tbb_opts
      "TBB_TEST OFF"
      "TBB_STRICT OFF"
      "TBB_INSTALL OFF"
    )
  endif()

  CPMAddPackage(
    NAME TBB
    GITHUB_REPOSITORY oneapi-src/oneTBB
    GIT_TAG v2022.2.0
    OPTIONS ${_tbb_opts}
  )
  if(TBB_ADDED)
    set(_have_tbb ON)
  endif()
endif()

if(_have_tbb)
  message(STATUS "TBB available (system or CPM); enabling ConcurrentMapBench")
  ngin_add_benchmark(ConcurrentMapBench ConcurrentMapBench.cpp)
  if(TARGET TBB::tbb)
    target_link_libraries(ConcurrentMapBench PRIVATE TBB::tbb)
  elseif(TARGET tbb)
    target_link_libraries(ConcurrentMapBench PRIVATE tbb)
  endif()
  target_compile_definitions(ConcurrentMapBench PRIVATE NGIN_HAVE_TBB=1)

  # Force debug symbols for this target even in Release.
  if(MSVC)
    target_compile_options(ConcurrentMapBench PRIVATE /Zi)
    set_target_properties(ConcurrentMapBench PROPERTIES LINK_FLAGS "/DEBUG")
  else()
    target_compile_options(ConcurrentMapBench PRIVATE -g)
  endif()

  # If shared on Windows, copy runtime DLLs beside the benchmark exe to avoid "tbb12.dll not found".
  if(WIN32 AND NOT NGIN_TBB_STATIC AND TARGET TBB::tbb)
    add_custom_command(TARGET ConcurrentMapBench POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E echo "Copying TBB runtime DLLs for ConcurrentMapBench"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:TBB::tbb>
        $<TARGET_FILE_DIR:ConcurrentMapBench>
    )
  endif()
else()
  message(STATUS "TBB not available; ConcurrentMapBench disabled")
endif()

cmake_minimum_required(VERSION 3.16)
project(NGINBenchmarks LANGUAGES CXX)

# Add the main library include path
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add a benchmark executable
add_executable(ExampleBenchmark ExampleBenchmark.cpp)
target_link_libraries(ExampleBenchmark PRIVATE NGIN.Base)

# Optionally, add more benchmarks here

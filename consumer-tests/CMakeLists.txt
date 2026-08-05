cmake_minimum_required(VERSION 3.20)
project(NGINBaseConsumer LANGUAGES CXX)

find_package(NGINBase CONFIG REQUIRED)

add_executable(NGINBaseConsumer main.cpp)
target_compile_features(NGINBaseConsumer PRIVATE cxx_std_23)
target_link_libraries(NGINBaseConsumer PRIVATE NGIN::Base)

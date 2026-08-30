cmake_minimum_required(VERSION 3.24)
project(NGINBasePackageConsumer LANGUAGES CXX)

if(NOT NGIN_BASE_TEST_COMPONENT)
  message(FATAL_ERROR "NGIN_BASE_TEST_COMPONENT is required")
endif()

if(NGIN_BASE_TEST_COMPONENT STREQUAL "Complete")
  find_package(NGINBase CONFIG REQUIRED)
  set(ngin_base_consumer_target NGIN::Base)
else()
  find_package(NGINBase CONFIG REQUIRED COMPONENTS ${NGIN_BASE_TEST_COMPONENT})
  set(ngin_base_consumer_target "NGIN::Base::${NGIN_BASE_TEST_COMPONENT}")
endif()

if(NGIN_BASE_EXPECT_AGGREGATE)
  if(NOT TARGET NGIN::Base)
    message(FATAL_ERROR "The complete package did not export NGIN::Base")
  endif()
elseif(TARGET NGIN::Base)
  message(FATAL_ERROR "A component-subset package exported a partial NGIN::Base aggregate")
endif()

add_executable(NGINBasePackageConsumer main.cc)
target_compile_features(NGINBasePackageConsumer PRIVATE cxx_std_23)
target_compile_definitions(
  NGINBasePackageConsumer
  PRIVATE "NGIN_BASE_TEST_COMPONENT_${NGIN_BASE_TEST_COMPONENT}=1"
)
target_link_libraries(NGINBasePackageConsumer PRIVATE ${ngin_base_consumer_target})

enable_testing()
add_test(NAME NGINBasePackageConsumer.Run COMMAND NGINBasePackageConsumer)

cmake_minimum_required(VERSION 3.24)
project(NGINBaseConsumer LANGUAGES CXX)

include(CTest)

set(NGIN_BASE_CONSUMER_COMPONENTS
  "Foundation;Execution;IO;Serialization;Crypto;Net;NetTLS"
  CACHE STRING "NGIN.Base components exercised by the consumer matrix"
)

find_package(NGINBase CONFIG REQUIRED COMPONENTS ${NGIN_BASE_CONSUMER_COMPONENTS})

function(ngin_base_add_consumer_test target_name)
  add_test(NAME "${target_name}" COMMAND "${target_name}")
  if(WIN32)
    set_tests_properties("${target_name}" PROPERTIES
      ENVIRONMENT "PATH=$<TARGET_FILE_DIR:NGIN::Base::Foundation>$<SEMICOLON>$ENV{PATH}"
    )
  endif()
endfunction()

add_executable(NGINBaseConsumer main.cpp)
target_compile_features(NGINBaseConsumer PRIVATE cxx_std_23)
target_link_libraries(NGINBaseConsumer PRIVATE NGIN::Base)
ngin_base_add_consumer_test(NGINBaseConsumer)

foreach(component IN LISTS NGIN_BASE_CONSUMER_COMPONENTS)
  string(TOUPPER "${component}" component_upper)
  add_executable("NGINBase${component}Consumer" component_main.cpp)
  target_compile_features("NGINBase${component}Consumer" PRIVATE cxx_std_23)
  target_compile_definitions("NGINBase${component}Consumer" PRIVATE "NGIN_CONSUMER_${component_upper}")
  target_link_libraries("NGINBase${component}Consumer" PRIVATE "NGIN::Base::${component}")
  ngin_base_add_consumer_test("NGINBase${component}Consumer")
endforeach()

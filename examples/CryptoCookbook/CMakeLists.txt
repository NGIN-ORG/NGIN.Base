add_executable(CryptoCookbook main.cpp)

target_link_libraries(CryptoCookbook PRIVATE NGIN::Base)
if(TARGET NGIN.Base.BuildOptions)
  target_link_libraries(CryptoCookbook PRIVATE NGIN.Base.BuildOptions)
endif()

set_target_properties(CryptoCookbook PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

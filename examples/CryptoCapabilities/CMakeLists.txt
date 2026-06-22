add_executable(CryptoCapabilities main.cpp)

target_link_libraries(CryptoCapabilities PRIVATE NGIN::Base)
if(TARGET NGIN.Base.BuildOptions)
  target_link_libraries(CryptoCapabilities PRIVATE NGIN.Base.BuildOptions)
endif()

set_target_properties(CryptoCapabilities PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

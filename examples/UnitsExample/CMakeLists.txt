# Sandbox/CMakeLists.txt

# Create an executable target for Sandbox
add_executable(UnitsExample main.cpp)

# Link the NGIN.Base library
target_link_libraries(UnitsExample PRIVATE NGIN::Base)
if(TARGET NGIN.Base.BuildOptions)
  target_link_libraries(UnitsExample PRIVATE NGIN.Base.BuildOptions)
endif()

# Set the C++ standard (optional, if not inherited)
set_target_properties(UnitsExample PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

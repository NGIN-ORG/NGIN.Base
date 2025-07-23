# Sandbox/CMakeLists.txt

# Create an executable target for Sandbox
add_executable(Sandbox main.cpp)

# Link the NGIN.Base library
target_link_libraries(Sandbox PRIVATE NGIN::Base)

if(MSVC)
   # target_compile_options(Sandbox PRIVATE /W4 /WX)
else()
   # target_compile_options(Sandbox PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Set the C++ standard (optional, if not inherited)
set_target_properties(Sandbox PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

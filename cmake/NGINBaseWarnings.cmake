#-------------------------------------------------------------------------------
# Warnings, sanitizers, and compiler build options
#-------------------------------------------------------------------------------
function(ngin_enable_warnings tgt)
  if(NGIN_BASE_STRICT_WARNINGS)
    get_target_property(_ngin_target_type ${tgt} TYPE)
    set(_ngin_visibility PRIVATE)
    if(_ngin_target_type STREQUAL "INTERFACE_LIBRARY")
      set(_ngin_visibility INTERFACE)
    endif()
    if(MSVC)
      target_compile_options(${tgt} ${_ngin_visibility} /W4 /permissive- /w44265 /w44263)
    else()
      target_compile_options(${tgt} ${_ngin_visibility}
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wdouble-promotion
      )
    endif()
  endif()
endfunction()

add_library(NGIN.Base.BuildOptions INTERFACE)
add_library(NGIN::Base::BuildOptions ALIAS NGIN.Base.BuildOptions)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(NGIN_BASE_ENABLE_ASAN)
    target_compile_options(NGIN.Base.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=address,undefined>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Base.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=address,undefined>)
  endif()
  if(NGIN_BASE_ENABLE_TSAN)
    target_compile_options(NGIN.Base.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=thread>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Base.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=thread>)
  endif()
endif()

# Clang (Linux): allow selecting an explicit GCC toolchain for libstdc++/runtime.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NGIN_BASE_CLANG_GCC_TOOLCHAIN)
  if(NOT EXISTS "${NGIN_BASE_CLANG_GCC_TOOLCHAIN}")
    message(WARNING "NGIN_BASE_CLANG_GCC_TOOLCHAIN does not exist: ${NGIN_BASE_CLANG_GCC_TOOLCHAIN}")
  endif()
  target_compile_options(NGIN.Base.BuildOptions
    INTERFACE
      $<BUILD_INTERFACE:--gcc-toolchain=${NGIN_BASE_CLANG_GCC_TOOLCHAIN}>
  )
  target_link_options(NGIN.Base.BuildOptions
    INTERFACE
      $<BUILD_INTERFACE:--gcc-toolchain=${NGIN_BASE_CLANG_GCC_TOOLCHAIN}>
  )
endif()

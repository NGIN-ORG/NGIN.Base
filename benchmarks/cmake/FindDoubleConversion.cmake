## Custom lightweight FindDoubleConversion overriding Folly's module lookup.
## We already added the double-conversion project via CPM, which defines the
## build target 'double-conversion'. Expose the variables Folly expects.

if(TARGET double-conversion)
    get_target_property(_dc_includes double-conversion INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _dc_includes)
        # Fallback to source directory if property absent (older CMake behavior)
        set(_dc_includes "${double-conversion_SOURCE_DIR}")
    endif()
    list(GET _dc_includes 0 _dc_first)
    set(DOUBLE_CONVERSION_INCLUDE_DIR "${_dc_first}" CACHE PATH "double-conversion include dir")

    # Use generator expression for location; library built later.
    set(DOUBLE_CONVERSION_LIBRARY "$<TARGET_FILE:double-conversion>")

    if(NOT TARGET DoubleConversion::double-conversion)
        add_library(DoubleConversion::double-conversion ALIAS double-conversion)
    endif()

    set(DoubleConversion_FOUND TRUE)
    set(DOUBLE_CONVERSION_FOUND TRUE)
else()
    message(FATAL_ERROR "FindDoubleConversion.cmake invoked before double-conversion target exists")
endif()

cmake_minimum_required(VERSION 3.16)
project(NGINBenchmarks LANGUAGES CXX)

# Add the main library include path
include_directories(${CMAKE_SOURCE_DIR}/include)

# CPM for optional deps
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)


# Add a benchmark executable
add_executable(ExampleBenchmark ExampleBenchmark.cpp)
target_link_libraries(ExampleBenchmark PRIVATE NGIN::Base)

# Optionally, add more benchmarks here
add_executable(SchedulerBenchmarks SchedulerBenchmarks.cpp)
target_link_libraries(SchedulerBenchmarks PRIVATE NGIN::Base)

add_executable(StringBenchmarks StringBenchmarks.cpp)
target_link_libraries(StringBenchmarks PRIVATE NGIN::Base)

add_executable(VectorBenchmarks VectorBenchmarks.cpp)
target_link_libraries(VectorBenchmarks PRIVATE NGIN::Base)

add_executable(SIMDFastMathBench SIMDFastMathBench.cpp)
target_link_libraries(SIMDFastMathBench PRIVATE NGIN::Base)
target_compile_options(SIMDFastMathBench PRIVATE -mavx2)

# Optional: concurrent map comparison benchmarks (TBB via system or CPM)
option(NGIN_TBB_STATIC "Prefer static TBB build via CPM" ON)
set(_have_tbb OFF)
find_package(TBB QUIET)
if(TBB_FOUND)
	set(_have_tbb ON)
else()
	if(NGIN_TBB_STATIC)
	set(_tbb_opts "TBB_TEST OFF" "TBB_STRICT OFF" "BUILD_SHARED_LIBS OFF" "TBB_INSTALL OFF")
	else()
	set(_tbb_opts "TBB_TEST OFF" "TBB_STRICT OFF" "TBB_INSTALL OFF")
	endif()
	CPMAddPackage(
		NAME TBB
		GITHUB_REPOSITORY oneapi-src/oneTBB
	GIT_TAG v2022.2.0
		OPTIONS ${_tbb_opts}
	)
	if(TBB_ADDED)
		set(_have_tbb ON)
	endif()
endif()

if(_have_tbb)
	message(STATUS "TBB available (system or CPM); enabling ConcurrentMapBench")
	add_executable(ConcurrentMapBench ConcurrentMapBench.cpp)
	if(TARGET TBB::tbb)
		target_link_libraries(ConcurrentMapBench PRIVATE NGIN::Base TBB::tbb)
	else()
		target_link_libraries(ConcurrentMapBench PRIVATE NGIN::Base tbb)
	endif()
	target_compile_definitions(ConcurrentMapBench PRIVATE NGIN_HAVE_TBB=1)

	    # --- Force debug symbols for this target even in Release ---
    if(MSVC)
        target_compile_options(ConcurrentMapBench PRIVATE /Zi)
        set_target_properties(ConcurrentMapBench PROPERTIES LINK_FLAGS "/DEBUG")
    else()
        target_compile_options(ConcurrentMapBench PRIVATE -g)
    endif()
	# If shared on Windows, copy runtime DLLs beside the benchmark exe to avoid 'tbb12.dll not found'.
	if(WIN32 AND NOT NGIN_TBB_STATIC)
		add_custom_command(TARGET ConcurrentMapBench POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E echo "Copying TBB runtime DLLs for ConcurrentMapBench"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:ConcurrentMapBench>
		)
	endif()
else()
	message(STATUS "TBB not available; ConcurrentMapBench disabled")
endif()


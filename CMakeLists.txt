cmake_minimum_required(VERSION 3.25)

project(RideAlong VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG: use the published prebuilt library when it matches this compiler, otherwise build it once from source.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT ON CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)

add_commonlibsse_plugin(
	RideAlong
	NAME "RideAlong"
	AUTHOR "Clyde"
	VERSION ${PROJECT_VERSION}
	SOURCES src/main.cpp
)

target_compile_features(RideAlong PRIVATE cxx_std_23)
target_precompile_headers(RideAlong PRIVATE src/PCH.h)
if(MSVC)
	target_compile_options(RideAlong PRIVATE /utf-8 /permissive- /W3)
endif()

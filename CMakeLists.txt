# auto generated
set (APP_NAME error_capture)

cmake_minimum_required(VERSION 3.10)
# set the project name
project(error_capture)
# set the C++ standard to use
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# Enable coroutines
if(MSVC)
    add_compile_options(/await)
else()
    add_compile_options(-fcoroutines)
endif()
# set include directories
include_directories(/usr/include)
include_directories(/usr/local/include)
# check dependencies
find_package(Boost 1.74 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_path(TOMLPP_FOUND "toml++/toml.h")
if(NOT TOMLPP_FOUND)
    message(FATAL_ERROR "toml++/toml.h not found in default include paths!")
endif()

# set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# set executable
add_executable(${APP_NAME} main.cpp
    src/log_capture.cpp
    src/request_handler.cpp
    libs/simdjson/simdjson.cpp)

#add link librarie files
target_include_directories(${APP_NAME} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${APP_NAME}
	#libboost_coroutine.so 
	#libboost_context.so 
	#libboost_url.so
	#libz.so)
	)

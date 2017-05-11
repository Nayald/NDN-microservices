cmake_minimum_required(VERSION 3.5)
project(FIB)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=c++11")

set(SOURCE_FILES main.cpp
        node.h
        node.cpp
        fib.h
        fib.cpp
        tcp_server.h
        tcp_server.cpp)

find_package(Boost COMPONENTS system thread REQUIRED)

find_library(ndn-cxx REQUIRED)
find_library(pthread REQUIRED)

add_executable(FIB ${SOURCE_FILES})

target_link_libraries(FIB ndn-cxx ${Boost_LIBRARIES} pthread)

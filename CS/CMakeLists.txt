cmake_minimum_required(VERSION 3.5)
project(CS)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=c++11 -pg")

set(SOURCE_FILES main.cpp
        cs_cache.h
        lru_cache.h
        lru_cache.cpp
        entry.h
        entry_exception.h
        file_exception.h
        entry.cpp
#        udp_server.h
#        udp_server.cpp
        tcp_server.h
        tcp_server.cpp)

find_package(Boost COMPONENTS system filesystem chrono thread REQUIRED)

find_library(ndn-cxx REQUIRED)
find_library(pthread REQUIRED)

add_executable(CS ${SOURCE_FILES})

target_link_libraries(CS ndn-cxx ${Boost_LIBRARIES} pthread)
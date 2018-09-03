cmake_minimum_required(VERSION 3.5)
project(FW)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

file(GLOB LOGGER_SOURCES log/*.cpp)
file(GLOB NETWORK_SOURCES network/*.cpp)
file(GLOB TREE_SOURCES tree/*.cpp tree/*.h)
set(SOURCE_FILES main.cpp filter.cpp firewall.cpp filter_entry.cpp module.h)

find_package(Boost COMPONENTS system chrono thread REQUIRED)

find_library(ndn-cxx REQUIRED)
find_library(pthread REQUIRED)

add_executable(FW ${SOURCE_FILES} ${LOGGER_SOURCES} ${NETWORK_SOURCES} ${TREE_SOURCES})

target_link_libraries(FW ndn-cxx ${Boost_LIBRARIES} pthread)

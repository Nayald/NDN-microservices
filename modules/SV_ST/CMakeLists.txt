cmake_minimum_required(VERSION 3.5)
project(SV)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g")

file(GLOB NETWORK_SOURCES network/*.cpp)
set(SOURCE_FILES main.cpp signature_verifier.cpp log/logger.cpp)

find_package(Boost COMPONENTS system filesystem chrono thread REQUIRED)

find_library(ndn-cxx REQUIRED)
find_library(ssl REQUIRED)
find_library(crypto REQUIRED)
find_library(pthread REQUIRED)

add_executable(SV ${SOURCE_FILES} ${NETWORK_SOURCES} ${TREE_SOURCES})

target_link_libraries(SV ndn-cxx ${Boost_LIBRARIES} ssl crypto pthread)
cmake_minimum_required(VERSION 3.5)
project(SR)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Og -g")

file(GLOB LOGGER_SOURCES log/*.cpp)
file(GLOB NETWORK_SOURCES network/*.cpp)
set(SOURCE_FILES main.cpp strategy_router.cpp module.h strategy.h multicast_strategy.cpp multicast_strategy.h failover_strategy.cpp failover_strategy.h loadbalancing_strategy.cpp loadbalancing_strategy.h)

find_package(Boost COMPONENTS system chrono thread REQUIRED)

find_library(ndn-cxx REQUIRED)
find_library(pthread REQUIRED)

add_executable(SR ${SOURCE_FILES} ${LOGGER_SOURCES} ${NETWORK_SOURCES} ${TREE_SOURCES})

target_link_libraries(SR ndn-cxx ${Boost_LIBRARIES} pthread)

cmake_minimum_required(VERSION 3.5)
project(SR)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=native -pg")

file(GLOB LOGGER_SOURCES log/*.h log/*.cpp)
file(GLOB NETWORK_SOURCES network/*.h network/*.cpp)
file(GLOB RAPIDJSON_SOURCES rapidjson/*.h rapidjson/*.cpp)
set(SOURCE_FILES main.cpp strategy_router.cpp module.h strategy.h multicast_strategy.cpp multicast_strategy.h failover_strategy.cpp failover_strategy.h loadbalancing_strategy.cpp loadbalancing_strategy.h)

find_package(Boost COMPONENTS system chrono thread REQUIRED)

find_library(pthread REQUIRED)

add_executable(SR ${SOURCE_FILES} ${LOGGER_SOURCES} ${NETWORK_SOURCES} ${TREE_SOURCES} ${RAPIDJSON_SOURCES})

target_link_libraries(SR ${Boost_LIBRARIES} tbb pthread)

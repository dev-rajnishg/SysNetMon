cmake_minimum_required(VERSION 3.20)
project(SysNetMon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(sysnetmon-server cpp/server/main.cpp)
target_include_directories(sysnetmon-server PRIVATE cpp/include)

add_executable(sysnetmon-agent cpp/client/main.cpp)
target_include_directories(sysnetmon-agent PRIVATE cpp/include)

if(WIN32)
    target_link_libraries(sysnetmon-server PRIVATE ws2_32)
    target_link_libraries(sysnetmon-agent PRIVATE ws2_32 iphlpapi)
endif()
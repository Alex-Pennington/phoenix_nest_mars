cmake_minimum_required(VERSION 3.16)
project(appliance_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(pna_core STATIC
  src/frame.cpp
  src/store.cpp
  src/net.cpp
  src/json.cpp
  src/loopback_modem.cpp
  src/core.cpp
  src/api_server.cpp
  src/sha256.cpp
  src/run_program.cpp
  src/greybeard_modem.cpp
  src/acp127.cpp
  src/acp126.cpp
  src/acp201.cpp
  src/compose.cpp
  src/received.cpp
  src/link_monitor.cpp)
target_link_libraries(pna_core PUBLIC SQLite::SQLite3 Threads::Threads)

add_executable(appliance-core src/main.cpp)
target_link_libraries(appliance-core PRIVATE pna_core)

add_executable(appliance-cli tools/appliance_cli.cpp)
target_link_libraries(appliance-cli PRIVATE pna_core)

enable_testing()
foreach(t test_frame test_core test_greybeard test_acp127 test_received test_templates test_compose test_link)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE pna_core)
  target_compile_definitions(${t} PRIVATE PNA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME ${t} COMMAND ${t})
endforeach()

cmake_minimum_required(VERSION 3.16)
project(appliance_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
add_compile_options(-Wall -Wextra)
set(CMAKE_INSTALL_DOCDIR share/doc/phoenix-nest-mars)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

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
  src/link_monitor.cpp
  src/ops.cpp
  src/api_ops.cpp
  src/config.cpp
  src/crypto.cpp
  src/digest.cpp
  src/keys.cpp
  src/codress.cpp
  src/station.cpp)
target_link_libraries(pna_core PUBLIC SQLite::SQLite3 Threads::Threads OpenSSL::Crypto)

add_executable(appliance-core src/main.cpp)
target_link_libraries(appliance-core PRIVATE pna_core)

add_executable(appliance-cli tools/appliance_cli.cpp)
target_link_libraries(appliance-cli PRIVATE pna_core)

add_executable(pna-keymaster tools/keymaster.cpp)
target_link_libraries(pna-keymaster PRIVATE pna_core)

enable_testing()
foreach(t test_frame test_core test_greybeard test_acp127 test_received test_templates test_compose test_link test_ops test_sched test_config test_crypto test_codress test_station test_badday)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE pna_core)
  target_compile_definitions(${t} PRIVATE PNA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Installing: the programs, the example settings, the systemd user service
# and the operator docs. packaging/install.sh does this for one user.
install(TARGETS appliance-core appliance-cli pna-keymaster RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(BINDIR ${CMAKE_INSTALL_FULL_BINDIR})
set(DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
configure_file(packaging/appliance-core.service.in ${CMAKE_CURRENT_BINARY_DIR}/appliance-core.service @ONLY)
install(FILES packaging/appliance.conf.example ${CMAKE_CURRENT_BINARY_DIR}/appliance-core.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/appliance-core)
install(FILES README.md docs/api.md docs/quick-start.md docs/crypto.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

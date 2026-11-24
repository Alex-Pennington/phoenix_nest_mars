cmake_minimum_required(VERSION 3.16)
project(appliance_core C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
add_compile_options(-Wall -Wextra)
# The cipher slot's modules are shared libraries built from the same code.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_DOCDIR share/doc/phoenix-nest-mars)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)

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
  src/filemsg.cpp
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
  src/station.cpp
  src/bundle.cpp
    src/first_start.cpp
  src/table.cpp
  src/channels.cpp
  src/mars_import.cpp
  src/s5066.cpp
  src/s5066_client.cpp
  src/appa.cpp
  src/appa_modem.cpp
  src/cipher_slot.cpp
  src/cipher_check.cpp
  modules/pna_aes.cpp)
target_link_libraries(pna_core PUBLIC SQLite::SQLite3 Threads::Threads OpenSSL::Crypto ZLIB::ZLIB EXPAT::EXPAT ${CMAKE_DL_LIBS})

# Our own cipher as a loadable module (docs/cipher-slot.md), to prove the
# slot and the harness on a real shared library.
add_library(pna-aes MODULE modules/pna_aes_entry.cpp)
set_target_properties(pna-aes PROPERTIES PREFIX "" OUTPUT_NAME "pna-aes")
target_link_libraries(pna-aes PRIVATE pna_core)

add_executable(pna-cipher-check tools/cipher_check.cpp)
target_link_libraries(pna-cipher-check PRIVATE pna_core)

add_executable(appliance-core src/main.cpp)
target_link_libraries(appliance-core PRIVATE pna_core)

add_executable(appliance-cli tools/appliance_cli.cpp)
target_link_libraries(appliance-cli PRIVATE pna_core)

add_executable(pna-keymaster tools/keymaster.cpp)
target_link_libraries(pna-keymaster PRIVATE pna_core)

enable_testing()
# Cipher modules made only for the tests (tests/modules/): a toy text-form
# module and a broken one the harness must fail. Neither is a real cipher.
add_library(test_text_module MODULE tests/modules/text_module.c)
set_target_properties(test_text_module PROPERTIES PREFIX "")
target_link_libraries(test_text_module PRIVATE OpenSSL::Crypto)
add_library(test_broken_module MODULE tests/modules/broken_module.c)
set_target_properties(test_broken_module PROPERTIES PREFIX "")
add_library(test_template_module MODULE sdk/cipher_template.c)
set_target_properties(test_template_module PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
target_include_directories(test_template_module PRIVATE sdk)

foreach(t test_frame test_core test_greybeard test_acp127 test_received test_templates test_compose test_link test_ops test_sched test_config test_crypto test_codress test_station test_badday test_import test_s5066 test_appa test_appa_m110 test_cipher_slot test_cp_link test_acp127_ops test_groundzero test_firststart)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE pna_core)
  target_compile_definitions(${t} PRIVATE PNA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" PNA_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}")
  add_test(NAME ${t} COMMAND ${t})
endforeach()
# test_firststart runs the real service and CLI.
add_dependencies(test_firststart appliance-core appliance-cli)

# Installing: the programs, the example settings, the systemd user service
# and the operator docs. packaging/install.sh does this for one user.
install(TARGETS appliance-core appliance-cli pna-keymaster pna-cipher-check RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(BINDIR ${CMAKE_INSTALL_FULL_BINDIR})
set(DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
configure_file(packaging/appliance-core.service.in ${CMAKE_CURRENT_BINARY_DIR}/appliance-core.service @ONLY)
install(FILES packaging/appliance.conf.example ${CMAKE_CURRENT_BINARY_DIR}/appliance-core.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/appliance-core)
install(FILES README.md docs/api.md docs/quick-start.md docs/crypto.md docs/formats.md docs/stanag-5066-client.md docs/appendix-a-driver.md docs/cipher-slot.md docs/cipher-adapter-guide.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

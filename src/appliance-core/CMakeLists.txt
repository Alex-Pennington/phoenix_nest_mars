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

# The forms built into the program (forms/*.form, docs/forms.md). A member's
# own changes live beside the station's database and need no rebuild; these
# are the ones a box comes with.
file(GLOB PNA_FORM_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/forms/*.form)
list(SORT PNA_FORM_FILES)
set(PNA_FORMS_CPP ${CMAKE_CURRENT_BINARY_DIR}/builtin_forms.cpp)
set(_pna_forms_body "")
foreach(_f ${PNA_FORM_FILES})
  file(READ ${_f} _t)
  get_filename_component(_n ${_f} NAME_WE)
  string(APPEND _pna_forms_body "        {\"${_n}\", R\"PNAFORM(${_t})PNAFORM\"},\n")
endforeach()
file(WRITE ${PNA_FORMS_CPP}.new "// Made by CMake from forms/*.form. Don't edit.\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/forms.h\"\nnamespace pna {\nnamespace forms {\nconst std::vector<std::pair<std::string, std::string>>& builtin_texts() {\n    static const std::vector<std::pair<std::string, std::string>> t = {\n${_pna_forms_body}    };\n    return t;\n}\n}  // namespace forms\n}  // namespace pna\n")
configure_file(${PNA_FORMS_CPP}.new ${PNA_FORMS_CPP} COPYONLY)

# The guide's pages (guide/*.page, src/guide.h), built in the same way.
file(GLOB PNA_PAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/guide/*.page)
list(SORT PNA_PAGE_FILES)
set(PNA_PAGES_CPP ${CMAKE_CURRENT_BINARY_DIR}/builtin_pages.cpp)
set(_pna_pages_body "")
foreach(_f ${PNA_PAGE_FILES})
  file(READ ${_f} _t)
  get_filename_component(_n ${_f} NAME_WE)
  string(APPEND _pna_pages_body "        {\"${_n}\", R\"PNAPAGE(${_t})PNAPAGE\"},\n")
endforeach()
file(WRITE ${PNA_PAGES_CPP}.new "// Made by CMake from guide/*.page. Don't edit.\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/guide.h\"\nnamespace pna {\nnamespace guide {\nconst std::vector<std::pair<std::string, std::string>>& builtin_texts() {\n    static const std::vector<std::pair<std::string, std::string>> t = {\n${_pna_pages_body}    };\n    return t;\n}\n}  // namespace guide\n}  // namespace pna\n")
configure_file(${PNA_PAGES_CPP}.new ${PNA_PAGES_CPP} COPYONLY)

# Training's sample traffic (src/training.h): the scrubbed MARS samples in
# tests/fixtures, kept byte for byte (they hold the stray bytes of noisy
# receptions), so built in as hex.
file(GLOB PNA_SAMPLE_FILES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mars-samples/*.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mars-samples-b2/*.txt)
list(SORT PNA_SAMPLE_FILES)
set(PNA_SAMPLES_CPP ${CMAKE_CURRENT_BINARY_DIR}/builtin_samples.cpp)
set(_pna_samples_body "")
foreach(_f ${PNA_SAMPLE_FILES})
  file(READ ${_f} _h HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" _h "${_h}")
  get_filename_component(_d ${_f} DIRECTORY)
  get_filename_component(_d ${_d} NAME)
  get_filename_component(_n ${_f} NAME_WE)
  string(APPEND _pna_samples_body "        {\"${_d}/${_n}\", std::string(\"${_h}\", sizeof(\"${_h}\") - 1)},\n")
endforeach()
file(WRITE ${PNA_SAMPLES_CPP}.new "// Made by CMake from tests/fixtures/mars-samples*/*.txt. Don't edit.\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/training.h\"\nnamespace pna {\nconst std::vector<std::pair<std::string, std::string>>& training_samples() {\n    static const std::vector<std::pair<std::string, std::string>> t = {\n${_pna_samples_body}    };\n    return t;\n}\n}  // namespace pna\n")
configure_file(${PNA_SAMPLES_CPP}.new ${PNA_SAMPLES_CPP} COPYONLY)

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
  src/parts.cpp
    src/first_start.cpp
    src/forms.cpp
    src/api_forms.cpp
    ${PNA_FORMS_CPP}
    src/guide.cpp
    src/api_guide.cpp
    ${PNA_PAGES_CPP}
    src/training.cpp
    ${PNA_SAMPLES_CPP}
  src/table.cpp
  src/channels.cpp
  src/mars_import.cpp
  src/s5066.cpp
  src/s5066_client.cpp
  src/appa.cpp
  src/appa_modem.cpp
  src/cipher_slot.cpp
  src/cipher_check.cpp
  src/bbs.cpp
  src/api_bbs.cpp
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

foreach(t test_frame test_core test_greybeard test_acp127 test_received test_templates test_compose test_link test_ops test_sched test_config test_crypto test_codress test_station test_badday test_import test_s5066 test_appa test_appa_m110 test_cipher_slot test_cp_link test_acp127_ops test_groundzero test_firststart test_forms test_parts test_guide test_bbs)
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

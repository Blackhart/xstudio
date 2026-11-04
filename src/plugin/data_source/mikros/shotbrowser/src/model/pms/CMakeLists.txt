if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.26)
    project(pms LANGUAGES CXX)
endif()

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
find_package(yaml-cpp QUIET CONFIG)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

find_package(nlohmann_json QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(httplib QUIET CONFIG)
if(NOT httplib_FOUND)
    set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.16.3
    )
    FetchContent_MakeAvailable(httplib)
endif()

find_package(OpenSSL REQUIRED)

set(PMS_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PMS_GEN_ENTITIES "${PMS_GEN_DIR}/pms/gen/entities.hpp")
set(PMS_GEN_FACTORIES "${PMS_GEN_DIR}/pms/gen/query_factories.hpp")

add_custom_command(
    OUTPUT "${PMS_GEN_ENTITIES}" "${PMS_GEN_FACTORIES}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PMS_GEN_DIR}/pms/gen"
    COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_entities.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema/schema.yaml"
        "${PMS_GEN_DIR}/pms/gen"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_entities.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema/schema.yaml"
    VERBATIM
)
add_custom_target(pms_codegen DEPENDS "${PMS_GEN_ENTITIES}" "${PMS_GEN_FACTORIES}")

set(PMS_LIB_SOURCES
    schema.cpp
    validate.cpp
    session.cpp
    dump.cpp
    caching_backend.cpp
    shotgrid/mapping.cpp
    shotgrid/translate.cpp
    shotgrid/backend.cpp
)
# Local-only FakeBackend (gitignored). Copy from model/pms/dev/ when needed.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fake/backend.cpp")
    list(APPEND PMS_LIB_SOURCES fake/backend.cpp)
endif()

add_library(pms STATIC ${PMS_LIB_SOURCES})
add_library(pms::pms ALIAS pms)
add_dependencies(pms pms_codegen)

# Parent of this dir is model/, so #include "pms/…" resolves here.
target_include_directories(pms
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${PMS_GEN_DIR}"
)
target_compile_features(pms PUBLIC cxx_std_17)
target_compile_definitions(pms PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(pms PUBLIC
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)
set_target_properties(pms PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
    )
    FetchContent_MakeAvailable(doctest)

    set(PMS_TEST_SOURCES
        tests/test_main.cpp
        tests/test_schema.cpp
        tests/test_query.cpp
        tests/test_validate.cpp
        tests/test_session.cpp
        tests/test_caching_backend.cpp
        tests/shotgrid/test_mapping.cpp
        tests/shotgrid/test_translate.cpp
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fake/test_fake_backend.cpp"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fake/backend.cpp")
        list(APPEND PMS_TEST_SOURCES tests/fake/test_fake_backend.cpp)
    endif()

    add_executable(pms_tests ${PMS_TEST_SOURCES})
    target_link_libraries(pms_tests PRIVATE pms doctest::doctest)
    target_compile_definitions(pms_tests PRIVATE
        PMS_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/schema/schema.yaml"
        PMS_SHOTGRID_MAPPING_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shotgrid/mapping.yaml"
        PMS_SHOTGRID_MAPPING_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/shotgrid/fixtures/mapping_filled.yaml"
    )
    add_test(NAME pms_tests COMMAND pms_tests)
    set_tests_properties(pms_tests PROPERTIES TIMEOUT 30)
endif()

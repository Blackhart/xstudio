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

find_package(OpenSSL REQUIRED)

set(PMS_SRC_DIR "${PMS_ROOT}/src")
set(PMS_CONFIG_DIR "${PMS_ROOT}/config")
set(PMS_TEST_DIR "${PMS_ROOT}/tests")

set(PMS_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PMS_GEN_ENTITIES "${PMS_GEN_DIR}/pms/gen/entities.hpp")
set(PMS_GEN_FACTORIES "${PMS_GEN_DIR}/pms/gen/query_factories.hpp")

add_custom_command(
    OUTPUT "${PMS_GEN_ENTITIES}" "${PMS_GEN_FACTORIES}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PMS_GEN_DIR}/pms/gen"
    COMMAND Python3::Interpreter
        "${PMS_SRC_DIR}/tools/gen_entities.py"
        "${PMS_CONFIG_DIR}/schema.yaml"
        "${PMS_GEN_DIR}/pms/gen"
    DEPENDS
        "${PMS_SRC_DIR}/tools/gen_entities.py"
        "${PMS_CONFIG_DIR}/schema.yaml"
    VERBATIM
)
add_custom_target(pms_codegen DEPENDS "${PMS_GEN_ENTITIES}" "${PMS_GEN_FACTORIES}")

set(PMS_LIB_SOURCES
    "${PMS_SRC_DIR}/pms/schema.cpp"
    "${PMS_SRC_DIR}/pms/validate.cpp"
    "${PMS_SRC_DIR}/pms/session.cpp"
    "${PMS_SRC_DIR}/pms/dump.cpp"
    "${PMS_SRC_DIR}/pms/caching_backend.cpp"
    "${PMS_SRC_DIR}/pms/env.cpp"
    "${PMS_SRC_DIR}/pms/shared_session.cpp"
    "${PMS_SRC_DIR}/pms/shotgrid/mapping.cpp"
    "${PMS_SRC_DIR}/pms/shotgrid/translate.cpp"
    "${PMS_SRC_DIR}/pms/shotgrid/backend.cpp"
)
# Local-only FakeBackend (gitignored). Copy from pms/dev/ when needed.
set(PMS_HAS_FAKE OFF)
if(EXISTS "${PMS_SRC_DIR}/pms/fake/backend.cpp")
    list(APPEND PMS_LIB_SOURCES "${PMS_SRC_DIR}/pms/fake/backend.cpp")
    set(PMS_HAS_FAKE ON)
endif()

add_library(pms SHARED ${PMS_LIB_SOURCES})
add_library(pms::pms ALIAS pms)
add_dependencies(pms pms_codegen)

# ${PMS_SRC_DIR} so #include "pms/…" resolves to src/pms/.
target_include_directories(pms
    PUBLIC
        "${PMS_SRC_DIR}"
        "${PMS_GEN_DIR}"
        "${PMS_ROOT}/../../../../../extern/include"
)
target_compile_features(pms PUBLIC cxx_std_17)
target_compile_definitions(pms PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(pms PUBLIC
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)
if(PMS_HAS_FAKE)
    find_package(OpenImageIO REQUIRED)
    target_link_libraries(pms PRIVATE OpenImageIO::OpenImageIO) #< Fake thumbs use ImageBufAlgo.
    target_compile_definitions(pms PUBLIC PMS_HAS_FAKE=1)
endif()

set_target_properties(pms PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

if(NOT PMS_STANDALONE)
    # Same folder as Mikros plugins so $ORIGIN resolves libpms.so.
    set_target_properties(pms PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugin"
    )
    install(TARGETS pms LIBRARY DESTINATION share/xstudio/plugin)
    if(COMMAND add_resource)
        add_resource(
            schema.yaml
            ${PMS_CONFIG_DIR}
            pms
            mikros_pms)
    endif()
endif()

if(BUILD_TESTING)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
    )
    FetchContent_MakeAvailable(doctest)

    set(PMS_TEST_SOURCES
        "${PMS_TEST_DIR}/test_main.cpp"
        "${PMS_TEST_DIR}/test_schema.cpp"
        "${PMS_TEST_DIR}/test_query.cpp"
        "${PMS_TEST_DIR}/test_validate.cpp"
        "${PMS_TEST_DIR}/test_session.cpp"
        "${PMS_TEST_DIR}/test_caching_backend.cpp"
        "${PMS_TEST_DIR}/shotgrid/test_mapping.cpp"
        "${PMS_TEST_DIR}/shotgrid/test_translate.cpp"
    )
    if(EXISTS "${PMS_TEST_DIR}/fake/test_fake_backend.cpp"
       AND EXISTS "${PMS_SRC_DIR}/pms/fake/backend.cpp")
        list(APPEND PMS_TEST_SOURCES "${PMS_TEST_DIR}/fake/test_fake_backend.cpp")
    endif()

    add_executable(pms_tests ${PMS_TEST_SOURCES})
    target_link_libraries(pms_tests PRIVATE pms doctest::doctest)
    target_include_directories(pms_tests PRIVATE "${PMS_TEST_DIR}")
    target_compile_definitions(pms_tests PRIVATE
        PMS_SCHEMA_PATH="${PMS_CONFIG_DIR}/schema.yaml"
        PMS_SHOTGRID_MAPPING_FIXTURE_PATH="${PMS_TEST_DIR}/shotgrid/fixtures/mapping_filled.yaml"
        PMS_SHOTGRID_MAPPING_EXAMPLE_PATH="${PMS_CONFIG_DIR}/shotgrid/mapping.yaml.example"
    )
    add_test(NAME pms_tests COMMAND pms_tests)
    set_tests_properties(pms_tests PROPERTIES TIMEOUT 30)
endif()

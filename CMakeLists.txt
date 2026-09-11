cmake_minimum_required(VERSION 3.20)
project(cpp_http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Everything except main() lives in a library so the tests can link the same
# objects the server runs.
add_library(httpserver_core
    src/config/Config.cpp
    src/http/HttpParser.cpp
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/http/StaticFiles.cpp
    src/http/Url.cpp
    src/json/Json.cpp
    src/logging/Logger.cpp
    src/net/Connection.cpp
    src/net/Socket.cpp
    src/api/SystemController.cpp
    src/api/UserController.cpp
    src/api/UserStore.cpp
    src/routing/Router.cpp
    src/server/Server.cpp
    src/threading/ThreadPool.cpp
)
target_include_directories(httpserver_core PUBLIC src)

find_package(Threads REQUIRED)
target_link_libraries(httpserver_core PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(httpserver_core PUBLIC ws2_32)
    # Winsock headers pull in the old winsock.h through windows.h otherwise.
    target_compile_definitions(httpserver_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
    target_compile_options(httpserver_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(httpserver_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(httpserver src/main.cpp)
target_link_libraries(httpserver PRIVATE httpserver_core)

add_executable(benchmark_client tools/BenchmarkClient.cpp)
target_link_libraries(benchmark_client PRIVATE httpserver_core)

# Copy the sample site and config next to the binary so the server can be run
# straight from the build directory.
add_custom_command(TARGET httpserver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/public $<TARGET_FILE_DIR:httpserver>/public
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/server.conf $<TARGET_FILE_DIR:httpserver>/server.conf
)

option(BUILD_TESTING "Build the test executables" ON)
if(BUILD_TESTING)
    enable_testing()

    add_executable(unit_tests
        tests/TestFramework.cpp
        tests/test_api.cpp
        tests/test_config.cpp
        tests/test_http_parser.cpp
        tests/test_http_response.cpp
        tests/test_json.cpp
        tests/test_router.cpp
        tests/test_static_files.cpp
        tests/test_thread_pool.cpp
    )
    target_link_libraries(unit_tests PRIVATE httpserver_core)
    target_include_directories(unit_tests PRIVATE tests)

    add_executable(integration_tests
        tests/TestFramework.cpp
        tests/test_integration.cpp
    )
    target_link_libraries(integration_tests PRIVATE httpserver_core)
    target_include_directories(integration_tests PRIVATE tests)

    add_test(NAME unit COMMAND unit_tests)
    add_test(NAME integration COMMAND integration_tests)
endif()

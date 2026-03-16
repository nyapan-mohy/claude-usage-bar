cmake_minimum_required(VERSION 3.20)
project(claude-usage-bar VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific defines
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# ---------- Dependencies ----------

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    SYSTEM
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    SYSTEM
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json googletest)

# ---------- Core library (platform-independent logic) ----------

set(CORE_SOURCES
    src/usage_model.cpp
    src/oauth.cpp
    src/http_client.cpp
    src/usage_service.cpp
    src/history.cpp
    src/notification.cpp
    src/settings.cpp
)

set(CORE_HEADERS
    src/config.h
    src/string_utils.h
    src/win32_raii.h
    src/usage_model.h
    src/oauth.h
    src/http_client.h
    src/usage_service.h
    src/history.h
    src/notification.h
    src/settings.h
)

add_library(core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(core PUBLIC src)
target_link_libraries(core
    PUBLIC
        nlohmann_json::nlohmann_json
        winhttp
        bcrypt
        crypt32
        shlwapi
        shell32
        rpcrt4
)

# ---------- Main executable ----------

set(APP_SOURCES
    src/main.cpp
    src/tray.cpp
    src/popup.cpp
)

set(APP_HEADERS
    src/tray.h
    src/popup.h
)

add_executable(claude-usage-bar WIN32 ${APP_SOURCES} ${APP_HEADERS})
target_link_libraries(claude-usage-bar
    PRIVATE
        core
        gdiplus
        comctl32
)

# ---------- Tests ----------

enable_testing()

set(TEST_SOURCES
    tests/test_usage_model.cpp
    tests/test_oauth.cpp
    tests/test_history.cpp
    tests/test_usage_service.cpp
    tests/test_settings.cpp
)

add_executable(claude-usage-bar-tests ${TEST_SOURCES})
target_link_libraries(claude-usage-bar-tests
    PRIVATE
        core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(claude-usage-bar-tests)

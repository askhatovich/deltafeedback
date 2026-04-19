cmake_minimum_required(VERSION 3.16)
project(deltafeedback LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DELTAFEEDBACK_BUILD_TESTS "Build gtest suite" ON)

# --- System packages ---
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# --- CrowCpp via FetchContent (pinned 1.2.0) ---
include(FetchContent)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(Crow)

# --- libdeltachat (built externally in ../deltachatbot-cpp) ---
set(DC_CORE  "${CMAKE_SOURCE_DIR}/../deltachatbot-cpp/deltachat-core-rust" CACHE PATH "deltachat-core-rust path")
set(DC_LIB   "${DC_CORE}/target/release")
set(DC_INC   "${DC_CORE}/deltachat-ffi")

# --- Library with all app code (no main) — linked by binary AND tests ---
add_library(deltafeedback_core STATIC
    src/app/config.cpp
    src/pow/sha256.cpp
    src/pow/challenge.cpp
    src/feedback/validator.cpp
    src/feedback/service.cpp
    src/db/statement.cpp
    src/db/database.cpp
    src/db/state.cpp
    src/db/tickets.cpp
    src/db/messages.cpp
    src/db/replay.cpp
    src/dc/admin_parse.cpp
    src/dc/admin_route.cpp
)
target_include_directories(deltafeedback_core PUBLIC src)
target_link_libraries(deltafeedback_core
    PUBLIC
        SQLite::SQLite3
        OpenSSL::Crypto
        Threads::Threads
)

# --- Main binary (DC FFI and Bot live here, out of core, so tests don't need libdeltachat) ---
add_executable(deltafeedback
    src/main.cpp
    src/dc/sender.cpp
    src/dc/bot.cpp
    src/server/server.cpp
    src/server/ratelimit.cpp
)
target_link_libraries(deltafeedback PRIVATE deltafeedback_core Crow::Crow)
target_include_directories(deltafeedback PRIVATE ${DC_INC})
target_link_directories(deltafeedback PRIVATE ${DC_LIB})
target_link_libraries(deltafeedback PRIVATE deltachat)
set_target_properties(deltafeedback PROPERTIES
    BUILD_RPATH   "${DC_LIB}"
    INSTALL_RPATH "${DC_LIB}"
)

# --- Tests ---
if(DELTAFEEDBACK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

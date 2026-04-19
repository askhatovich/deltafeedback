include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(deltafeedback_tests
    pow_sha256_test.cpp
    pow_challenge_test.cpp
    validator_test.cpp
    statement_test.cpp
    state_test.cpp
    tickets_test.cpp
    messages_test.cpp
    replay_test.cpp
    admin_parse_test.cpp
    admin_route_test.cpp
    service_test.cpp
)
target_include_directories(deltafeedback_tests PRIVATE .)
target_link_libraries(deltafeedback_tests PRIVATE
    deltafeedback_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(deltafeedback_tests)

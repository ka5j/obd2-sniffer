# Host-side unit tests for the portable core (lib/), using the Unity framework.
#
#   cmake --preset host-tests && cmake --build --preset host-tests
#   ctest --preset host-tests
#
# Each Unity test function is registered as its own CTest test, so test
# reports list every case individually.

include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(unity)
# Lets each test executable run a single named test: `test_x -n test_case`.
target_compile_definitions(unity PUBLIC UNITY_USE_COMMAND_LINE_ARGS)

find_package(Threads REQUIRED)

function(add_unit_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE core unity Threads::Threads project_warnings)

    # Register one CTest test per UNIT_TEST(...) in the source file.
    file(STRINGS ${name}.c run_lines REGEX "^[ \t]*UNIT_TEST\\([A-Za-z0-9_]+\\)")
    if(NOT run_lines)
        message(FATAL_ERROR "${name}.c has no UNIT_TEST(...) lines")
    endif()
    string(REGEX REPLACE "^test_" "" suite ${name})
    foreach(line IN LISTS run_lines)
        string(REGEX REPLACE ".*UNIT_TEST\\(([A-Za-z0-9_]+)\\).*" "\\1" case ${line})
        add_test(NAME ${suite}.${case} COMMAND ${name} -n ${case})
        # Unity exits 0 if the filter matches nothing; treat that as a failure.
        set_tests_properties(${suite}.${case} PROPERTIES
            FAIL_REGULAR_EXPRESSION "(^|\n)0 Tests "
            LABELS ${suite})
    endforeach()
endfunction()

add_unit_test(test_ring_buffer)

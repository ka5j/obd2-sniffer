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

# add_unit_test(obd/test_isotp): the tests sit in folders that mirror lib/.
function(add_unit_test path)
    get_filename_component(name ${path} NAME)
    add_executable(${name} ${path}.c)
    target_link_libraries(${name} PRIVATE core unity Threads::Threads project_warnings)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # test_support.h

    # Register one CTest test per UNIT_TEST(...) in the source file. The list
    # is read when CMake configures, so re-configure whenever the file
    # changes; otherwise a newly added test would silently not run.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${path}.c)
    file(STRINGS ${path}.c run_lines REGEX "^[ \t]*UNIT_TEST\\([A-Za-z0-9_]+\\)")
    if(NOT run_lines)
        message(FATAL_ERROR "${path}.c has no UNIT_TEST(...) lines")
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

add_unit_test(buffers/test_frame_queue)
add_unit_test(buffers/test_ring_buffer)
add_unit_test(can/test_can_timing)
add_unit_test(console/test_cli)
add_unit_test(console/test_console_core)
add_unit_test(console/test_line_editor)
add_unit_test(fault/test_fault_record)
add_unit_test(link/test_can_text)
add_unit_test(link/test_cobs)
add_unit_test(link/test_crc16)
add_unit_test(link/test_packet)
add_unit_test(obd/test_ecu_node)
add_unit_test(obd/test_ecu_sim)
add_unit_test(obd/test_isotp)
add_unit_test(obd/test_obd2)
add_unit_test(obd/test_obd_client)

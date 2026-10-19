# Script mode (cmake -P): derive the firmware version from git and write it
# into a header. configure_file() leaves OUTPUT untouched when the content is
# unchanged, so this does not force a rebuild every time.
#
# Inputs: SOURCE_DIR, INPUT, OUTPUT, FALLBACK_VERSION

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE FW_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_result)

if(NOT git_result EQUAL 0 OR FW_GIT_DESCRIBE STREQUAL "")
    set(FW_GIT_DESCRIBE "v${FALLBACK_VERSION}-nogit")
endif()

configure_file(${INPUT} ${OUTPUT} @ONLY)

# Portable core: plain C with no hardware or HAL dependencies. The same
# sources are compiled into the firmware and into the host unit tests.

add_library(core STATIC
    src/cli.c
    src/line_editor.c
    src/ring_buffer.c)
target_include_directories(core PUBLIC include)
target_link_libraries(core PRIVATE project_warnings)

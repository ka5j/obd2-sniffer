# Portable core: plain C with no hardware or HAL dependencies. The same
# sources are compiled into the firmware and into the host unit tests.

add_library(core STATIC
    src/can_text.c
    src/can_timing.c
    src/cli.c
    src/cobs.c
    src/console_core.c
    src/crc16.c
    src/ecu_node.c
    src/ecu_sim.c
    src/fault_record.c
    src/frame_queue.c
    src/isotp.c
    src/line_editor.c
    src/obd2.c
    src/obd_client.c
    src/packet.c
    src/ring_buffer.c)
target_include_directories(core PUBLIC include)
target_link_libraries(core PRIVATE project_warnings)

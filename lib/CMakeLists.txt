# Portable core: plain C with no hardware or HAL dependencies. The same
# sources are compiled into the firmware and into the host unit tests.

add_library(core STATIC
    buffers/frame_queue.c
    buffers/ring_buffer.c
    can/can_timing.c
    console/cli.c
    console/console_core.c
    console/line_editor.c
    fault/fault_record.c
    link/can_text.c
    link/cobs.c
    link/crc16.c
    link/packet.c
    obd/ecu_node.c
    obd/ecu_sim.c
    obd/isotp.c
    obd/obd2.c
    obd/obd_client.c)
# Headers sit beside their sources and are included by folder:
# #include "obd/isotp.h".
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PRIVATE project_warnings)

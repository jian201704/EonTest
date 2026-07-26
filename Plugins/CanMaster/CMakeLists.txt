add_library(can-send-plugin MODULE
    src/can-send.json
    src/CanSendPlugin.cpp
)

add_library(can-receive-plugin MODULE
    src/can-receive.json
    src/CanReceivePlugin.cpp
)

find_package(Qt6 COMPONENTS SerialPort QUIET)

target_link_libraries(can-send-plugin PRIVATE eon-sdk eon-infrastructure Qt6::Core Qt6::Network)
target_link_libraries(can-receive-plugin PRIVATE eon-sdk eon-infrastructure Qt6::Core Qt6::Network)

foreach(tgt can-send-plugin can-receive-plugin)
    set_target_properties(${tgt} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
        PREFIX ""
    )
endforeach()

add_library(modbus-master-plugin MODULE
    src/modbusmaster.json
    src/ModbusMasterPlugin.cpp
    src/Transports.cpp
)

target_include_directories(modbus-master-plugin PRIVATE src)
find_package(Qt6 COMPONENTS SerialPort Network QUIET)
target_link_libraries(modbus-master-plugin
    PRIVATE eon-sdk eon-infrastructure Qt6::Core Qt6::SerialPort Qt6::Network)

set_target_properties(modbus-master-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

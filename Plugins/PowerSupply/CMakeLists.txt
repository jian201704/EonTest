add_library(power-supply-plugin MODULE
    src/PowerSupplyPlugin.cpp
    src/powersupply.json
)

# 检测 Qt6::SerialPort 是否可用
find_package(Qt6 COMPONENTS SerialPort QUIET)
if(Qt6SerialPort_FOUND)
    target_link_libraries(power-supply-plugin
        PRIVATE eon-sdk eon-infrastructure Qt6::Core Qt6::SerialPort)
    target_compile_definitions(power-supply-plugin PRIVATE EON_HAS_SERIALPORT=1)
else()
    target_link_libraries(power-supply-plugin
        PRIVATE eon-sdk Qt6::Core)
    message(STATUS "PowerSupply: Qt6::SerialPort not found, using Win32 API fallback")
endif()

set_target_properties(power-supply-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

add_library(multimeter-plugin MODULE
    src/MultimeterPlugin.cpp
    src/multimeter.json
)

# 检测 Qt6::SerialPort 是否可用
find_package(Qt6 COMPONENTS SerialPort QUIET)
if(Qt6SerialPort_FOUND)
    target_link_libraries(multimeter-plugin
        PRIVATE eon-sdk Qt6::Core Qt6::SerialPort)
    target_compile_definitions(multimeter-plugin PRIVATE EON_HAS_SERIALPORT=1)
else()
    target_link_libraries(multimeter-plugin
        PRIVATE eon-sdk Qt6::Core)
    message(STATUS "Multimeter: Qt6::SerialPort not found, using Win32 API fallback")
endif()

set_target_properties(multimeter-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

add_library(scpi-step-plugin MODULE
    src/ScpiStepPlugin.cpp
    src/scpistep.json
)

find_package(Qt6 COMPONENTS SerialPort QUIET)
target_link_libraries(scpi-step-plugin
    PRIVATE eon-sdk eon-infrastructure eon-runtime Qt6::Core Qt6::Network Qt6::SerialPort)
target_compile_definitions(scpi-step-plugin PRIVATE EON_HAS_SERIALPORT=1)

set_target_properties(scpi-step-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

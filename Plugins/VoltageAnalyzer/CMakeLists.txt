add_library(voltage-analyzer-plugin MODULE
    src/VoltageAnalyzerPlugin.cpp
    src/voltageanalyzer.json
)

target_link_libraries(voltage-analyzer-plugin
    PRIVATE
        eon-sdk
        Qt6::Core
)

set_target_properties(voltage-analyzer-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

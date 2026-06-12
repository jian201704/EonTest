add_library(sample-analyzer-plugin MODULE
    src/SampleAnalyzerPlugin.cpp
)

target_link_libraries(sample-analyzer-plugin
    PRIVATE
        eon-sdk
        Qt6::Core
)

set_target_properties(sample-analyzer-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
)

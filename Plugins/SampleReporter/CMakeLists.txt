add_library(sample-reporter-plugin MODULE
    src/SampleReporterPlugin.cpp
)

target_link_libraries(sample-reporter-plugin
    PRIVATE
        eon-sdk
        Qt6::Core
)

set_target_properties(sample-reporter-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
)

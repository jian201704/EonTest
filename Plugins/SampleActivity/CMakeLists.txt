add_library(sample-activity-plugin MODULE
    src/SampleActivityPlugin.cpp
)

target_link_libraries(sample-activity-plugin
    PRIVATE
        eon-sdk
        Qt6::Core
)

set_target_properties(sample-activity-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
)

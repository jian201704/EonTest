add_library(delay-step-plugin MODULE
    src/DelayStepPlugin.cpp
    src/delaystep.json
)

target_link_libraries(delay-step-plugin
    PRIVATE
        eon-sdk
        Qt6::Core
)

set_target_properties(delay-step-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

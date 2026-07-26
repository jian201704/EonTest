add_library(doip-master-plugin MODULE
    src/doipmaster.json
    src/DoipMasterPlugin.cpp
)

target_link_libraries(doip-master-plugin
    PRIVATE eon-sdk eon-infrastructure Qt6::Core Qt6::Network)

set_target_properties(doip-master-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

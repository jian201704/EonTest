add_library(python-step-plugin MODULE
    src/pythonstep.json
    src/PythonStepPlugin.cpp
)

find_package(Qt6 COMPONENTS Network QUIET)
target_link_libraries(python-step-plugin
    PRIVATE eon-sdk eon-infrastructure Qt6::Core Qt6::Network)

set_target_properties(python-step-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

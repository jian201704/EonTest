add_library(json-reporter-plugin SHARED
    src/JsonReporterPlugin.cpp
    src/jsonreporter.json
)

target_include_directories(json-reporter-plugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/SDK/include
)

target_link_libraries(json-reporter-plugin
    PRIVATE
        Qt6::Core
)

set_target_properties(json-reporter-plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

add_library(csv-reporter-plugin SHARED
    src/CsvReporterPlugin.cpp
    src/csvreporter.json
)

target_include_directories(csv-reporter-plugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/SDK/include
)

target_link_libraries(csv-reporter-plugin
    PRIVATE
        Qt6::Core
)

set_target_properties(csv-reporter-plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

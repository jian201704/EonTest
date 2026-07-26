add_library(mqtt-reporter-plugin SHARED
    src/MqttReporterPlugin.cpp
    src/mqttreporter.json
)

target_include_directories(mqtt-reporter-plugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/SDK/include
)

# MQTT 可选
find_package(Qt6 COMPONENTS Mqtt QUIET)
if(Qt6Mqtt_FOUND)
    target_link_libraries(mqtt-reporter-plugin PRIVATE Qt6::Core Qt6::Mqtt)
    target_compile_definitions(mqtt-reporter-plugin PRIVATE EON_HAS_MQTT=1)
else()
    target_link_libraries(mqtt-reporter-plugin PRIVATE Qt6::Core)
    message(STATUS "MqttReporter: DISABLED (Qt6::Mqtt not found)")
endif()

set_target_properties(mqtt-reporter-plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${EON_PLUGIN_OUTPUT_DIR}"
    PREFIX ""
)

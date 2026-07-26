add_library(eon-runtime STATIC
    src/WorkflowEngine.cpp
    src/PluginManager.cpp
    src/ResourceManager.cpp
    src/ResourceDependencyAnalyzer.cpp
    src/EnvSnapshot.cpp
    src/CellWorker.cpp
    src/MatrixManager.cpp
    src/CapabilityRegistry.cpp
    src/JobScheduler.cpp
    src/TelemetryExporter.cpp
)

target_include_directories(eon-runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-runtime
    PUBLIC
        eon-core
        eon-domain
        eon-sdk
        Qt6::Core
        Qt6::Network
)

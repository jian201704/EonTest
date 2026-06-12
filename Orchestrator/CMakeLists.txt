add_executable(eon-orchestrator
    src/main.cpp
)

target_link_libraries(eon-orchestrator
    PRIVATE
        eon-core
        eon-runtime
        Qt6::Core
)

set_target_properties(eon-orchestrator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_RUNTIME_OUTPUT_DIR}"
)

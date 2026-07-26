add_executable(eon-orchestrator
    src/main.cpp
)

target_include_directories(eon-orchestrator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-orchestrator
    PRIVATE
        eon-core
        Qt6::Core
        Qt6::Sql
)

set_target_properties(eon-orchestrator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_RUNTIME_OUTPUT_DIR}"
)

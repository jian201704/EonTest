add_executable(eon-runtime-worker
    src/main.cpp
)

target_link_libraries(eon-runtime-worker
    PRIVATE
        eon-application
        eon-core
        Qt6::Core
)

set_target_properties(eon-runtime-worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_RUNTIME_OUTPUT_DIR}"
)

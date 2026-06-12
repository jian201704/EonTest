add_library(eon-runtime STATIC
    src/WorkflowEngine.cpp
)

target_include_directories(eon-runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-runtime
    PUBLIC
        eon-core
        eon-sdk
        Qt6::Core
)

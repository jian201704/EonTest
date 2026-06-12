add_library(eon-domain STATIC
    src/WorkflowDefinitionIO.cpp
)

target_include_directories(eon-domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-domain
    PUBLIC
        Qt6::Core
)

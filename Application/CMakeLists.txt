add_library(eon-application STATIC
    src/RunWorkflowUseCase.cpp
)

target_include_directories(eon-application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-application
    PUBLIC
        eon-domain
        eon-runtime
        Qt6::Core
)

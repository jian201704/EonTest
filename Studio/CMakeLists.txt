add_executable(eon-studio
    src/main.cpp
)

target_link_libraries(eon-studio
    PRIVATE
        Qt6::Core
        Qt6::Widgets
)

set_target_properties(eon-studio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_RUNTIME_OUTPUT_DIR}"
)

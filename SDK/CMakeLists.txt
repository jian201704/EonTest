add_library(eon-sdk INTERFACE)

target_include_directories(eon-sdk
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-sdk
    INTERFACE
        Qt6::Core
)

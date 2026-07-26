add_library(eon-sdk STATIC
    src/Dut.cpp
    src/ArtifactPipeline.cpp
)

target_include_directories(eon-sdk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-sdk
    PUBLIC
        Qt6::Core
)

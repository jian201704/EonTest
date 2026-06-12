add_library(eon-core STATIC
    include/eon/core/EventBus.h
    src/EventBus.cpp
)

target_include_directories(eon-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-core
    PUBLIC
        Qt6::Core
)

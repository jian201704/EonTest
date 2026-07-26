add_executable(eon-studio
    src/main.cpp
    src/StudioBackend.cpp
    src/WorkflowEditorModel.cpp
    src/BenchSettings.cpp
    include/eon/studio/StudioBackend.h
    include/eon/studio/WorkflowEditorModel.h
    include/eon/studio/BenchSettings.h
    resources.qrc
)

target_include_directories(eon-studio
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 检测 Qt Quick 可用性
find_package(Qt6 COMPONENTS Quick QuickControls2 QUIET)
if(Qt6Quick_FOUND AND Qt6QuickControls2_FOUND)
    message(STATUS "Studio: QML mode ENABLED")
    target_link_libraries(eon-studio
        PRIVATE
            eon-infrastructure
            eon-domain
            eon-core
            eon-sdk
            Qt6::Core
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Sql
            Qt6::Widgets
    )
    target_compile_definitions(eon-studio PRIVATE EON_STUDIO_QML=1)
else()
    message(STATUS "Studio: QML mode DISABLED (Qt6::Quick not found), using QWidget fallback")
    target_link_libraries(eon-studio
        PRIVATE
            Qt6::Core
            Qt6::Widgets
            Qt6::Sql
    )
endif()

set_target_properties(eon-studio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EON_RUNTIME_OUTPUT_DIR}"
)

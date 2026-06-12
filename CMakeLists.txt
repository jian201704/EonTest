cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" EON_VERSION_RAW)
string(STRIP "${EON_VERSION_RAW}" EON_VERSION)

project(EonTest VERSION "${EON_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
include(CTest)

set(EON_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Plugins" CACHE PATH "Plugin output directory")
set(EON_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Runtime output directory")

add_subdirectory(SDK)
add_subdirectory(Core)
add_subdirectory(Domain)
add_subdirectory(Runtime)
add_subdirectory(Application)
add_subdirectory(Plugins/SampleActivity)
add_subdirectory(Plugins/SampleAnalyzer)
add_subdirectory(Plugins/SampleReporter)
add_subdirectory(RuntimeWorker)
add_subdirectory(Orchestrator)
add_subdirectory(Studio)

if(BUILD_TESTING)
    add_test(
        NAME orchestrator_smoke_minimal
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.workflow.json
    )
endif()

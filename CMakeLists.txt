cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" EON_VERSION_RAW)
string(STRIP "${EON_VERSION_RAW}" EON_VERSION)

project(EonTest VERSION "${EON_VERSION}" LANGUAGES CXX)

# 使 EON_VERSION 在所有目标中可用
add_compile_definitions(EON_VERSION="${EON_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql Network)
include(CTest)
include(GNUInstallDirs)

set(EON_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Plugins" CACHE PATH "Plugin output directory")
set(EON_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Runtime output directory")

# QXlsx — Excel 读写库
set(QT_VERSION_MAJOR 6)
add_subdirectory(ThirdParty/QXlsx)

# 生成 .xlsx 测试用例文件（从 .json workflow 转换）
set(GEN_XLSX_SRC ${CMAKE_SOURCE_DIR}/cmake/GenerateTestXlsx.cpp)
add_executable(eon-gen-test-xlsx EXCLUDE_FROM_ALL ${GEN_XLSX_SRC})
target_link_libraries(eon-gen-test-xlsx PRIVATE QXlsx Qt6::Core)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
           ${CMAKE_SOURCE_DIR}/Workflows/branching.test.xlsx
           ${CMAKE_SOURCE_DIR}/Workflows/compensation.test.xlsx
           ${CMAKE_SOURCE_DIR}/Workflows/parallel.test.xlsx
           ${CMAKE_SOURCE_DIR}/Workflows/recovery.test.xlsx
    COMMAND $<TARGET_FILE:eon-gen-test-xlsx>
            "${CMAKE_SOURCE_DIR}/Workflows"
    COMMENT "Generating .xlsx test workflow files..."
    DEPENDS eon-gen-test-xlsx
)
# 先手动运行一次 cmake --build . --target eon-gen-test-xlsx 来生成

add_subdirectory(SDK)
add_subdirectory(Core)
add_subdirectory(Domain)
add_subdirectory(Infrastructure)
add_subdirectory(Runtime)
add_subdirectory(Application)
add_subdirectory(Plugins/SampleActivity)
add_subdirectory(Plugins/SampleAnalyzer)
add_subdirectory(Plugins/SampleReporter)
add_subdirectory(Plugins/CsvReporter)
add_subdirectory(Plugins/JsonReporter)
add_subdirectory(Plugins/MqttReporter)
add_subdirectory(Plugins/PowerSupply)
add_subdirectory(Plugins/Multimeter)
add_subdirectory(Plugins/DelayStep)
add_subdirectory(Plugins/ScpiStep)
add_subdirectory(Plugins/VoltageAnalyzer)
add_subdirectory(Plugins/ModbusMaster)
add_subdirectory(Plugins/CanMaster)
add_subdirectory(Plugins/PythonStep)
add_subdirectory(Plugins/DoipMaster)
add_subdirectory(RuntimeWorker)
add_subdirectory(Orchestrator)
add_subdirectory(Studio)

# 部署 Qt6::SerialPort DLL 到运行时目录（插件动态加载需要）
if(Qt6SerialPort_FOUND)
    add_custom_command(TARGET eon-runtime-worker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::SerialPort>"
            "${EON_RUNTIME_OUTPUT_DIR}/"
        COMMENT "Copy Qt6SerialPort.dll to runtime output"
    )
endif()

# 压力测试（ResourceManager）
add_executable(eon-rm-stress-test EXCLUDE_FROM_ALL
    Tests/ResourceManagerStressTest.cpp
)
target_link_libraries(eon-rm-stress-test
    PRIVATE eon-runtime eon-sdk Qt6::Core
)

add_executable(eon-response-decoder-test
    Tests/ResponseDecoderTest.cpp
)
target_link_libraries(eon-response-decoder-test
    PRIVATE eon-infrastructure Qt6::Core
)

if(BUILD_TESTING)
    add_test(
        NAME response_decoder_unit
        COMMAND $<TARGET_FILE:eon-response-decoder-test>
    )

    add_test(
        NAME orchestrator_smoke_minimal
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
    )

    add_test(
        NAME orchestrator_resume_smoke_seed
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --state
            ${CMAKE_BINARY_DIR}/orchestration-resume-smoke.db
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
    )

    add_test(
        NAME orchestrator_resume_smoke_resume
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --resume
            --state
            ${CMAKE_BINARY_DIR}/orchestration-resume-smoke.db
    )
    set_tests_properties(orchestrator_resume_smoke_resume PROPERTIES DEPENDS orchestrator_resume_smoke_seed)

    add_test(
        NAME orchestrator_parallel_smoke
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --cells
            2
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/parallel.test.xlsx
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
    )

    add_test(
        NAME orchestrator_stop_on_failure_smoke
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --stop-on-failure
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/compensation.test.xlsx
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
    )
    set_tests_properties(orchestrator_stop_on_failure_smoke PROPERTIES WILL_FAIL TRUE)

    # Phase 7: FIFO 调度策略测试
    add_test(
        NAME orchestrator_fifo_policy
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --scheduling-policy fifo
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
            ${CMAKE_SOURCE_DIR}/Workflows/branching.test.xlsx
    )

    # Phase 7: 资源锁阻塞测试 (两个 workflow 共享资源锁)
    add_test(
        NAME orchestrator_resource_lock
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --cells 1
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/parallel.test.xlsx
    )

    # Phase 8: 含遥测 + 指标的长时间多 CELL 测试
    add_test(
        NAME orchestrator_multi_cell_stress
        COMMAND
            $<TARGET_FILE:eon-orchestrator>
            --cells 3
            ${EON_PLUGIN_OUTPUT_DIR}
            ${CMAKE_SOURCE_DIR}/Workflows/minimal.test.xlsx
            ${CMAKE_SOURCE_DIR}/Workflows/branching.test.xlsx
            ${CMAKE_SOURCE_DIR}/Workflows/recovery.test.xlsx
    )
endif()

install(TARGETS eon-orchestrator eon-runtime-worker eon-studio
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS sample-activity-plugin sample-analyzer-plugin sample-reporter-plugin
    RUNTIME DESTINATION plugins
    LIBRARY DESTINATION plugins
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/Workflows/
    DESTINATION workflows
    FILES_MATCHING PATTERN "*.json"
)

install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/VERSION
    DESTINATION .
)

set(CPACK_PACKAGE_NAME "EonTest")
set(CPACK_PACKAGE_VENDOR "jian201704")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "EonTest")
set(CPACK_PACKAGE_FILE_NAME "EonTest-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

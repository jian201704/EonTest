add_library(eon-infrastructure STATIC
    src/Hardware/HardwareManager.cpp
    src/Drivers/VirtualBusDriver.cpp
    src/Drivers/TcpBusDriver.cpp
    src/Drivers/SocketCanDriver.cpp
    src/Drivers/PowerSupplyDriver.cpp
    src/Drivers/MultimeterDriver.cpp
    src/InstrumentManager.cpp
    src/Backends/PythonProcessBackend.cpp
    src/Protocols/UdsProtocolLayer.cpp
    src/Protocols/DoipProtocolLayer.cpp
    src/ResponseDecoder.cpp
    src/Persistence/SqliteTestRepository.cpp
    src/Persistence/TestResultCollector.cpp
    src/Persistence/SqliteRecipeRepository.cpp
    src/XlsxParser.cpp               # <-- Excel 三 Sheet 解析器
    src/SerialScpiIO.cpp             # <-- 串口 SCPI I/O
    src/TcpScpiIO.cpp               # <-- TCP/IP SCPI I/O (LAN 仪器)
    src/VisaScpiIO.cpp              # <-- VISA SCPI I/O (统一 ASRL/TCPIP/USB/GPIB)
    src/ScpiInstrument.cpp           # <-- SCPI 仪器基类
)

# SerialPort 驱动 — 仅当 Qt6::SerialPort 可用时编译
find_package(Qt6 COMPONENTS SerialPort QUIET)
if(Qt6SerialPort_FOUND)
    target_sources(eon-infrastructure PRIVATE
        src/Drivers/SerialPortDriver.cpp
        src/Backends/SerialBackend.cpp
    )
    target_link_libraries(eon-infrastructure PUBLIC Qt6::SerialPort)
    target_compile_definitions(eon-infrastructure PUBLIC EON_HAS_SERIALPORT=1)
    message(STATUS "SerialPort driver: ENABLED")
else()
    message(STATUS "SerialPort driver: DISABLED (Qt6::SerialPort not found)")
endif()

target_include_directories(eon-infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eon-infrastructure
    PUBLIC
        eon-sdk
        eon-core
        eon-domain
        Qt6::Core
        Qt6::Network
        Qt6::Sql
    PRIVATE
        QXlsx
)

# SocketCAN 仅在 Linux 编译
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(eon-infrastructure PRIVATE Q_OS_LINUX)
endif()

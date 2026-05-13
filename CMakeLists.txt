cmake_minimum_required(VERSION 3.21)

project(EngineSCADA
    VERSION 1.0.0
    DESCRIPTION "SCADA-система обкатки дизельного двигателя"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets PrintSupport)
find_package(Qt6 OPTIONAL_COMPONENTS SerialPort)

set(QCUSTOMPLOT_SRC third_party/qcustomplot.cpp)
set(QCUSTOMPLOT_INC third_party)

# ── Заголовки с Q_OBJECT — MOC должен их видеть явно ────────────────────────
set(MOC_HEADERS
    include/core/virtual_mcu.h
    include/modbus/modbus_slave_adapter.h
    include/modbus/modbus_master_adapter.h
    include/modbus/emulated_modbus_transport.h
    include/modbus/serial_modbus_transport.h
    include/gui/main_window.h
    include/gui/actuator_window.h
    include/gui/history_window.h
    include/gui/fault_injection_dialog.h
)

set(CORE_SOURCES
    src/core/datatypes.cpp
    src/core/engine_model.cpp
    src/core/actuator_driver.cpp
    src/core/sensor_simulator.cpp
    src/core/pi_controller.cpp
    src/core/virtual_mcu.cpp
    src/core/high_level_controller.cpp
    src/core/data_logger.cpp
    src/core/reporter.cpp
    src/core/session_history.cpp
)

set(MODBUS_SOURCES
    src/modbus/modbus_utils.cpp
    src/modbus/modbus_slave_adapter.cpp
    src/modbus/modbus_master_adapter.cpp
    src/modbus/emulated_modbus_transport.cpp
    src/modbus/serial_modbus_transport.cpp
)

set(GUI_SOURCES
    src/gui/main_window.cpp
    src/gui/actuator_window.cpp
    src/gui/history_window.cpp
    src/gui/fault_injection_dialog.cpp
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    ${CORE_SOURCES}
    ${MODBUS_SOURCES}
    ${GUI_SOURCES}
    ${MOC_HEADERS}        # ← явно добавляем .h с Q_OBJECT для MOC
    ${QCUSTOMPLOT_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    include/core
    include/modbus
    include/gui
    include/utils
    ${QCUSTOMPLOT_INC}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::PrintSupport
)

if(Qt6SerialPort_FOUND)
    message(STATUS "Qt6SerialPort найден — реальный COM-порт доступен")
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::SerialPort)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SERIAL_PORT)
else()
    message(STATUS "Qt6SerialPort НЕ найден — используется эмулированный транспорт")
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Running windeployqt..."
    )
endif()

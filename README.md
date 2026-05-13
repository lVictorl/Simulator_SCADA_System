# SCADA — Обкатка дизельного двигателя

## Стек технологий
| Компонент | Версия |
|---|---|
| C++ | 17 |
| Qt | 6.5 (Widgets, SerialPort, PrintSupport) |
| CMake | 3.21+ |
| QCustomPlot | 2.x |

---

## Структура проекта

```
engine_scada/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── datatypes.cpp
│   │   ├── engine_model.cpp
│   │   ├── actuator_driver.cpp
│   │   ├── sensor_simulator.cpp
│   │   ├── pi_controller.cpp
│   │   ├── virtual_mcu.cpp
│   │   ├── high_level_controller.cpp
│   │   ├── data_logger.cpp
│   │   ├── reporter.cpp
│   │   └── session_history.cpp
│   ├── modbus/
│   │   ├── modbus_utils.cpp
│   │   ├── modbus_slave_adapter.cpp
│   │   ├── modbus_master_adapter.cpp
│   │   ├── emulated_modbus_transport.cpp
│   │   └── serial_modbus_transport.cpp
│   └── gui/
│       ├── main_window.cpp
│       ├── actuator_window.cpp
│       ├── history_window.cpp
│       └── fault_injection_dialog.cpp
├── include/
│   ├── core/       (.h-файлы ядра)
│   ├── modbus/     (.h-файлы Modbus)
│   └── gui/        (.h-файлы GUI)
├── third_party/
│   ├── qcustomplot.h    ← скачать с qcustomplot.com
│   └── qcustomplot.cpp  ← скачать с qcustomplot.com
└── logs/               ← создаётся автоматически
```

---

## Быстрый старт

### 1. Зависимости
```bash
# Linux
sudo apt install qt6-base-dev qt6-serialport-dev qt6-base-private-dev cmake
```

### 2. QCustomPlot
Скачайте `qcustomplot.h` и `qcustomplot.cpp` с https://www.qcustomplot.com/
и поместите в папку `third_party/`.

### 3. Сборка
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### 4. Запуск
```bash
./EngineSCADA
```

---

## Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│  MainWindow (GUI thread)                                    │
│  HighLevelController → ModbusMasterAdapter                  │
│        ↕  Qt::QueuedConnection                              │
│  EmulatedModbusTransport (заменяется SerialModbusTransport) │
│        ↕                                                    │
│  ModbusSlaveAdapter (thread)                                │
│        ↕                                                    │
│  VirtualMCU (thread)                                        │
│  ├── EngineModel          (физика двигателя)                │
│  ├── SensorSimulator      (шум + отказы датчиков)          │
│  ├── ActuatorDriver       (инерционность актуаторов)        │
│  └── PIController         (ПИ-регулятор дросселя)          │
└─────────────────────────────────────────────────────────────┘
```

### Потоки
| Поток | Период | Задача |
|---|---|---|
| VirtualMCU | 20 мс | Симуляция физики, конечный автомат |
| ModbusSlaveAdapter | ~5 мс | Декодирование Modbus-кадров |
| ModbusMasterAdapter | 50 мс | Опрос телеметрии, watchdog |
| GUI (main thread) | По событиям Qt | Отображение |

### Протокол Modbus (эмулированный)
- **0x10** (Write Multiple Registers), адрес 0, 32 регистра → MCUCommand
- **0x03** (Read Holding Registers), адрес 100, 33 регистра → MCUTelemetry
- Масштабирование: `double ×1000 → int32` (big-endian, 4 байта)
- CRC-16 (полином 0xA001, little-endian)

### Переход на реальное железо
Замените `EmulatedModbusTransport` на `SerialModbusTransport`:
```cpp
// В main.cpp:
auto *transport = new SerialModbusTransport("/dev/ttyUSB0", 115200);
transport->open();
```
Остальной код не меняется.

---

## Форматы файлов

### CSV (логи телеметрии)
```
timestamp,engine_rpm,torque,engine_temp,oil_pressure,fuel_pressure,
boost_pressure,dyno_motor_temp,resistor_temp,oil_level,fuel_level
```

### HTML-отчёт
Самодостаточный файл с inline-графиками (PNG base64).

### session_index.json
Индекс всех завершённых сессий в `logs/session_index.json`.

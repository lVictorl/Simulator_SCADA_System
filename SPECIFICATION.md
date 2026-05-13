# EngineSCADA — Техническая спецификация

> SCADA-система автоматизации процесса обкатки дизельных двигателей  
> Версия: 1.0.0 | C++17 | Qt 6.5 | Modbus RTU

---

## Содержание

1. [Обзор архитектуры](#1-обзор-архитектуры)
2. [Слой данных — datatypes](#2-слой-данных)
3. [Ядро симуляции](#3-ядро-симуляции)
   - 3.1 EngineModel
   - 3.2 ActuatorDriver
   - 3.3 SensorSimulator
   - 3.4 PIController
4. [Виртуальный МКУ — VirtualMCU](#4-virtualmcu)
5. [Слой Modbus](#5-слой-modbus)
   - 5.1 ModbusUtils
   - 5.2 IModbusTransport
   - 5.3 EmulatedModbusTransport
   - 5.4 SerialModbusTransport
   - 5.5 ModbusSlaveAdapter
   - 5.6 ModbusMasterAdapter
6. [Управление верхнего уровня](#6-highlevelcontroller)
7. [Хранение данных](#7-хранение-данных)
   - 7.1 DataLogger
   - 7.2 Reporter
   - 7.3 SessionHistory
8. [GUI](#8-gui)
   - 8.1 MainWindow
   - 8.2 ActuatorWindow
   - 8.3 HistoryWindow
   - 8.4 FaultInjectionDialog
9. [Точка входа — main.cpp](#9-mainсpp)
10. [Протокол Modbus — карта регистров](#10-протокол-modbus)
11. [Потоки и синхронизация](#11-потоки-и-синхронизация)
12. [История коммитов](#12-история-коммитов)

---

## 1. Обзор архитектуры

```
┌──────────────────────────────────────────────────────────────────────┐
│  GUI THREAD (Qt event loop)                                          │
│                                                                      │
│  MainWindow                                                          │
│  ├── HighLevelController  ──► ModbusMasterAdapter::sendCommand()     │
│  └── слушает: ModbusMasterAdapter::telemetryReady(MCUTelemetry)      │
└────────────────────────┬─────────────────────────────────────────────┘
                         │ Qt::QueuedConnection (межпоточный сигнал)
┌────────────────────────▼─────────────────────────────────────────────┐
│  MASTER THREAD (QTimer 50 мс)                                        │
│                                                                      │
│  ModbusMasterAdapter                                                 │
│  ├── pollAll() → readTelemetry() → emit telemetryReady()             │
│  └── sendCommand()  ← вызов из GUI через QMetaObject::invokeMethod   │
└────────────────────────┬─────────────────────────────────────────────┘
                         │ IModbusTransport (EmulatedModbusTransport)
┌────────────────────────▼─────────────────────────────────────────────┐
│  SLAVE THREAD (QThread, опрос каждые 5 мс)                           │
│                                                                      │
│  ModbusSlaveAdapter                                                  │
│  ├── run(): разбирает входящие Modbus-кадры                          │
│  ├── 0x10 → decodeCommand() → VirtualMCU::enqueueCommand()           │
│  └── 0x03 → buildTelemetryRegisters() → ответ мастеру               │
└────────────────────────┬─────────────────────────────────────────────┘
                         │ updateTelemetry() / enqueueCommand()
┌────────────────────────▼─────────────────────────────────────────────┐
│  MCU THREAD (QThread, шаг dt = 20 мс)                                │
│                                                                      │
│  VirtualMCU                                                          │
│  ├── Конечный автомат: IDLE→COLD_BREAKIN→WARMUP→HOT_*→STOPPED       │
│  ├── EngineModel.step(dt)     — физика двигателя                     │
│  ├── ActuatorDriver.apply()   — инерционность механизмов             │
│  ├── SensorSimulator.read()   — шум + отказы датчиков                │
│  ├── PIController.update()    — авторегулятор дросселя               │
│  └── sendTelemetry() → ModbusSlaveAdapter::updateTelemetry()         │
└──────────────────────────────────────────────────────────────────────┘
```

### Принцип замены транспорта (реальное железо)
```cpp
// Было (эмуляция):
auto *transport = new EmulatedModbusTransport(slave);

// Стало (реальный COM-порт):
auto *transport = new SerialModbusTransport("/dev/ttyUSB0", 115200);
transport->open();
```
Всё остальное — без изменений.

---

## 2. Слой данных

**Файл:** `include/core/datatypes.h`, `src/core/datatypes.cpp`

Центральный заголовочный файл. Определяет все типы данных проекта.
Не содержит логики — только структуры, перечисления и мета-регистрацию Qt.

### Перечисления

#### `BreakInMode : quint16`
Режим обкатки. Кодируется в регистр 1 команды Modbus 0x10.

| Значение | Код | Описание |
|---|---|---|
| `COLD` | 0 | Холодная обкатка (двигатель не запущен) |
| `HOT_NOLOAD` | 1 | Горячая без нагрузки |
| `HOT_LOAD` | 2 | Горячая под нагрузкой |

```cpp
QString breakInModeDisplayName(BreakInMode mode);
// → "Холодная обкатка" / "Горячая без нагрузки" / "Горячая под нагрузкой"
```

#### `BreakInState : quint16`
Состояние конечного автомата VirtualMCU.

```
IDLE → (start) → COLD_BREAKIN → (elapsed) → STOPPED
IDLE → (start) → WARMUP → (elapsed) → HOT_NOLOAD → (elapsed) → STOPPED
IDLE → (start) → WARMUP → (elapsed) → HOT_LOAD  → (elapsed) → STOPPED
любое → (превышение лимита или emergency_stop) → EMERGENCY → (reset) → IDLE
```

#### `DynoMotorMode : quint16`
| `SPIN` | Прокрутка двигателя (холодная обкатка) |
| `BRAKE` | Торможение — создание нагрузки |

#### `SensorFaultType : quint16`
| `NONE` | Исправен |
| `OPEN_CIRCUIT` | Обрыв → возвращает `NaN` |
| `SHORT_CIRCUIT` | КЗ → возвращает `1e6` |
| `OUT_OF_RANGE` | Уход диапазона → `trueValue * 10 + 500` |

---

### Структуры данных

#### `CriticalLimits`
Пороговые значения для аварийного останова. Задаются оператором в GUI.

| Поле | Тип | Умолчание | Единица |
|---|---|---|---|
| `max_engine_temp` | double | 95.0 | °C |
| `min_oil_pressure` | double | 0.8 | бар |
| `max_oil_pressure` | double | 6.0 | бар |
| `min_fuel_pressure` | double | 2.0 | бар |
| `max_fuel_pressure` | double | 5.0 | бар |
| `max_boost_pressure` | double | 2.5 | бар |
| `max_engine_rpm` | double | 4000.0 | об/мин |
| `max_dyno_motor_temp` | double | 150.0 | °C |
| `max_resistor_temp` | double | 200.0 | °C |

#### `SensorData`
Снимок показаний всех датчиков в момент времени.

| Поле | Тип | Единица |
|---|---|---|
| `timestamp` | double | с (монотонное) |
| `engine_rpm` | double | об/мин |
| `torque` | double | Н·м |
| `engine_temp` | double | °C |
| `oil_pressure` | double | бар |
| `fuel_pressure` | double | бар |
| `boost_pressure` | double | бар |
| `dyno_motor_temp` | double | °C |
| `resistor_temp` | double | °C |
| `oil_level` | double | % |
| `fuel_level` | double | % |
| `fault_mask` | quint16 | битовая маска (биты 0–5) |

**Битовая маска fault_mask:**
```
бит 0 = отказ датчика оборотов
бит 1 = отказ датчика момента
бит 2 = отказ датчика температуры ДВС
бит 3 = отказ датчика давлений
бит 4 = отказ датчика температуры ЭД
бит 5 = отказ датчика температуры резистора
```

#### `ActuatorSetpoints`
Логические уставки (что хотим получить).
```cpp
double throttle;        // 0..100 %
double target_speed;    // целевые об/мин (в режиме SPIN)
double target_torque;   // целевой Н·м (в режиме BRAKE)
DynoMotorMode dyno_mode;
bool engine_running, engine_fan, dyno_motor_fan, resistor_fan;
```

#### `ActuatorSignals`
Физические сигналы на оборудование (результат работы ActuatorDriver).
```cpp
int    throttle_pwm_duty;    // 0..4095 (12-бит ШИМ)
double dyno_speed_voltage;   // 0..10 В (для режима SPIN)
double dyno_torque_current;  // 4..20 мА (для режима BRAKE)
bool   engine_fan_on, dyno_motor_fan_on, resistor_fan_on;
```

#### `ActuatorFeedback`
Фактические значения после инерционности (что реально происходит).

#### `MCUCommand`
Команда от GUI к виртуальному МКУ.

| `command_id` | Назначение |
|---|---|
| `"start"` | Запуск испытания |
| `"stop"` | Нормальный останов |
| `"emergency_stop"` | Аварийный останов |
| `"next_stage"` | Принудительный переход этапа |
| `"reset_emergency"` | Сброс аварии |
| `"set_throttle"` | Установка дросселя вручную |
| `"set_mode"` | Смена режима без старта |
| `"inject_fault"` | Внедрение отказа датчика |
| `"clear_fault"` | Сброс отказа датчика |

#### `MCUTelemetry`
Полный пакет данных от МКУ к GUI (50 мс).
Содержит: `SensorData`, состояние автомата, режим, дроссель,
режим ЭД, состояние вентиляторов, списки ошибок и предупреждений,
`ActuatorFeedback`.

#### `SessionInfo`
Метаданные завершённой сессии. Сохраняется в `session_index.json`.

---

### Регистрация мета-типов

```cpp
// В конце datatypes.h — макросы для Q_DECLARE_METATYPE
Q_DECLARE_METATYPE(MCUTelemetry)  // и другие типы...

// Вспомогательная функция — вызывается один раз из main()
void registerMetaTypes();
// Обязательна для Qt::QueuedConnection через границы потоков!
```

---

## 3. Ядро симуляции

### 3.1 EngineModel

**Файл:** `include/core/engine_model.h`, `src/core/engine_model.cpp`

Математическая модель дизельного двигателя и стенда.
**Не содержит логики управления** — только физические уравнения.

#### Конструктор
```cpp
explicit EngineModel(double ambientTemp = 20.0);
// ambientTemp — температура окружающей среды в °C
```

#### Основной метод
```cpp
void step(double dt, const ActuatorSetpoints &cmd);
```
Выполняет один шаг численного интегрирования длительностью `dt` секунд.
Вызывается из VirtualMCU каждые 20 мс.

**Алгоритм step() — 5 блоков:**

**Блок 1: Обороты и момент**
```
Холодная прокрутка (engine_running == false):
  rpm += (target_speed - rpm) * (dt / inertia_tau)   // апериодическое звено 1-го порядка
  torque = friction_coeff * rpm + 5.0                 // трение + константа

Горячий режим (engine_running == true):
  engine_torque = throttle * engine_torque_gain       // мощность по дросселю
  friction_torque = rpm * friction_coeff
  load_torque = (BRAKE) ? target_torque : 0.0
  net_torque = engine_torque - load_torque - friction_torque
  rpm += net_torque * (dt / j_inertia)               // 2-й закон Ньютона для вращения
```

**Блок 2: Тепловая модель ДВС**
```
heat    = engine_running ? throttle * heat_from_throttle : 0.0
cooling = natural_cooling * (engine_temp - ambient)
fan_cool = engine_fan ? fan_cooling_rate * (engine_temp - ambient) : 0.0
engine_temp += (heat - cooling - fan_cool) * dt
```

**Блок 3: Давления**
```
oil_pressure  = clamp(1.5 + 0.0005*rpm - 0.005*(engine_temp-20), 0, 20)
fuel_pressure = 3.0 + 0.0001 * rpm
boost_pressure = (running && rpm>1000) ? 0.8*(rpm-1000)/3000 * throttle/100 : 0.0
```

**Блок 4: Уровни жидкостей**
```
oil_level  -= 0.001 * throttle * dt   // расход масла
fuel_level -= 0.005 * throttle * dt   // расход топлива
```

**Блок 5: Тепловые модели ЭД и резистора**
```
power_loss = (running && BRAKE) ? dyno_heat_factor * |torque| * rpm : 0.0
dyno_motor_temp += (power_loss - natural_cooling*(T-amb) - fan*fan_cooling*(T-amb)) * dt
resistor_temp   += (power_loss*0.9 - ...) * dt
```

#### Физические коэффициенты (приватные поля)

| Поле | Значение | Смысл |
|---|---|---|
| `m_inertia_tau` | 0.3 с | Постоянная времени инерции (холодная прокрутка) |
| `m_friction_coeff` | 0.02 | Коэффициент трения (Н·м / (об/мин)) |
| `m_engine_torque_gain` | 0.6 | КПД двигателя (Н·м на 1% дросселя) |
| `m_j_inertia` | 0.5 | Момент инерции ротора, кг·м² |
| `m_heat_from_throttle` | 0.015 | Удельный нагрев на 1% дросселя |
| `m_natural_cooling` | 0.005 | Коэффициент естественного охлаждения |
| `m_fan_cooling_rate` | 0.03 | Коэффициент охлаждения вентилятором |
| `m_dyno_heat_factor` | 0.0002 | Фактор нагрева стенда (W / (Н·м · об/мин)) |

#### Метод state()
```cpp
SensorData state() const;
// Возвращает «истинные» значения без шума датчиков.
// timestamp берётся из глобального QElapsedTimer (singleton).
```

---

### 3.2 ActuatorDriver

**Файл:** `include/core/actuator_driver.h`, `src/core/actuator_driver.cpp`

Виртуальный драйвер исполнительных механизмов.
Моделирует **инерционность**: заслонка не открывается мгновенно,
ЭД разгоняется постепенно, вентиляторы включаются с задержкой.

#### Конструктор
```cpp
ActuatorDriver(double throttleRate   = 40.0,   // %/с — скорость открытия заслонки
               double dynoAccel      = 500.0,  // об/мин/с — разгон ЭД
               double torqueSlewRate = 100.0,  // Н·м/с — нарастание момента
               double fanDelay       = 0.5);   // с — задержка включения вентилятора
```

#### Основной метод
```cpp
void applySetpoints(const ActuatorSetpoints &sp, double dt);
```

**Алгоритм:**

1. **Дроссель:** `actual_throttle = moveToward(actual, target, rate * dt)`
   → ШИМ = actual / 100 * 4095

2. **ЭД в режиме SPIN:** `actual_speed = moveToward(actual, target, accel * dt)`
   → voltage = actual_speed / 3000 * 10 В

3. **ЭД в режиме BRAKE:** `actual_torque = moveToward(actual, target, slew * dt)`
   → current = 4.0 + actual_torque / 200 * 16 мА (диапазон 4..20 мА)

4. **Вентиляторы:** состояние меняется только когда таймер ≥ fanDelay

#### Вспомогательные методы

```cpp
// Линейное движение к цели — используется для инерционности
static double moveToward(double current, double target, double maxStep) noexcept;

// Управление таймером задержки вентилятора
bool updateFanTimer(double &timer, bool &actual, bool cmd, double dt);
```

#### Геттеры
```cpp
const ActuatorSignals  &actuatorSignals() const;  // физические сигналы
const ActuatorFeedback &feedback()        const;  // фактические значения
```

---

### 3.3 SensorSimulator

**Файл:** `include/core/sensor_simulator.h`, `src/core/sensor_simulator.cpp`

Симулятор реальных датчиков — добавляет гауссов шум к истинным
значениям EngineModel и может моделировать неисправности.

#### Конструктор
```cpp
explicit SensorSimulator(const EngineModel &model, unsigned int seed = 42);
// model — ссылка на модель (не владеет, не копирует)
// seed  — инициализатор ГПСЧ для воспроизводимости шума
```

#### Управление отказами

```cpp
void setFault(int sensorIndex, SensorFaultType type);
// sensorIndex: 0=rpm, 1=torque, 2=engine_temp, 3=pressures, 4=dyno_temp, 5=resistor_temp

void clearFault(int sensorIndex);
void clearAllFaults();
```

#### Чтение данных
```cpp
SensorData read() const;
```
Для каждого канала применяет:
- `NONE`: `value + N(0, σ)` — гауссов шум
- `OPEN_CIRCUIT`: `NaN`
- `SHORT_CIRCUIT`: `1e6`
- `OUT_OF_RANGE`: `trueValue * 10 + 500`

**СКО шумов:**
| Канал | σ |
|---|---|
| Обороты | 5.0 об/мин |
| Момент | 0.5 Н·м |
| Температуры | 0.3 °C |
| Давления | 0.02 бар |
| Уровни | 0.1 % |

**fault_mask** в возвращаемом `SensorData` — битовое OR всех активных отказов.

---

### 3.4 PIController

**Файл:** `include/core/pi_controller.h`, `src/core/pi_controller.cpp`

Пропорционально-интегральный регулятор для автоматического
управления дросселем по целевым оборотам.

#### Конструктор
```cpp
PIController(double kp          = 0.05,   // пропорциональный коэффициент
             double ki          = 0.01,   // интегральный коэффициент
             double maxIntegral = 100.0); // ограничение накопления (anti-windup)
```

#### Методы

```cpp
// Вычисляет управляющий сигнал [0..100] %
double update(double setpoint, double actual, double dt);
// error = setpoint - actual
// integral += error * dt  (с ограничением [-maxIntegral, +maxIntegral])
// output = clamp(Kp * error + Ki * integral, 0, 100)

// Сброс интегральной составляющей (при смене этапа)
void reset();
```

**Использование в VirtualMCU:**
```
WARMUP: target = 800 об/мин (холостой ход при прогреве)
HOT_NOLOAD / HOT_LOAD: target = m_target_rpm (задан оператором)
```

---

## 4. VirtualMCU

**Файл:** `include/core/virtual_mcu.h`, `src/core/virtual_mcu.cpp`

Виртуальный микроконтроллер — эмулирует прошивку реального STM32.
Реализует конечный автомат процесса обкатки. Работает в отдельном `QThread`.

#### Конструктор
```cpp
explicit VirtualMCU(QObject *parent = nullptr,
                    int slaveId = 1,       // адрес Modbus slave
                    double dt   = 0.02);   // период симуляции, с
```

#### Публичные методы

```cpp
void setSlaveAdapter(ModbusSlaveAdapter *adapter);
// Устанавливает ссылку на слой Modbus для отправки телеметрии.
// Вызывается до start().

void enqueueCommand(const MCUCommand &cmd);
// Потокобезопасное добавление команды в очередь.
// Вызывается из ModbusSlaveAdapter (другой поток).

void stopMCU();
// Останавливает поток: устанавливает флаг + requestInterruption().
```

#### Главный цикл — run()

Выполняется в отдельном потоке с периодом `dt = 20 мс`.

```
Каждые 20 мс:
1. Обработать все команды из очереди (processCommand)
2. Если IDLE/STOPPED/EMERGENCY → отправить пустую телеметрию, продолжить
3. Если force_next_stage → performNextStage()
4. updateActuatorsLogic() — ПИ-регулятор, вентиляторы
5. Сформировать ActuatorSetpoints
6. actuator_driver.applySetpoints(sp, dt)
7. engine.step(dt, effective_setpoints)  ← с фактическими значениями ОС
8. Проверить переходы по времени (elapsed >= duration)
9. sensors.read() + checkLimits() → если ошибка → EMERGENCY
10. sendTelemetry() → ModbusSlaveAdapter::updateTelemetry()
```

#### Конечный автомат

```cpp
void handleStart(const MCUCommand &cmd);
// Запускает процесс обкатки. Переходы:
//   COLD:      → COLD_BREAKIN
//   HOT_NOLOAD: → WARMUP (dyno_mode = SPIN)
//   HOT_LOAD:   → WARMUP (dyno_mode = BRAKE)

void performNextStage();
// Принудительный переход:
//   COLD_BREAKIN → STOPPED
//   WARMUP       → HOT_NOLOAD или HOT_LOAD
//   HOT_*        → STOPPED

void updateActuatorsLogic();
// Автоматическое управление:
//   - Вентиляторы включаются за 10°C до критической температуры
//   - ПИ-регулятор дросселя в горячих режимах

std::pair<QStringList,QStringList> checkLimits(const SensorData &s) const;
// Проверяет критические пределы.
// errors → переход в EMERGENCY
// warnings → отображаются в журнале
```

#### Внутренние компоненты

| Поле | Тип | Описание |
|---|---|---|
| `m_engine` | EngineModel | Физическая модель |
| `m_sensors` | SensorSimulator | Датчики с шумом |
| `m_actuator_driver` | ActuatorDriver | Инерционность механизмов |
| `m_pi_controller` | PIController | Авторегулятор дросселя |
| `m_cmd_queue` | QQueue\<MCUCommand\> | Очередь команд |
| `m_cmd_mutex` | QMutex | Защита очереди |
| `m_stop_flag` | QAtomicInt | Флаг остановки |

---

## 5. Слой Modbus

### 5.1 ModbusUtils

**Файл:** `include/modbus/modbus_utils.h`, `src/modbus/modbus_utils.cpp`

Пространство имён со вспомогательными функциями протокола Modbus RTU.

```cpp
namespace ModbusUtils {

// CRC-16 (полином 0xA001, стандарт Modbus RTU)
quint16 calcCRC16(const QByteArray &data);

// Упаковка/распаковка IEEE 754 float в 4 байта big-endian
void  packFloat32(float value, quint8 *buf);
float unpackFloat32(const quint8 *buf);

// Масштабирование double → int32 (×1000) → 4 байта big-endian
// Точность: 0.001 единицы
void   packScaledInt32(double value, quint8 *buf);
double unpackScaledInt32(const quint8 *buf);

// Сборка кадра RTU: [addr][func][data][CRC_lo][CRC_hi]
QByteArray buildRequestFrame(quint8 slaveAddr, quint8 funcCode,
                              const QByteArray &data);

// Разбор кадра с проверкой CRC
// Возвращает false при ошибке CRC или слишком коротком кадре
bool parseResponseFrame(const QByteArray &frame,
                        quint8 &addr, quint8 &func, QByteArray &payload);
}
```

**Формат CRC в кадре:** little-endian в конце (стандарт RTU).  
`buildRequestFrame` помещает `CRC_lo` перед `CRC_hi`.

---

### 5.2 IModbusTransport

**Файл:** `include/modbus/emulated_modbus_transport.h`

Абстрактный интерфейс транспортного уровня.

```cpp
class IModbusTransport : public QObject {
public:
    virtual void sendFrame(const QByteArray &frame) = 0;
    virtual bool receiveFrame(QByteArray &out, int timeoutMs = 500) = 0;
};
```

Две реализации: `EmulatedModbusTransport` (тесты) и `SerialModbusTransport` (железо).

---

### 5.3 EmulatedModbusTransport

**Файл:** `src/modbus/emulated_modbus_transport.cpp`

Соединяет мастер и ведомый напрямую через `ModbusSlaveAdapter` (in-process).

```cpp
EmulatedModbusTransport(ModbusSlaveAdapter *slave, QObject *parent = nullptr);

void sendFrame(const QByteArray &frame) override;
// → slave->receiveFrame(frame)

bool receiveFrame(QByteArray &out, int timeoutMs = 500) override;
// Ждёт ответа с шагом 2 мс: slave->takeResponseFrame(out)
```

---

### 5.4 SerialModbusTransport

**Файл:** `include/modbus/serial_modbus_transport.h`, `src/modbus/serial_modbus_transport.cpp`

Транспорт на базе `QSerialPort`. Компилируется только если `HAVE_SERIAL_PORT` определён (Qt6SerialPort найден).

```cpp
SerialModbusTransport(const QString &portName,
                      qint32 baudRate = 115200,
                      QObject *parent = nullptr);

bool open();    // Открыть порт (8N1, без управления потоком)
void close();
bool isOpen() const;

void sendFrame(const QByteArray &frame) override;
bool receiveFrame(QByteArray &out, int timeoutMs = 500) override;
// Накапливает байты пока size >= 4 или не истёк timeout
```

**Настройки порта:** 8 бит, без чётности, 1 стоп-бит, без управления потоком.

---

### 5.5 ModbusSlaveAdapter

**Файл:** `include/modbus/modbus_slave_adapter.h`, `src/modbus/modbus_slave_adapter.cpp`

Ведомое устройство Modbus. Работает в отдельном `QThread`.

#### Конструктор
```cpp
ModbusSlaveAdapter(int slaveAddr = 1, QObject *parent = nullptr);
```

#### Методы

```cpp
void setMCU(VirtualMCU *mcu);
// Устанавливает ссылку на МКУ для передачи декодированных команд.

void updateTelemetry(const MCUTelemetry &tele);
// Вызывается из VirtualMCU (другой поток).
// Атомарно обновляет кэш телеметрии под мьютексом.

void receiveFrame(const QByteArray &frame);
// Помещает входящий кадр в очередь запросов.
// Вызывается из EmulatedModbusTransport.

bool takeResponseFrame(QByteArray &out);
// Забирает следующий ответный кадр (если есть).
```

#### Поддерживаемые функции Modbus

**0x10 — Write Multiple Registers (команда к МКУ):**
- Стартовый регистр: 0, количество: 32
- Декодирует `MCUCommand` из 32 регистров × 2 байта
- Передаёт в `VirtualMCU::enqueueCommand()`
- Отвечает эхом стартового регистра и количества

**0x03 — Read Holding Registers (чтение телеметрии):**
- Стартовый регистр: 100, количество: 33
- Кодирует `MCUTelemetry` в 33 регистра × 2 байта
- Возвращает 66-байтный блок данных

#### Карта регистров ответа 0x03 (полная — в разделе 10)

---

### 5.6 ModbusMasterAdapter

**Файл:** `include/modbus/modbus_master_adapter.h`, `src/modbus/modbus_master_adapter.cpp`

Ведущее устройство Modbus с watchdog и автоопросом.

#### Конструктор
```cpp
explicit ModbusMasterAdapter(IModbusTransport *transport,
                              QList<int> slaveIds = {1},
                              QObject *parent = nullptr);
```

#### Методы

```cpp
void start();
// Создаёт рабочий поток и таймер опроса (POLL_INTERVAL_MS = 50 мс).
// Таймер перемещается в рабочий поток, сам объект остаётся в GUI-потоке.

void stop();
// Останавливает таймер (BlockingQueuedConnection) и ждёт завершения потока.

void sendCommand(const MCUCommand &cmd);
// Потокобезопасно (m_transport_mutex).
// MAX_RETRIES = 3 попытки, 500 мс timeout каждая.
// Бросает std::runtime_error при неудаче.

MCUTelemetry readTelemetry(int slaveId);
// Потокобезопасно. Запрос 0x03 → ответ → decodeTelemetry().
// MAX_RETRIES = 3, бросает std::runtime_error при неудаче.
```

#### Сигналы

```cpp
void telemetryReady(const MCUTelemetry &tele);
// Испускается каждые 50 мс для каждого slaveId.
// Используется MainWindow::onTelemetryReceived (Qt::QueuedConnection).

void connectionLost(int slaveId);
// После WATCHDOG_MAX_FAIL = 3 последовательных ошибок.

void connectionRestored(int slaveId);
// При первом успехе после потери связи.
```

#### Watchdog

```
failCount[id]++ при каждой ошибке readTelemetry()
failCount[id] = 0 при успехе
если failCount[id] >= 3 → emit connectionLost(id)
```

---

## 6. HighLevelController

**Файл:** `include/core/high_level_controller.h`, `src/core/high_level_controller.cpp`

Фасад над `ModbusMasterAdapter`. GUI вызывает только его методы.
Формирует `MCUCommand` и вызывает `sendCommand()`.

#### Конструктор
```cpp
explicit HighLevelController(ModbusMasterAdapter *master,
                              int defaultSlave = 1);
```

#### API методов

```cpp
void start(BreakInMode mode, double duration, double target,
           const CriticalLimits &limits,
           double warmup = 5.0, int slaveId = -1);
// Формирует команду "start" с полным набором параметров.
// target: об/мин для COLD/HOT_NOLOAD, Н·м для HOT_LOAD

void stop(int slaveId = -1);
void emergencyStop(int slaveId = -1);
void nextStage(int slaveId = -1);
void resetEmergency(int slaveId = -1);

void setThrottleManual(double value, int slaveId = -1);
// value: 0..100 %

void setThrottleAuto(int slaveId = -1);
// Возвращает управление ПИ-регулятору

void injectFault(int sensorIdx, SensorFaultType type, int slaveId = -1);
void clearFault(int sensorIdx, int slaveId = -1);
```

`slaveId = -1` → используется `defaultSlave` из конструктора.

---

## 7. Хранение данных

### 7.1 DataLogger

**Файл:** `include/core/data_logger.h`, `src/core/data_logger.cpp`

Запись телеметрии в CSV-файл.

```cpp
explicit DataLogger(const QString &baseDir = "logs");

bool startSession(const QString &filename, SessionInfo &sessionInfo);
// Открывает файл, пишет заголовок CSV.
// Заполняет sessionInfo.csv_filename абсолютным путём.

void log(const MCUTelemetry &tele);
// Добавляет одну строку: timestamp,rpm,torque,temp,oil_p,fuel_p,boost,
//                        dyno_temp,res_temp,oil_lvl,fuel_lvl
// Каждые 50 вызовов — flush() для защиты от потери данных.

void close();
// flush() + close файла.

bool isOpen() const;
```

**Формат CSV-строки:**
```
1234.567,1500.3,45.2,82.1,2.45,3.21,0.12,35.1,28.4,98.5,76.3
```

---

### 7.2 Reporter

**Файл:** `include/core/reporter.h`, `src/core/reporter.cpp`

Генерация самодостаточного HTML-отчёта с встроенными графиками.

```cpp
bool generate(const SessionInfo &info,
              const QVector<double> &timestamps,
              const QVector<SensorData> &data,
              const QString &outPath);
// Создаёт HTML-файл с:
//   - таблицей общей информации (оператор, режим, время, итог)
//   - таблицей уставок и критических лимитов
//   - сводной таблицей фактических параметров
//   - 6 inline PNG-графиков (base64)
```

**Графики:**
1. Обороты двигателя (об/мин)
2. Крутящий момент (Н·м)
3. Температура ДВС (°C)
4. Давление масла (бар)
5. Температура ЭД (°C)
6. Температура резистора (°C)

**Зависимость:** QCustomPlot 2.x для рендеринга графиков в `QPixmap` → PNG → base64.

```cpp
// Приватный метод рендеринга одного графика
QString renderChartPng(const QVector<double> &time,
                       const QVector<double> &values,
                       const QString &yLabel,
                       double yMin, double yMax) const;
// Возвращает base64-строку PNG 800×300 пикселей
```

---

### 7.3 SessionHistory

**Файл:** `include/core/session_history.h`, `src/core/session_history.cpp`

Индекс всех завершённых сессий в `logs/session_index.json`.

```cpp
explicit SessionHistory(const QString &baseDir = "logs");
// Создаёт папку и файл индекса если не существуют.

void addEntry(const SessionInfo &session, const QString &reportPath);
// Добавляет JSON-запись в массив и перезаписывает файл.

QJsonArray loadAll() const;
// Загружает и возвращает весь массив сессий.
```

**Формат JSON-записи:**
```json
{
  "start_time": 1715000000.0,
  "end_time":   1715000120.5,
  "operator":   "Иванов И.И.",
  "mode":       "Горячая под нагрузкой",
  "duration":   60.0,
  "target":     150.0,
  "final_state": 5,
  "csv_file":   "logs/20260509_192345.csv",
  "report_file": "logs/report_20260509_192345.html",
  "limits": { "max_engine_temp": 95.0, ... }
}
```

---

## 8. GUI

### 8.1 MainWindow

**Файл:** `include/gui/main_window.h`, `src/gui/main_window.cpp`

Главное окно приложения. Компоновка:

```
┌─────────────────────────────────────────────────────┐
│ [Управление QGroupBox] │ [Критические уставки]       │
├────────────────────────┴────────────────────────────-┤
│ [6 графиков QCustomPlot в QGridLayout 2×3]           │
├──────────────────────────────────────────────────────┤
│ [Текущие значения QLabel] │ [Журнал событий QTextEdit]│
└──────────────────────────────────────────────────────┘
```

#### Конструктор
```cpp
MainWindow(HighLevelController *controller,
           ModbusMasterAdapter *master,
           DataLogger          *logger,
           SessionHistory      *history,
           QWidget             *parent = nullptr);
```
Не владеет переданными объектами (не удаляет их).

#### Публичные слоты

```cpp
void onTelemetryReceived(const MCUTelemetry &tele);
// Вызывается каждые 50 мс из рабочего потока мастера (QueuedConnection).
// Обновляет: лейблы, графики, журнал, CSV-лог, проверяет завершение сессии.

void onConnectionLost(int slaveId);
// Меняет статусную строку на "Нет связи" (красным).

void onConnectionRestored(int slaveId);
// Меняет статусную строку на "Связь OK" (зелёным).
```

#### Приватные слоты (кнопки GUI)

| Слот | Действие |
|---|---|
| `onStartClicked()` | Запрос имени оператора, открытие CSV-сессии, start() |
| `onStopClicked()` | stop() |
| `onEmergencyClicked()` | emergencyStop() |
| `onNextStageClicked()` | nextStage() |
| `onResetEmergencyClicked()` | resetEmergency() |
| `onThrottleChanged(int)` | setThrottleManual() если включён ручной режим |
| `onManualThrottleToggled(bool)` | Переключение авто/ручной режим дросселя |
| `onOpenActuators()` | Открытие ActuatorWindow с подключённым сигналом телеметрии |
| `onOpenHistory()` | Открытие HistoryWindow |
| `onOpenFaultInjection()` | Открытие FaultInjectionDialog |

#### Управление графиками

```cpp
// Буферы данных (MAX_PLOT_POINTS = 3000 точек → 60 с при 50 мс)
QVector<double> m_plotTime, m_plotRpmData, ...;

// При onTelemetryReceived: новые точки добавляются, старые удаляются
// QCustomPlot::rpQueuedReplot — перерисовка асинхронно (без блокировки GUI)
```

#### Завершение сессии

```cpp
void finishSession(const MCUTelemetry &last);
// Вызывается автоматически при STOPPED или EMERGENCY.
// 1. logger->close()
// 2. reporter->generate()
// 3. history->addEntry()
// 4. appendLog("Сессия завершена. Отчёт: ...")
```

---

### 8.2 ActuatorWindow

**Файл:** `include/gui/actuator_window.h`, `src/gui/actuator_window.cpp`

Диалог мониторинга исполнительных механизмов. Обновляется в реальном времени.

```cpp
explicit ActuatorWindow(HighLevelController *ctrl, QWidget *parent = nullptr);

// Слот — подключается к ModbusMasterAdapter::telemetryReady
void onTelemetryReceived(const MCUTelemetry &tele);
```

**Отображаемые поля:**
- Фактический дроссель (%)
- Фактическая скорость ЭД (об/мин)
- Фактический момент ЭД (Н·м)
- Режим ЭД (Прокрутка / Торможение)
- Состояние вентиляторов ДВС/ЭД/резистора (ВКЛ зелёным / ВЫКЛ серым)

**Подключение при открытии (в MainWindow::onOpenActuators):**
```cpp
connect(m_master, &ModbusMasterAdapter::telemetryReady,
        dlg,      &ActuatorWindow::onTelemetryReceived,
        Qt::QueuedConnection);
dlg->onTelemetryReceived(m_lastTelemetry); // сразу показать последние данные
```

---

### 8.3 HistoryWindow

**Файл:** `include/gui/history_window.h`, `src/gui/history_window.cpp`

Диалог просмотра истории испытаний.

```cpp
HistoryWindow(SessionHistory *history, QWidget *parent = nullptr);
```

**QTableWidget** с колонками: Дата начала, Оператор, Режим, Длительность, Итог, Отчёт.

```cpp
// Слот — двойной клик → открывает HTML-отчёт в браузере
void onOpenReport(int row, int col);
// QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))
// Обязательно absoluteFilePath() — иначе gio возвращает ошибку!
```

---

### 8.4 FaultInjectionDialog

**Файл:** `include/gui/fault_injection_dialog.h`, `src/gui/fault_injection_dialog.cpp`

Диалог внедрения тестовых отказов в датчики.

```cpp
FaultInjectionDialog(HighLevelController *ctrl, QWidget *parent = nullptr);
```

**QComboBox датчиков:** 0–Обороты, 1–Момент, 2–Температура ДВС,
3–Давления, 4–Температура ЭД, 5–Температура резистора.

**QComboBox типов:** Нет, Обрыв, КЗ, Уход диапазона.

```cpp
void onInject(); // → ctrl->injectFault(idx, type)
void onClear();  // → ctrl->clearFault(idx)
```

---

## 9. main.cpp

Точка входа. Порядок создания и связывания компонентов:

```cpp
// 1. Регистрация мета-типов (ДО создания потоков!)
registerMetaTypes();

// 2. Виртуальный МКУ
auto *mcu = new VirtualMCU(nullptr, 1, 0.02);

// 3. Ведомый Modbus
auto *slave = new ModbusSlaveAdapter(1);
slave->setMCU(mcu);
mcu->setSlaveAdapter(slave);

// 4. Транспорт (эмулированный или реальный)
auto *transport = new EmulatedModbusTransport(slave);

// 5. Ведущий Modbus
auto *master = new ModbusMasterAdapter(transport, {1});

// 6. Контроллер верхнего уровня
auto *controller = new HighLevelController(master, 1);

// 7. Хранение данных
auto *logger  = new DataLogger("logs");
auto *history = new SessionHistory("logs");

// 8. Запуск потоков (порядок важен!)
slave->start();   // сначала slave
mcu->start();     // потом MCU (slave уже готов принять телеметрию)
master->start();  // мастер начинает опрашивать

// 9. GUI
MainWindow win(controller, master, logger, history);
win.show();

// 10. Корректное завершение
master->stop();
mcu->stopMCU();
slave->stopAdapter();
mcu->wait(3000);
slave->wait(3000);
```

---

## 10. Протокол Modbus — карта регистров

### Команда 0x10 (Write Multiple Registers)
Стартовый адрес: 0, Количество: 32 регистра (64 байта)

| Регистры | Поле | Тип | Масштаб |
|---|---|---|---|
| 0 | command_id | uint16 | 1=start, 2=stop, 3=emergency_stop, 4=next_stage, 5=reset_emergency, 6=set_throttle, 7=set_mode, 8=inject_fault, 9=clear_fault |
| 1 | mode | uint16 | BreakInMode |
| 2–3 | duration_sec | int32 | ×1000 |
| 4–5 | warmup_duration_sec | int32 | ×1000 |
| 6–7 | target_rpm | int32 | ×1000 |
| 8–9 | target_torque | int32 | ×1000 |
| 10–11 | max_engine_temp | int32 | ×1000 |
| 12–13 | min_oil_pressure | int32 | ×1000 |
| 14–15 | max_oil_pressure | int32 | ×1000 |
| 16–17 | min_fuel_pressure | int32 | ×1000 |
| 18–19 | max_fuel_pressure | int32 | ×1000 |
| 20–21 | max_boost_pressure | int32 | ×1000 |
| 22–23 | max_engine_rpm | int32 | ×1000 |
| 24–25 | max_dyno_motor_temp | int32 | ×1000 |
| 26–27 | max_resistor_temp | int32 | ×1000 |
| 28 | throttle_value | uint16 | ×10 (0..1000 → 0..100%) |
| 29 | manual_throttle | uint16 | 0/1 |
| 30 | fault_sensor_idx | uint16 | 0..5 |
| 31 | fault_type | uint16 | SensorFaultType |

### Телеметрия 0x03 (Read Holding Registers)
Стартовый адрес: 100, Количество: 33 регистра (66 байт)

| Регистры | Поле | Тип | Масштаб |
|---|---|---|---|
| 100–101 | timestamp | int32 | ×1000 (с) |
| 102–103 | engine_rpm | int32 | ×1000 |
| 104–105 | torque | int32 | ×1000 |
| 106–107 | engine_temp | int32 | ×1000 |
| 108–109 | oil_pressure | int32 | ×1000 |
| 110–111 | fuel_pressure | int32 | ×1000 |
| 112–113 | boost_pressure | int32 | ×1000 |
| 114–115 | dyno_motor_temp | int32 | ×1000 |
| 116–117 | resistor_temp | int32 | ×1000 |
| 118–119 | oil_level | int32 | ×1000 |
| 120–121 | fuel_level | int32 | ×1000 |
| 122 | fault_mask | uint16 | битовая маска |
| 123 | state | uint16 | BreakInState |
| 124 | mode | uint16 | BreakInMode |
| 125 | throttle_pct | uint16 | ×10 (0..1000) |
| 126 | dyno_mode | uint16 | 0=SPIN, 1=BRAKE |
| 127–128 | dyno_speed_or_torque | int32 | ×1000 |
| 129 | engine_fan | uint16 | 0/1 |
| 130 | dyno_fan | uint16 | 0/1 |
| 131 | resistor_fan | uint16 | 0/1 |
| 132 | error_mask | uint16 | биты ошибок |

---

## 11. Потоки и синхронизация

### Схема потоков

| Поток | Объект | Период | Защита |
|---|---|---|---|
| GUI (main) | MainWindow, HighLevelController | По событиям | Qt event loop |
| Worker | ModbusMasterAdapter (таймер) | 50 мс | m_transport_mutex |
| Slave | ModbusSlaveAdapter | 5 мс (опрос очереди) | m_req_mutex, m_resp_mutex, m_tele_mutex |
| MCU | VirtualMCU | 20 мс | m_cmd_mutex |

### Межпоточные взаимодействия

```
VirtualMCU → ModbusSlaveAdapter::updateTelemetry()
    защита: m_tele_mutex (mutable — вызов из const-метода buildTelemetryRegisters)

ModbusSlaveAdapter → VirtualMCU::enqueueCommand()
    защита: m_cmd_mutex в VirtualMCU

ModbusMasterAdapter → IModbusTransport::sendFrame/receiveFrame
    защита: m_transport_mutex (sendCommand и readTelemetry могут вызываться из разных потоков)

ModbusMasterAdapter → GUI: emit telemetryReady(MCUTelemetry)
    тип соединения: Qt::QueuedConnection (автоматически — разные потоки)
    требование: MCUTelemetry зарегистрирован через qRegisterMetaType
```

### Завершение работы (порядок важен)

```
1. master->stop()        — ждём завершения таймера и потока
2. mcu->stopMCU()        — устанавливаем флаг + requestInterruption()
3. slave->stopAdapter()  — устанавливаем флаг + requestInterruption()
4. mcu->wait(3000)       — ждём завершения потока MCU
5. slave->wait(3000)     — ждём завершения потока slave
6. delete ...            — освобождаем память
```

---

## 12. История коммитов

Рекомендуемая разбивка на коммиты для Git:

```
commit 1: chore: project scaffold
  - CMakeLists.txt
  - структура папок include/ src/ third_party/
  - .gitignore, README.md, third_party/README.md

commit 2: feat(core): data types and enumerations
  - include/core/datatypes.h
  - src/core/datatypes.cpp
  - все перечисления, структуры, registerMetaTypes()

commit 3: feat(core): engine physics model
  - include/core/engine_model.h
  - src/core/engine_model.cpp
  - численное интегрирование: обороты, тепло, давления, уровни

commit 4: feat(core): actuator driver with inertia
  - include/core/actuator_driver.h
  - src/core/actuator_driver.cpp
  - дроссель, ЭД, вентиляторы с задержками

commit 5: feat(core): sensor simulator with fault injection
  - include/core/sensor_simulator.h
  - src/core/sensor_simulator.cpp
  - гауссов шум, 4 типа отказов, fault_mask

commit 6: feat(core): PI controller
  - include/core/pi_controller.h
  - src/core/pi_controller.cpp
  - anti-windup, авторегулятор дросселя

commit 7: feat(core): virtual MCU state machine
  - include/core/virtual_mcu.h
  - src/core/virtual_mcu.cpp
  - конечный автомат, главный цикл, checkLimits

commit 8: feat(modbus): protocol utilities
  - include/modbus/modbus_utils.h
  - src/modbus/modbus_utils.cpp
  - CRC-16, pack/unpack, buildRequestFrame, parseResponseFrame

commit 9: feat(modbus): transport layer
  - include/modbus/emulated_modbus_transport.h
  - src/modbus/emulated_modbus_transport.cpp
  - include/modbus/serial_modbus_transport.h
  - src/modbus/serial_modbus_transport.cpp
  - IModbusTransport интерфейс

commit 10: feat(modbus): slave adapter
  - include/modbus/modbus_slave_adapter.h
  - src/modbus/modbus_slave_adapter.cpp
  - обработка 0x03 и 0x10, кодирование телеметрии

commit 11: feat(modbus): master adapter with watchdog
  - include/modbus/modbus_master_adapter.h
  - src/modbus/modbus_master_adapter.cpp
  - опрос 50 мс, retries, connectionLost/Restored

commit 12: feat(core): high level controller
  - include/core/high_level_controller.h
  - src/core/high_level_controller.cpp
  - API фасад для GUI

commit 13: feat(storage): data logger and session history
  - include/core/data_logger.h + src/core/data_logger.cpp
  - include/core/session_history.h + src/core/session_history.cpp
  - CSV-логирование, JSON-индекс сессий

commit 14: feat(storage): HTML report generator
  - include/core/reporter.h
  - src/core/reporter.cpp
  - QCustomPlot inline PNG, self-contained HTML

commit 15: feat(gui): main window
  - include/gui/main_window.h
  - src/gui/main_window.cpp
  - панель управления, графики, показания, журнал

commit 16: feat(gui): auxiliary dialogs
  - include/gui/actuator_window.h + src/gui/actuator_window.cpp
  - include/gui/history_window.h + src/gui/history_window.cpp
  - include/gui/fault_injection_dialog.h + src/gui/fault_injection_dialog.cpp

commit 17: feat: application entry point
  - src/main.cpp
  - порядок инициализации, корректное завершение

commit 18: fix: MOC headers in CMakeLists, metatype registration
  - CMakeLists.txt: добавление MOC_HEADERS в add_executable
  - datatypes.h: Q_DECLARE_METATYPE + registerMetaTypes()
  - исправление vtable ошибок компоновщика

commit 19: fix: Qt macro conflicts and thread safety
  - actuator_driver.h: signals() → actuatorSignals()
  - modbus_slave_adapter.h: mutable QMutex
  - modbus_master_adapter: убран moveToThread(this), добавлен m_transport_mutex

commit 20: fix: report opening with absolute paths
  - history_window.cpp: QFileInfo::absoluteFilePath()
  - устранение ошибки gio с относительными путями

commit 21: feat(gui): actuator window live telemetry
  - actuator_window.h/cpp: слот onTelemetryReceived
  - main_window.cpp: connect при открытии окна

commit 22: docs: specification and commit history
  - SPECIFICATION.md
  - закомментированные исходники
```

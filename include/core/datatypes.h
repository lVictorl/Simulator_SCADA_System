#pragma once
/**
 * @file datatypes.h
 * @brief Центральное хранилище всех типов данных SCADA-системы.
 *
 * Принципы проектирования:
 *  - Plain Old Data (POD) — нет виртуальных функций, нет наследования.
 *  - Фиксированные размеры полей — совместимость с Modbus RTU (16/32 бит).
 *  - Q_DECLARE_METATYPE — регистрация для передачи через Qt-сигналы
 *    между потоками (Qt::QueuedConnection).
 */

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <cmath>
#include <limits>

// ============================================================================
//  ПЕРЕЧИСЛЕНИЯ (тип-основа quint16 → совместимость с Modbus-регистрами)
// ============================================================================

/**
 * @brief Режим обкатки. Кодируется в регистр 1 команды Modbus 0x10.
 *
 * COLD:       Двигатель ВЫКЛ, внешний ЭД прокручивает коленвал
 * HOT_NOLOAD: Двигатель ВКЛ, прогрев + работа на холостых
 * HOT_LOAD:   Двигатель ВКЛ, прогрев + ЭД создаёт тормозную нагрузку
 */
enum class BreakInMode : quint16 {
    COLD       = 0,  ///< Холодная обкатка
    HOT_NOLOAD = 1,  ///< Горячая без нагрузки
    HOT_LOAD   = 2   ///< Горячая под нагрузкой
};
/// Возвращает русскоязычное название режима.
QString breakInModeDisplayName(BreakInMode mode);

/**
 * @brief Состояние конечного автомата VirtualMCU.
 *
 * Диаграмма переходов:
 *   IDLE → COLD_BREAKIN → STOPPED
 *   IDLE → WARMUP → HOT_NOLOAD → STOPPED
 *   IDLE → WARMUP → HOT_LOAD   → STOPPED
 *   любое → EMERGENCY → IDLE (по reset)
 */
enum class BreakInState : quint16 {
    IDLE         = 0,  ///< Ожидание команды
    COLD_BREAKIN = 1,  ///< Активная холодная обкатка
    WARMUP       = 2,  ///< Прогрев двигателя
    HOT_NOLOAD   = 3,  ///< Горячая без нагрузки
    HOT_LOAD     = 4,  ///< Горячая с нагрузкой
    STOPPED      = 5,  ///< Нормальное завершение
    EMERGENCY    = 6   ///< Аварийный останов
};
/// Возвращает русскоязычное название состояния.
QString breakInStateDisplayName(BreakInState state);

/**
 * @brief Режим работы электродвигателя стенда.
 *   SPIN:  ЭД вращает коленвал (прокрутка)
 *   BRAKE: ЭД тормозит вал (нагрузка)
 */
enum class DynoMotorMode : quint16 {
    SPIN  = 0,  ///< Прокрутка
    BRAKE = 1   ///< Торможение/нагрузка
};

/**
 * @brief Типы моделируемых отказов датчиков.
 *   OPEN_CIRCUIT:  обрыв провода → NaN
 *   SHORT_CIRCUIT: КЗ на питание → 1e6 (зашкал)
 *   OUT_OF_RANGE:  дрейф нуля   → trueValue * 10 + 500
 */
enum class SensorFaultType : quint16 {
    NONE          = 0,
    OPEN_CIRCUIT  = 1,
    SHORT_CIRCUIT = 2,
    OUT_OF_RANGE  = 3
};

// ============================================================================
//  СТРУКТУРЫ ДАННЫХ
// ============================================================================

/**
 * @brief Пороговые значения аварийного останова.
 * Задаются оператором в GUI, передаются в MCU командой "start".
 */
struct CriticalLimits {
    double max_engine_temp     = 95.0;   ///< °C
    double min_oil_pressure    = 0.8;    ///< бар
    double max_oil_pressure    = 6.0;    ///< бар
    double min_fuel_pressure   = 2.0;    ///< бар
    double max_fuel_pressure   = 5.0;    ///< бар
    double max_boost_pressure  = 2.5;    ///< бар
    double max_engine_rpm      = 4000.0; ///< об/мин
    double max_dyno_motor_temp = 150.0;  ///< °C
    double max_resistor_temp   = 200.0;  ///< °C
};

/**
 * @brief Мгновенный снимок показаний всех датчиков.
 * Заполняется SensorSimulator::read() каждые 20 мс.
 *
 * fault_mask — битовая маска (биты 0–5):
 *   0=rpm, 1=torque, 2=engine_temp, 3=pressures, 4=dyno_temp, 5=resistor_temp
 */
struct SensorData {
    double  timestamp       = 0.0;   ///< Монотонное время, с
    double  engine_rpm      = 0.0;   ///< об/мин
    double  torque          = 0.0;   ///< Н·м
    double  engine_temp     = 20.0;  ///< °C
    double  oil_pressure    = 1.5;   ///< бар
    double  fuel_pressure   = 3.0;   ///< бар
    double  boost_pressure  = 0.0;   ///< бар
    double  dyno_motor_temp = 20.0;  ///< °C
    double  resistor_temp   = 20.0;  ///< °C
    double  oil_level       = 100.0; ///< %
    double  fuel_level      = 100.0; ///< %
    quint16 fault_mask      = 0;     ///< Битовая маска активных отказов
};

/**
 * @brief Логические уставки — "что хотим получить".
 * Передаются в ActuatorDriver::applySetpoints().
 * Двигатель получает фактические значения из ActuatorFeedback (с инерционностью).
 */
struct ActuatorSetpoints {
    double        throttle       = 0.0;                ///< 0..100 %
    double        target_speed   = 0.0;                ///< об/мин (режим SPIN)
    double        target_torque  = 0.0;                ///< Н·м (режим BRAKE)
    DynoMotorMode dyno_mode      = DynoMotorMode::SPIN;
    bool          engine_running = false;
    bool          engine_fan     = false;
    bool          dyno_motor_fan = false;
    bool          resistor_fan   = false;
};

/**
 * @brief Физические сигналы на оборудование — выход ActuatorDriver.
 *   throttle_pwm_duty:   12-бит ШИМ (0..4095) → сервопривод дросселя
 *   dyno_speed_voltage:  0..10 В → частотный преобразователь ЭД
 *   dyno_torque_current: 4..20 мА → задание момента нагрузки
 */
struct ActuatorSignals {
    int           throttle_pwm_duty   = 0;
    DynoMotorMode dyno_mode           = DynoMotorMode::SPIN;
    double        dyno_speed_voltage  = 0.0;   ///< 0..10 В
    double        dyno_torque_current = 4.0;   ///< 4..20 мА
    bool          engine_fan_on       = false;
    bool          dyno_motor_fan_on   = false;
    bool          resistor_fan_on     = false;
};

/**
 * @brief Фактические значения обратной связи — "что реально произошло".
 * С учётом инерционности ActuatorDriver.
 * Используется как вход EngineModel::step() для корректной физики.
 */
struct ActuatorFeedback {
    double throttle_actual       = 0.0;
    double dyno_speed_actual     = 0.0;
    double dyno_torque_actual    = 0.0;
    bool   engine_fan_actual     = false;
    bool   dyno_motor_fan_actual = false;
    bool   resistor_fan_actual   = false;
};

/**
 * @brief Команда от GUI к виртуальному МКУ.
 * Передаётся через Modbus 0x10 (Write Multiple Registers, адрес 0, 32 регистра).
 *
 * Коды команд (command_id → uint16 в регистре 0):
 *   "start"=1, "stop"=2, "emergency_stop"=3, "next_stage"=4,
 *   "reset_emergency"=5, "set_throttle"=6, "set_mode"=7,
 *   "inject_fault"=8, "clear_fault"=9
 */
struct MCUCommand {
    QString         command_id;
    int             slave_id           = 1;
    BreakInMode     mode               = BreakInMode::COLD;
    double          duration_sec       = 0.0;
    double          warmup_duration_sec= 0.0;
    double          target_rpm         = 0.0;
    double          target_torque      = 0.0;
    CriticalLimits  limits;
    double          throttle_value     = 0.0;  ///< Для set_throttle, 0..100 %
    bool            manual_throttle    = false;
    int             fault_sensor_idx   = 0;    ///< 0..5 для inject/clear fault
    SensorFaultType fault_type         = SensorFaultType::NONE;
};

/**
 * @brief Полный пакет телеметрии от МКУ к GUI.
 * Передаётся через Modbus 0x03 каждые 50 мс.
 * Содержит всё необходимое для обновления GUI одним запросом.
 */
struct MCUTelemetry {
    SensorData       sensors;
    BreakInState     state                = BreakInState::IDLE;
    BreakInMode      mode                 = BreakInMode::COLD;
    double           throttle_pct         = 0.0;
    DynoMotorMode    dyno_mode            = DynoMotorMode::SPIN;
    double           dyno_speed_or_torque = 0.0; ///< об/мин или Н·м в зависимости от режима
    bool             engine_fan           = false;
    bool             dyno_fan             = false;
    bool             resistor_fan         = false;
    QStringList      errors;    ///< Активные ошибки → EMERGENCY
    QStringList      warnings;  ///< Предупреждения → только в журнал
    int              slave_id             = 1;
    ActuatorFeedback actuator_feedback;
};

/**
 * @brief Метаданные завершённой сессии испытания.
 * Сохраняется в logs/session_index.json через SessionHistory::addEntry().
 */
struct SessionInfo {
    QString        operator_name;
    BreakInMode    mode            = BreakInMode::COLD;
    double         duration        = 0.0;
    double         warmup_duration = 0.0;
    double         target          = 0.0;
    CriticalLimits limits;
    double         start_time      = 0.0;  ///< Unix timestamp
    double         end_time        = 0.0;  ///< Unix timestamp
    BreakInState   final_state     = BreakInState::IDLE;
    QString        csv_filename;   ///< Абсолютный путь к CSV
    QString        report_file;    ///< Абсолютный путь к HTML-отчёту
};

// ============================================================================
//  РЕГИСТРАЦИЯ МЕТ-ТИПОВ ДЛЯ Qt
//
//  Необходима для:
//  1. Qt::QueuedConnection — сигналы между потоками
//  2. QVariant::fromValue() — хранение в QVariant (QComboBox itemData)
//
//  Q_DECLARE_METATYPE — статическое объявление (в заголовке)
//  qRegisterMetaType  — динамическая регистрация (один раз в main)
// ============================================================================
Q_DECLARE_METATYPE(BreakInMode)
Q_DECLARE_METATYPE(BreakInState)
Q_DECLARE_METATYPE(DynoMotorMode)
Q_DECLARE_METATYPE(SensorFaultType)
Q_DECLARE_METATYPE(SensorData)
Q_DECLARE_METATYPE(MCUTelemetry)
Q_DECLARE_METATYPE(ActuatorFeedback)

/**
 * @brief Регистрирует все пользовательские типы в мета-системе Qt.
 * Вызывать ОДИН РАЗ из main() ДО создания любых потоков и QApplication::exec().
 */
inline void registerMetaTypes()
{
    qRegisterMetaType<BreakInMode>     ("BreakInMode");
    qRegisterMetaType<BreakInState>    ("BreakInState");
    qRegisterMetaType<DynoMotorMode>   ("DynoMotorMode");
    qRegisterMetaType<SensorFaultType> ("SensorFaultType");
    qRegisterMetaType<SensorData>      ("SensorData");
    qRegisterMetaType<MCUTelemetry>    ("MCUTelemetry");
    qRegisterMetaType<ActuatorFeedback>("ActuatorFeedback");
}

#pragma once
/**
 * datatypes.h — центральное хранилище всех перечислений и структур данных
 * SCADA-системы обкатки дизельного двигателя.
 *
 * Все типы спроектированы как Plain Old Data (struct) с Q_GADGET для интеграции
 * с системой метаобъектов Qt (QMetaEnum, сериализация).
 * Соответствуют промышленному протоколу Modbus RTU — фиксированные размеры
 * полей, однозначные диапазоны.
 */

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QSharedPointer>
#include <cmath>
#include <limits>
#include <optional>

// ============================================================================
// Перечисления
// ============================================================================

/**
 * Режимы обкатки. Кодируются uint16 в Modbus (регистр 1 команды 0x10).
 */
enum class BreakInMode : quint16 {
    COLD      = 0,  ///< Холодная обкатка
    HOT_NOLOAD = 1, ///< Горячая без нагрузки
    HOT_LOAD  = 2   ///< Горячая под нагрузкой
};
QString breakInModeDisplayName(BreakInMode mode);

/**
 * Состояния конечного автомата процесса обкатки.
 */
enum class BreakInState : quint16 {
    IDLE        = 0, ///< Ожидание
    COLD_BREAKIN = 1,///< Холодная обкатка
    WARMUP      = 2, ///< Прогрев
    HOT_NOLOAD  = 3, ///< Горячая без нагрузки
    HOT_LOAD    = 4, ///< Горячая с нагрузкой
    STOPPED     = 5, ///< Нормальное завершение
    EMERGENCY   = 6  ///< Аварийный останов
};
QString breakInStateDisplayName(BreakInState state);

/**
 * Режимы работы электродвигателя стенда.
 */
enum class DynoMotorMode : quint16 {
    SPIN  = 0, ///< Прокрутка внешним мотором
    BRAKE = 1  ///< Торможение (создание нагрузки)
};

/**
 * Типы моделируемых неисправностей датчиков.
 */
enum class SensorFaultType : quint16 {
    NONE         = 0, ///< Нет неисправности
    OPEN_CIRCUIT = 1, ///< Обрыв → NaN
    SHORT_CIRCUIT = 2,///< КЗ → экстремальное значение
    OUT_OF_RANGE = 3  ///< Уход диапазона
};

// ============================================================================
// Plain-Old-Data структуры
// ============================================================================

/** Критические уставки, настраиваемые оператором. */
struct CriticalLimits {
    double max_engine_temp    = 95.0;  ///< °C
    double min_oil_pressure   =  0.8;  ///< бар
    double max_oil_pressure   =  6.0;  ///< бар
    double min_fuel_pressure  =  2.0;  ///< бар
    double max_fuel_pressure  =  5.0;  ///< бар
    double max_boost_pressure =  2.5;  ///< бар
    double max_engine_rpm     = 4000.0;///< об/мин
    double max_dyno_motor_temp= 150.0; ///< °C
    double max_resistor_temp  = 200.0; ///< °C
};

/** Показания всех датчиков в один момент времени. */
struct SensorData {
    double    timestamp      = 0.0;  ///< монотонное время, с
    double    engine_rpm     = 0.0;  ///< об/мин
    double    torque         = 0.0;  ///< Н·м
    double    engine_temp    = 20.0; ///< °C
    double    oil_pressure   = 1.5;  ///< бар
    double    fuel_pressure  = 3.0;  ///< бар
    double    boost_pressure = 0.0;  ///< бар
    double    dyno_motor_temp= 20.0; ///< °C
    double    resistor_temp  = 20.0; ///< °C
    double    oil_level      = 100.0;///< %
    double    fuel_level     = 100.0;///< %
    quint16   fault_mask     = 0;    ///< битовая маска неисправностей (биты 0–5)
};

/** Логические уставки для исполнительных механизмов. */
struct ActuatorSetpoints {
    double        throttle      = 0.0;                      ///< 0..100 %
    double        target_speed  = 0.0;                      ///< об/мин
    double        target_torque = 0.0;                      ///< Н·м
    DynoMotorMode dyno_mode     = DynoMotorMode::SPIN;
    bool          engine_running= false;
    bool          engine_fan    = false;
    bool          dyno_motor_fan= false;
    bool          resistor_fan  = false;
};

/** Физические сигналы на оборудование. */
struct ActuatorSignals {
    int           throttle_pwm_duty   = 0;    ///< 0..4095
    DynoMotorMode dyno_mode           = DynoMotorMode::SPIN;
    double        dyno_speed_voltage  = 0.0;  ///< 0..10 В
    double        dyno_torque_current = 4.0;  ///< 4..20 мА
    bool          engine_fan_on       = false;
    bool          dyno_motor_fan_on   = false;
    bool          resistor_fan_on     = false;
};

/** Фактические значения от датчиков обратной связи. */
struct ActuatorFeedback {
    double throttle_actual      = 0.0;
    double dyno_speed_actual    = 0.0;
    double dyno_torque_actual   = 0.0;
    bool   engine_fan_actual    = false;
    bool   dyno_motor_fan_actual= false;
    bool   resistor_fan_actual  = false;
};

/** Команда от ПК к микроконтроллеру. */
struct MCUCommand {
    QString       command_id;       ///< "start","stop","emergency_stop","next_stage",
                                    ///  "reset_emergency","set_throttle","set_mode",
                                    ///  "inject_fault","clear_fault"
    int           slave_id         = 1;
    BreakInMode   mode             = BreakInMode::COLD;
    double        duration_sec     = 0.0;
    double        warmup_duration_sec = 0.0;
    double        target_rpm       = 0.0;
    double        target_torque    = 0.0;
    CriticalLimits limits;
    double        throttle_value   = 0.0;
    bool          manual_throttle  = false;
    int           fault_sensor_idx = 0;   ///< 0..5
    SensorFaultType fault_type     = SensorFaultType::NONE;
};

/** Пакет телеметрии от контроллера. */
struct MCUTelemetry {
    SensorData      sensors;
    BreakInState    state            = BreakInState::IDLE;
    BreakInMode     mode             = BreakInMode::COLD;
    double          throttle_pct     = 0.0;
    DynoMotorMode   dyno_mode        = DynoMotorMode::SPIN;
    double          dyno_speed_or_torque = 0.0;
    bool            engine_fan       = false;
    bool            dyno_fan         = false;
    bool            resistor_fan     = false;
    QStringList     errors;
    QStringList     warnings;
    int             slave_id         = 1;
    ActuatorFeedback actuator_feedback;
};

/** Метаинформация об одной сессии испытания. */
struct SessionInfo {
    QString        operator_name;
    BreakInMode    mode             = BreakInMode::COLD;
    double         duration         = 0.0;
    double         warmup_duration  = 0.0;
    double         target           = 0.0;
    CriticalLimits limits;
    double         start_time       = 0.0;
    double         end_time         = 0.0;
    BreakInState   final_state      = BreakInState::IDLE;
    QString        csv_filename;
    QString        report_file;
};

// ── Регистрация мета-типов для Qt-сигналов и QVariant ──────────────────────
Q_DECLARE_METATYPE(BreakInMode)
Q_DECLARE_METATYPE(BreakInState)
Q_DECLARE_METATYPE(DynoMotorMode)
Q_DECLARE_METATYPE(SensorFaultType)
Q_DECLARE_METATYPE(SensorData)
Q_DECLARE_METATYPE(MCUTelemetry)
Q_DECLARE_METATYPE(ActuatorFeedback)

// ── Inline helper — регистрирует все типы одним вызовом из main() ───────────
inline void registerMetaTypes()
{
    qRegisterMetaType<BreakInMode>    ("BreakInMode");
    qRegisterMetaType<BreakInState>   ("BreakInState");
    qRegisterMetaType<DynoMotorMode>  ("DynoMotorMode");
    qRegisterMetaType<SensorFaultType>("SensorFaultType");
    qRegisterMetaType<SensorData>     ("SensorData");
    qRegisterMetaType<MCUTelemetry>   ("MCUTelemetry");
    qRegisterMetaType<ActuatorFeedback>("ActuatorFeedback");
}

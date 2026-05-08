#pragma once
/**
 * engine_model.h — математическая модель дизельного двигателя и испытательного стенда.
 */

#include "datatypes.h"

/**
 * Математическая модель двигателя и стенда.
 *
 * Не содержит логики управления — только численное интегрирование физических уравнений.
 * Метод step() вызывается каждые dt секунд.
 */
class EngineModel
{
public:
    explicit EngineModel(double ambientTemp = 20.0);

    /** Выполняет один шаг интегрирования длительностью dt. */
    void step(double dt, const ActuatorSetpoints &cmd);

    /** Возвращает текущее «истинное» состояние (без шума датчиков). */
    SensorData state() const;

    // Доступ к полям состояния (только чтение)
    double engineRpm()     const { return m_engine_rpm; }
    double torque()        const { return m_torque; }
    double engineTemp()    const { return m_engine_temp; }
    double oilPressure()   const { return m_oil_pressure; }
    double fuelPressure()  const { return m_fuel_pressure; }
    double boostPressure() const { return m_boost_pressure; }
    double dynoMotorTemp() const { return m_dyno_motor_temp; }
    double resistorTemp()  const { return m_resistor_temp; }
    double oilLevel()      const { return m_oil_level; }
    double fuelLevel()     const { return m_fuel_level; }

private:
    double m_ambient;

    // Состояние
    double m_engine_rpm     = 0.0;
    double m_torque         = 0.0;
    double m_engine_temp;
    double m_oil_pressure   = 1.5;
    double m_fuel_pressure  = 3.0;
    double m_boost_pressure = 0.0;
    double m_dyno_motor_temp;
    double m_resistor_temp;
    double m_oil_level      = 100.0;
    double m_fuel_level     = 100.0;

    // Конфигурационные коэффициенты
    double m_inertia_tau        = 0.3;
    double m_friction_coeff     = 0.02;
    double m_engine_torque_gain = 0.6;
    double m_j_inertia          = 0.5;

    double m_heat_from_throttle = 0.015;
    double m_natural_cooling    = 0.005;
    double m_fan_cooling_rate   = 0.03;
    double m_dyno_heat_factor   = 0.0002;

    static double clamp(double v, double lo, double hi) noexcept;
};

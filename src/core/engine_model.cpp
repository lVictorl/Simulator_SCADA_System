#include "engine_model.h"
#include <QtCore/QElapsedTimer>
#include <cmath>
#include <algorithm>

static QElapsedTimer s_timer;
namespace { struct TimerInit { TimerInit() { s_timer.start(); } } s_init; }

EngineModel::EngineModel(double ambientTemp)
    : m_ambient(ambientTemp)
    , m_engine_temp(ambientTemp)
    , m_dyno_motor_temp(ambientTemp)
    , m_resistor_temp(ambientTemp)
{}

double EngineModel::clamp(double v, double lo, double hi) noexcept
{
    return std::max(lo, std::min(hi, v));
}

void EngineModel::step(double dt, const ActuatorSetpoints &cmd)
{
    const double throttle = clamp(cmd.throttle, 0.0, 100.0);

    if (!cmd.engine_running) {
        m_engine_rpm += (cmd.target_speed - m_engine_rpm) * (dt / m_inertia_tau);
        m_torque = m_friction_coeff * m_engine_rpm + 5.0;
    } else {
        const double engine_torque  = throttle * m_engine_torque_gain;
        const double friction_torque= m_engine_rpm * m_friction_coeff;
        const double load_torque    = (cmd.dyno_mode == DynoMotorMode::BRAKE)
                                      ? cmd.target_torque : 0.0;
        const double net_torque     = engine_torque - load_torque - friction_torque;
        m_engine_rpm += net_torque * (dt / m_j_inertia);
        m_torque = (cmd.dyno_mode == DynoMotorMode::BRAKE) ? load_torque : engine_torque;
    }
    if (m_engine_rpm < 0.0) m_engine_rpm = 0.0;

    {
        const double heat    = cmd.engine_running ? throttle * m_heat_from_throttle : 0.0;
        const double cooling = m_natural_cooling * (m_engine_temp - m_ambient);
        const double fan_cool= cmd.engine_fan ? m_fan_cooling_rate * (m_engine_temp - m_ambient) : 0.0;
        m_engine_temp += (heat - cooling - fan_cool) * dt;
    }

    m_oil_pressure = clamp(1.5 + 0.0005 * m_engine_rpm - 0.005 * (m_engine_temp - 20.0), 0.0, 20.0);
    m_fuel_pressure = 3.0 + 0.0001 * m_engine_rpm;
    m_boost_pressure = (cmd.engine_running && m_engine_rpm > 1000.0)
        ? 0.8 * (m_engine_rpm - 1000.0) / 3000.0 * throttle / 100.0 : 0.0;

    m_oil_level  = clamp(m_oil_level  - 0.001 * throttle * dt, 0.0, 100.0);
    m_fuel_level = clamp(m_fuel_level - 0.005 * throttle * dt, 0.0, 100.0);

    {
        const double power_loss = (cmd.engine_running && cmd.dyno_mode == DynoMotorMode::BRAKE)
            ? m_dyno_heat_factor * std::abs(cmd.target_torque) * m_engine_rpm : 0.0;
        const double cool_dyno = m_natural_cooling * (m_dyno_motor_temp - m_ambient)
            + (cmd.dyno_motor_fan ? m_fan_cooling_rate * (m_dyno_motor_temp - m_ambient) : 0.0);
        m_dyno_motor_temp += (power_loss - cool_dyno) * dt;
        const double cool_res = m_natural_cooling * (m_resistor_temp - m_ambient)
            + (cmd.resistor_fan ? m_fan_cooling_rate * (m_resistor_temp - m_ambient) : 0.0);
        m_resistor_temp += (power_loss * 0.9 - cool_res) * dt;
    }
}

SensorData EngineModel::state() const
{
    SensorData s;
    s.timestamp       = s_timer.elapsed() / 1000.0;
    s.engine_rpm      = m_engine_rpm;
    s.torque          = m_torque;
    s.engine_temp     = m_engine_temp;
    s.oil_pressure    = m_oil_pressure;
    s.fuel_pressure   = m_fuel_pressure;
    s.boost_pressure  = m_boost_pressure;
    s.dyno_motor_temp = m_dyno_motor_temp;
    s.resistor_temp   = m_resistor_temp;
    s.oil_level       = m_oil_level;
    s.fuel_level      = m_fuel_level;
    s.fault_mask      = 0;
    return s;
}

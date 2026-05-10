#include "actuator_driver.h"
#include <algorithm>
#include <cmath>

ActuatorDriver::ActuatorDriver(double throttleRate, double dynoAccel,
                                double torqueSlewRate, double fanDelay)
    : m_throttle_rate(throttleRate)
    , m_dyno_accel(dynoAccel)
    , m_torque_slew_rate(torqueSlewRate)
    , m_fan_delay(fanDelay)
{}

double ActuatorDriver::moveToward(double current, double target, double maxStep) noexcept
{
    const double diff = target - current;
    if (std::abs(diff) <= maxStep) return target;
    return current + (diff > 0 ? maxStep : -maxStep);
}

bool ActuatorDriver::updateFanTimer(double &timer, bool &actual, bool cmd, double dt)
{
    if (cmd != actual) {
        timer += dt;
        if (timer >= m_fan_delay) {
            actual = cmd;
            timer  = 0.0;
        }
    } else {
        timer = 0.0;
    }
    return actual;
}

void ActuatorDriver::applySetpoints(const ActuatorSetpoints &sp, double dt)
{
    // ── Дроссель ─────────────────────────────────────────────────────────────
    m_actual_throttle = moveToward(m_actual_throttle, sp.throttle,
                                   m_throttle_rate * dt);
    m_signals.throttle_pwm_duty = static_cast<int>(m_actual_throttle / 100.0 * 4095);

    // ── Электродвигатель стенда ───────────────────────────────────────────────
    m_signals.dyno_mode = sp.dyno_mode;
    if (sp.dyno_mode == DynoMotorMode::SPIN) {
        m_actual_speed = moveToward(m_actual_speed, sp.target_speed,
                                    m_dyno_accel * dt);
        m_actual_torque = 0.0;
        m_signals.dyno_speed_voltage  = (m_actual_speed / 3000.0) * 10.0;
        m_signals.dyno_torque_current = 4.0;
    } else {
        m_actual_torque = moveToward(m_actual_torque, sp.target_torque,
                                     m_torque_slew_rate * dt);
        m_actual_speed = 0.0;
        m_signals.dyno_torque_current = 4.0 + (m_actual_torque / 200.0) * 16.0;
        m_signals.dyno_speed_voltage  = 0.0;
    }

    // ── Вентиляторы с задержкой ───────────────────────────────────────────────
    updateFanTimer(m_fan_timer_engine,   m_actual_engine_fan,   sp.engine_fan,    dt);
    updateFanTimer(m_fan_timer_dyno,     m_actual_dyno_fan,     sp.dyno_motor_fan,dt);
    updateFanTimer(m_fan_timer_resistor, m_actual_resistor_fan, sp.resistor_fan,  dt);

    m_signals.engine_fan_on     = m_actual_engine_fan;
    m_signals.dyno_motor_fan_on = m_actual_dyno_fan;
    m_signals.resistor_fan_on   = m_actual_resistor_fan;

    // ── Обратная связь ────────────────────────────────────────────────────────
    m_feedback.throttle_actual       = m_actual_throttle;
    m_feedback.dyno_speed_actual     = m_actual_speed;
    m_feedback.dyno_torque_actual    = m_actual_torque;
    m_feedback.engine_fan_actual     = m_actual_engine_fan;
    m_feedback.dyno_motor_fan_actual = m_actual_dyno_fan;
    m_feedback.resistor_fan_actual   = m_actual_resistor_fan;
}

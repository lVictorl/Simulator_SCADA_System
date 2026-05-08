#pragma once
#include "datatypes.h"

/**
 * Виртуальный драйвер исполнительных механизмов.
 * Моделирует инерционность заслонки, разгон ЭД, задержки вентиляторов.
 */
class ActuatorDriver
{
public:
    ActuatorDriver(double throttleRate  = 40.0,
                   double dynoAccel     = 500.0,
                   double torqueSlewRate= 100.0,
                   double fanDelay      = 0.5);

    /** Принимает уставки, обновляет внутреннее состояние и обратную связь. */
    void applySetpoints(const ActuatorSetpoints &sp, double dt);

    const ActuatorSignals  &actuatorSignals() const { return m_signals; }
    const ActuatorFeedback &feedback() const { return m_feedback; }

private:
    ActuatorSignals  m_signals;
    ActuatorFeedback m_feedback;

    double m_throttle_rate;
    double m_dyno_accel;
    double m_torque_slew_rate;
    double m_fan_delay;

    double m_actual_throttle = 0.0;
    double m_actual_speed    = 0.0;
    double m_actual_torque   = 0.0;

    double m_fan_timer_engine  = 0.0;
    double m_fan_timer_dyno    = 0.0;
    double m_fan_timer_resistor= 0.0;
    bool   m_cmd_engine_fan    = false;
    bool   m_cmd_dyno_fan      = false;
    bool   m_cmd_resistor_fan  = false;
    bool   m_actual_engine_fan = false;
    bool   m_actual_dyno_fan   = false;
    bool   m_actual_resistor_fan=false;

    static double moveToward(double current, double target, double maxStep) noexcept;
    bool  updateFanTimer(double &timer, bool &actual, bool cmd, double dt);
};

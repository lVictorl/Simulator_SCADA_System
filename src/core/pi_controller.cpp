#include "pi_controller.h"
#include <algorithm>

PIController::PIController(double kp, double ki, double maxIntegral)
    : m_kp(kp), m_ki(ki), m_max_integral(maxIntegral)
{}

void PIController::reset()
{
    m_integral = 0.0;
}

double PIController::update(double setpoint, double actual, double dt)
{
    const double error = setpoint - actual;
    m_integral = std::clamp(m_integral + error * dt, -m_max_integral, m_max_integral);
    const double output = m_kp * error + m_ki * m_integral;
    return std::clamp(output, 0.0, 100.0);
}

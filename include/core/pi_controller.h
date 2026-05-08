#pragma once

/**
 * Пропорционально-интегральный регулятор с ограничением интегральной суммы (anti-windup).
 * Используется для автоматического управления дросселем по заданным оборотам.
 */
class PIController
{
public:
    PIController(double kp = 0.05, double ki = 0.01, double maxIntegral = 100.0);

    /** Вычисляет управляющий сигнал 0..100 %. */
    double update(double setpoint, double actual, double dt);

    /** Сброс интегральной составляющей. */
    void reset();

private:
    double m_kp;
    double m_ki;
    double m_max_integral;
    double m_integral = 0.0;
};

/**
 * @file engine_model.cpp
 * @brief Математическая модель дизельного двигателя и стенда.
 *
 * Модель использует метод Эйлера первого порядка для численного интегрирования.
 * Шаг интегрирования dt = 20 мс (задаётся из VirtualMCU).
 *
 * Физические допущения:
 *  - Одномассовая модель ротора (один момент инерции j_inertia)
 *  - Тепловая модель — RC-цепочка (тепловой источник + потери)
 *  - Давления — статические функции от оборотов и температуры
 *  - Уровни жидкостей — простая убыль, пропорциональная нагрузке
 */

#include "engine_model.h"
#include <QtCore/QElapsedTimer>
#include <cmath>
#include <algorithm>

// ── Глобальный таймер для метки времени ─────────────────────────────────────
// Singleton-паттерн через статическую переменную + структуру с конструктором.
// QElapsedTimer запускается при первой загрузке библиотеки.
static QElapsedTimer s_timer;
namespace {
    struct TimerInit { TimerInit() { s_timer.start(); } } s_init;
}

// ── Конструктор ──────────────────────────────────────────────────────────────
EngineModel::EngineModel(double ambientTemp)
    : m_ambient(ambientTemp)
    , m_engine_temp(ambientTemp)       // начальная температура = окружающая среда
    , m_dyno_motor_temp(ambientTemp)
    , m_resistor_temp(ambientTemp)
{}

// ── Вспомогательная функция зажима значения ──────────────────────────────────
double EngineModel::clamp(double v, double lo, double hi) noexcept
{
    return std::max(lo, std::min(hi, v));
}

// ── Главный метод — один шаг интегрирования ──────────────────────────────────
void EngineModel::step(double dt, const ActuatorSetpoints &cmd)
{
    // Нормализуем дроссель в диапазон [0, 100]
    const double throttle = clamp(cmd.throttle, 0.0, 100.0);

    // ── БЛОК 1: Динамика оборотов и крутящий момент ──────────────────────────
    //
    // Два режима: холодная прокрутка и горячий ход.
    // Холодная прокрутка: апериодическое звено 1-го порядка.
    //   Математически: dx/dt = (x_target - x) / tau
    //   Дискретизация: x[k+1] = x[k] + (x_target - x[k]) * (dt/tau)
    //
    if (!cmd.engine_running) {
        // ── Холодная прокрутка: ЭД вращает вал с заданной скоростью ──
        m_engine_rpm += (cmd.target_speed - m_engine_rpm) * (dt / m_inertia_tau);
        // Момент трения пропорционален оборотам + постоянная составляющая
        m_torque = m_friction_coeff * m_engine_rpm + 5.0;
    } else {
        // ── Горячий ход: 2-й закон Ньютона для вращательного движения ──
        // M_двигателя = throttle * gain    (линейная аппроксимация)
        const double engine_torque  = throttle * m_engine_torque_gain;

        // M_трение = ω * k_friction         (вязкое трение)
        const double friction_torque = m_engine_rpm * m_friction_coeff;

        // M_нагрузка = target_torque (только в режиме BRAKE)
        const double load_torque = (cmd.dyno_mode == DynoMotorMode::BRAKE)
                                   ? cmd.target_torque : 0.0;

        // Результирующий момент → угловое ускорение → прирост оборотов
        // J * dω/dt = M_net  →  ω[k+1] = ω[k] + M_net * dt / J
        const double net_torque = engine_torque - load_torque - friction_torque;
        m_engine_rpm += net_torque * (dt / m_j_inertia);

        // Измеряемый момент — то, что видит датчик
        m_torque = (cmd.dyno_mode == DynoMotorMode::BRAKE) ? load_torque : engine_torque;
    }

    // Физическое ограничение: отрицательных оборотов не бывает
    if (m_engine_rpm < 0.0) m_engine_rpm = 0.0;

    // ── БЛОК 2: Тепловая модель двигателя ────────────────────────────────────
    //
    // Аналог RC-цепочки:
    //   dT/dt = (Q_нагрев - Q_охлаждение_естественное - Q_охлаждение_вентилятор)
    //
    {
        // Нагрев пропорционален нагрузке (дросселю)
        const double heat = cmd.engine_running ? throttle * m_heat_from_throttle : 0.0;

        // Естественное охлаждение (конвекция): пропорционально перегреву над ambient
        const double cooling = m_natural_cooling * (m_engine_temp - m_ambient);

        // Дополнительное охлаждение вентилятором
        const double fan_cool = cmd.engine_fan
                                ? m_fan_cooling_rate * (m_engine_temp - m_ambient) : 0.0;

        m_engine_temp += (heat - cooling - fan_cool) * dt;
    }

    // ── БЛОК 3: Давления ─────────────────────────────────────────────────────
    //
    // Эмпирические формулы для типичного дизельного двигателя:
    //   Масло: растёт с оборотами (насос), падает при перегреве (вязкость)
    //   Топливо: линейно растёт с оборотами (ТНВД)
    //   Наддув: нелинейно зависит от оборотов и дросселя

    m_oil_pressure = clamp(
        1.5 + 0.0005 * m_engine_rpm - 0.005 * (m_engine_temp - 20.0),
        0.0, 20.0);

    m_fuel_pressure = 3.0 + 0.0001 * m_engine_rpm;

    // Наддув появляется только при достаточных оборотах (>1000) и нагрузке
    m_boost_pressure = (cmd.engine_running && m_engine_rpm > 1000.0)
        ? 0.8 * (m_engine_rpm - 1000.0) / 3000.0 * throttle / 100.0
        : 0.0;

    // ── БЛОК 4: Уровни жидкостей ─────────────────────────────────────────────
    //
    // Простая модель убыли: расход пропорционален нагрузке.
    // Масло расходуется медленнее чем топливо.
    m_oil_level  = clamp(m_oil_level  - 0.001 * throttle * dt, 0.0, 100.0);
    m_fuel_level = clamp(m_fuel_level - 0.005 * throttle * dt, 0.0, 100.0);

    // ── БЛОК 5: Тепловая модель ЭД и тормозного резистора ───────────────────
    //
    // Мощность рассеяния = момент × угловая скорость (только в режиме BRAKE).
    // Резистор рассеивает 90% мощности (ЭД — более эффективен).
    {
        const double power_loss = (cmd.engine_running && cmd.dyno_mode == DynoMotorMode::BRAKE)
            ? m_dyno_heat_factor * std::abs(cmd.target_torque) * m_engine_rpm : 0.0;

        // Тепловой баланс ЭД
        const double cool_dyno = m_natural_cooling * (m_dyno_motor_temp - m_ambient)
            + (cmd.dyno_motor_fan
               ? m_fan_cooling_rate * (m_dyno_motor_temp - m_ambient) : 0.0);
        m_dyno_motor_temp += (power_loss - cool_dyno) * dt;

        // Тепловой баланс резистора (90% мощности рассеяния)
        const double cool_res = m_natural_cooling * (m_resistor_temp - m_ambient)
            + (cmd.resistor_fan
               ? m_fan_cooling_rate * (m_resistor_temp - m_ambient) : 0.0);
        m_resistor_temp += (power_loss * 0.9 - cool_res) * dt;
    }
}

// ── Геттер текущего состояния ────────────────────────────────────────────────
SensorData EngineModel::state() const
{
    SensorData s;
    // Метка времени — миллисекунды глобального таймера → секунды
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
    s.fault_mask      = 0; // модель не знает об отказах — это задача SensorSimulator
    return s;
}

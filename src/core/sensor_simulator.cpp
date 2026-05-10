#include "sensor_simulator.h"
#include <cmath>
#include <limits>

SensorSimulator::SensorSimulator(const EngineModel &model, unsigned int seed)
    : m_model(model)
    , m_rng(seed)
{}

void SensorSimulator::setFault(int sensorIndex, SensorFaultType type)
{
    if (sensorIndex >= 0 && sensorIndex <= 5)
        m_faults[sensorIndex] = type;
}

void SensorSimulator::clearFault(int sensorIndex)
{
    m_faults.erase(sensorIndex);
}

void SensorSimulator::clearAllFaults()
{
    m_faults.clear();
}

double SensorSimulator::processChannel(int idx, double trueValue, double noise) const
{
    auto it = m_faults.find(idx);
    if (it == m_faults.end())
        return trueValue + m_dist(m_rng) * noise;

    switch (it->second) {
    case SensorFaultType::NONE:
        return trueValue + m_dist(m_rng) * noise;
    case SensorFaultType::OPEN_CIRCUIT:
        return std::numeric_limits<double>::quiet_NaN();
    case SensorFaultType::SHORT_CIRCUIT:
        return 1e6;
    case SensorFaultType::OUT_OF_RANGE:
        return trueValue * 10.0 + 500.0;
    }
    return trueValue;
}

SensorData SensorSimulator::read() const
{
    const SensorData s = m_model.state();

    SensorData out;
    out.timestamp    = s.timestamp;
    out.engine_rpm   = processChannel(0, s.engine_rpm,     NOISE_RPM);
    out.torque       = processChannel(1, s.torque,         NOISE_TORQUE);
    out.engine_temp  = processChannel(2, s.engine_temp,    NOISE_TEMP);
    out.oil_pressure = processChannel(3, s.oil_pressure,   NOISE_PRESSURE);
    out.fuel_pressure= processChannel(3, s.fuel_pressure,  NOISE_PRESSURE);
    out.boost_pressure=processChannel(3, s.boost_pressure, NOISE_PRESSURE);
    out.dyno_motor_temp=processChannel(4, s.dyno_motor_temp,NOISE_TEMP);
    out.resistor_temp= processChannel(5, s.resistor_temp,  NOISE_TEMP);
    out.oil_level    = s.oil_level  + m_dist(m_rng) * NOISE_LEVEL;
    out.fuel_level   = s.fuel_level + m_dist(m_rng) * NOISE_LEVEL;

    // Сборка маски неисправностей
    quint16 mask = 0;
    for (int i = 0; i <= 5; ++i) {
        auto it = m_faults.find(i);
        if (it != m_faults.end() && it->second != SensorFaultType::NONE)
            mask |= (1u << i);
    }
    out.fault_mask = mask;

    return out;
}

#pragma once
#include "datatypes.h"
#include "engine_model.h"
#include <unordered_map>
#include <random>

/**
 * Симулятор реальных датчиков.
 * Считывает «истинные» значения из EngineModel, добавляет гауссов шум,
 * моделирует неисправности (обрыв, КЗ, уход диапазона).
 *
 * Индексы датчиков (биты fault_mask):
 *   0 – обороты, 1 – момент, 2 – температура ДВС,
 *   3 – давления, 4 – температура ЭД, 5 – температура резистора
 */
class SensorSimulator
{
public:
    explicit SensorSimulator(const EngineModel &model, unsigned int seed = 42);

    void setFault(int sensorIndex, SensorFaultType type);
    void clearFault(int sensorIndex);
    void clearAllFaults();

    SensorData read() const;

private:
    const EngineModel &m_model;
    mutable std::mt19937 m_rng;
    mutable std::normal_distribution<double> m_dist{0.0, 1.0};

    std::unordered_map<int, SensorFaultType> m_faults;

    // СКО шумов
    static constexpr double NOISE_RPM      = 5.0;
    static constexpr double NOISE_TORQUE   = 0.5;
    static constexpr double NOISE_TEMP     = 0.3;
    static constexpr double NOISE_PRESSURE = 0.02;
    static constexpr double NOISE_LEVEL    = 0.1;

    double processChannel(int idx, double trueValue, double noise) const;
    quint16 buildFaultMask(const std::vector<std::pair<int,bool>> &hits) const;
};
